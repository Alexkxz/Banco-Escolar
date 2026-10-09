import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { execFileSync } from 'node:child_process'
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'
import { fileURLToPath } from 'node:url'
import { deflateSync } from 'node:zlib'
import { chromium, expect } from '@playwright/test'

const appDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const viteCli = path.join(appDir, 'node_modules', 'vite', 'bin', 'vite.js')
const origin = 'http://127.0.0.1:4187'
const databaseName = 'banco-escolar-panel-demo'
let viteProcess
let context
let browser
let chromeProcess
let viteClosed
let chromeClosed
let chromeExecutablePath
let viteArguments
let profileDir
let serverOutput = ''

function pngChunk(name, body) {
  const type = Buffer.from(name)
  const length = Buffer.alloc(4)
  length.writeUInt32BE(body.length)
  const crcInput = Buffer.concat([type, body])
  let crc = 0xffffffff
  for (const byte of crcInput) {
    crc ^= byte
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0)
  }
  const crcBuffer = Buffer.alloc(4)
  crcBuffer.writeUInt32BE((crc ^ 0xffffffff) >>> 0)
  return Buffer.concat([length, type, body, crcBuffer])
}

function makePng(width, height, color) {
  const signature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])
  const header = Buffer.alloc(13)
  header.writeUInt32BE(width, 0); header.writeUInt32BE(height, 4)
  header[8] = 8; header[9] = 6
  const rows = Buffer.alloc(height * (1 + width * 4))
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
    const offset = y * (1 + width * 4) + 1 + x * 4
    rows.set(color, offset)
  }
  return Buffer.concat([
    signature,
    pngChunk('IHDR', header),
    pngChunk('IDAT', deflateSync(rows)),
    pngChunk('IEND', Buffer.alloc(0)),
  ])
}

function observeClose(child) {
  return new Promise((resolve) => {
    child.once('close', (code, signal) => resolve({ code, signal }))
    child.once('error', (error) => resolve({ error }))
  })
}

async function waitForClose(child, closed, label, timeoutMs = 5000) {
  if (!child) return null
  if (child.exitCode !== null || child.signalCode !== null) {
    return { code: child.exitCode, signal: child.signalCode }
  }
  return Promise.race([closed, delay(timeoutMs).then(() => null)])
}

function isExpectedWindowsProcess(child, executablePath, commandLineParts) {
  if (!child?.pid || process.platform !== 'win32') return false
  const expectedExecutable = executablePath.replaceAll("'", "''")
  const expectedParts = commandLineParts.map((part) => `$process.CommandLine.Contains('${part.replaceAll("'", "''")}')`).join(' -and ')
  const script = [
    `$process = Get-CimInstance Win32_Process -Filter "ProcessId=${child.pid}"`,
    `if ($process -and $process.ExecutablePath -ieq '${expectedExecutable}' -and ${expectedParts}) { exit 0 } else { exit 1 }`,
  ].join('; ')
  try {
    execFileSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', script], {
      stdio: 'ignore',
      timeout: 5000,
      windowsHide: true,
    })
    return true
  } catch (error) {
    if (error?.status === 1) return false
    throw new Error(`Could not verify identity for owned child PID ${child.pid}: ${error?.message ?? error}`)
  }
}

async function readDatabase(page) {
  return page.evaluate(async (name) => {
    const db = await new Promise((resolve, reject) => {
      const opening = indexedDB.open(name)
      opening.onsuccess = () => resolve(opening.result)
      opening.onerror = () => reject(opening.error)
    })
    return new Promise((resolve, reject) => {
      const tx = db.transaction(['meta', 'students', 'accounts', 'movements', 'studentImages'], 'readonly')
      const values = {}
      const requests = [
        ['seedCompleted', tx.objectStore('meta').get('seedCompleted')],
        ['revision', tx.objectStore('meta').get('revision')],
        ['students', tx.objectStore('students').getAll()],
        ['accounts', tx.objectStore('accounts').getAll()],
        ['movements', tx.objectStore('movements').getAll()],
        ['images', tx.objectStore('studentImages').getAll()],
      ]
      for (const [key, request] of requests) request.onsuccess = () => { values[key] = request.result }
      tx.oncomplete = async () => {
        try {
          const images = await Promise.all((values.images ?? []).map(async (image) => ({
            student_id: image.student_id,
            mimeType: image.mimeType,
            fileName: image.fileName,
            byteLength: image.byteLength,
            width: image.width,
            height: image.height,
            blobType: image.blob?.type,
            blobSize: image.blob?.size,
            bytes: Array.from(new Uint8Array(await image.blob.arrayBuffer())),
          })))
          db.close()
          resolve({
            seedCompleted: values.seedCompleted?.value,
            revision: values.revision?.value,
            students: values.students ?? [],
            accounts: values.accounts ?? [],
            movements: values.movements ?? [],
            images,
          })
        } catch (error) { db.close(); reject(error) }
      }
      tx.onerror = () => { db.close(); reject(tx.error ?? new Error('IndexedDB read transaction failed')) }
      tx.onabort = () => { db.close(); reject(tx.error ?? new Error('IndexedDB read transaction aborted')) }
    })
  }, databaseName)
}

async function waitForServer() {
  for (let attempt = 0; attempt < 80; attempt++) {
    if (viteProcess.exitCode !== null) throw new Error(`Vite exited with code ${viteProcess.exitCode}. ${serverOutput}`)
    try {
      const response = await fetch(origin)
      if (response.ok) return
    } catch { /* server is still starting */ }
    await delay(100)
  }
  throw new Error(`Vite did not become ready at ${origin}. ${serverOutput}`)
}

async function stopVite() {
  if (!viteProcess) return
  if (viteProcess.exitCode === null && viteProcess.signalCode === null) {
    if (!isExpectedWindowsProcess(viteProcess, process.execPath, viteArguments)) {
      const alreadyClosed = await waitForClose(viteProcess, viteClosed, 'Vite', 1000)
      if (alreadyClosed === null) throw new Error(`Vite PID ${viteProcess.pid} no longer matches its recorded path/arguments; it was not signaled.`)
    } else {
      console.log(`Verified Vite PID ${viteProcess.pid} executable and port 4187 arguments before targeted stop.`)
      viteProcess.kill()
    }
  }
  const result = await waitForClose(viteProcess, viteClosed, 'Vite')
  if (result === null) throw new Error(`Vite child PID ${viteProcess.pid} did not emit close within 5000 ms.`)
  if (result.error) throw result.error
  console.log(`Closed Vite PID ${viteProcess.pid}; exit=${result.code}; signal=${result.signal}.`)
}

async function waitForChromeDebugPort(activePortFile) {
  for (let attempt = 0; attempt < 100; attempt++) {
    if (chromeProcess.exitCode !== null) throw new Error(`Chromium PID ${chromeProcess.pid} exited with code ${chromeProcess.exitCode} before CDP was ready.`)
    if (existsSync(activePortFile)) {
      const [portLine] = readFileSync(activePortFile, 'utf8').trim().split(/\r?\n/)
      const port = Number(portLine)
      if (Number.isInteger(port) && port > 0) return port
    }
    await delay(100)
  }
  throw new Error(`Chromium PID ${chromeProcess.pid} did not publish DevToolsActivePort.`)
}

async function stopChrome() {
  if (browser) {
    await browser.close()
    browser = null
    context = null
  }
  if (!chromeProcess) return
  let closed = await waitForClose(chromeProcess, chromeClosed, 'Chromium')
  if (closed === null && chromeProcess.exitCode === null && chromeProcess.signalCode === null) {
    if (!isExpectedWindowsProcess(chromeProcess, chromeExecutablePath, [`--user-data-dir=${profileDir}`])) {
      closed = await waitForClose(chromeProcess, chromeClosed, 'Chromium', 1000)
      if (closed === null) throw new Error(`Chromium PID ${chromeProcess.pid} no longer matches its executable/profile; it was not signaled.`)
    } else {
      console.log(`Verified Chromium PID ${chromeProcess.pid}, executable and isolated profile before targeted stop.`)
      chromeProcess.kill()
    }
    closed = await waitForClose(chromeProcess, chromeClosed, 'Chromium')
  }
  if (closed === null) throw new Error(`Chromium PID ${chromeProcess.pid} did not emit close after Playwright close and targeted child stop.`)
  if (closed.error) throw closed.error
  console.log(`Closed Chromium PID ${chromeProcess.pid}; exit=${closed.code}; signal=${closed.signal}.`)
}

async function openProfile() {
  chromeExecutablePath = chromium.executablePath()
  assert.ok(existsSync(chromeExecutablePath), `Chromium executable is missing: ${chromeExecutablePath}`)
  const activePortFile = path.join(profileDir, 'DevToolsActivePort')
  rmSync(activePortFile, { force: true })
  chromeProcess = spawn(chromeExecutablePath, [
    '--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check',
    '--remote-debugging-address=127.0.0.1', '--remote-debugging-port=0',
    `--user-data-dir=${profileDir}`, 'about:blank',
  ], { stdio: 'ignore', windowsHide: true })
  chromeClosed = observeClose(chromeProcess)
  console.log(`Started temporary Chromium PID ${chromeProcess.pid} with profile ${profileDir}; it will be closed in finally.`)
  const port = await waitForChromeDebugPort(activePortFile)
  browser = await chromium.connectOverCDP(`http://127.0.0.1:${port}`)
  context = browser.contexts()[0]
  assert.ok(context, 'Chromium did not expose its temporary persistent context')
  return context.pages()[0] ?? context.newPage()
}

async function prepareAdjustment(page, amount) {
  await page.getByRole('button', { name: 'Agregar Áureos' }).click()
  await page.getByLabel('Cantidad de Áureos').fill(String(amount))
  await page.getByRole('button', { name: 'Revisar ajuste' }).click()
  await expect(page.getByRole('region', { name: 'Confirmación del ajuste' })).toBeVisible()
}

function accountBalance(snapshot, studentId) {
  const account = snapshot.accounts.find((entry) => entry.student_id === studentId)
  assert.ok(account, `No account row found for student ${studentId}`)
  return account.balance
}

async function main() {
  const browserPath = chromium.executablePath()
  assert.ok(existsSync(browserPath), `Chromium executable is missing: ${browserPath}`)
  viteProcess = spawn(process.execPath, [viteCli, '--host', '127.0.0.1', '--port', '4187', '--strictPort'], {
    cwd: appDir,
    stdio: ['ignore', 'pipe', 'pipe'],
    windowsHide: true,
  })
  viteArguments = [viteCli, '--port 4187', '--strictPort']
  viteClosed = observeClose(viteProcess)
  viteProcess.stdout.on('data', (data) => { serverOutput += data.toString() })
  viteProcess.stderr.on('data', (data) => { serverOutput += data.toString() })
  console.log(`Started temporary Vite PID ${viteProcess.pid} for ${origin}; it will be stopped in finally.`)
  await waitForServer()

  profileDir = mkdtempSync(path.join(tmpdir(), 'banco-escolar-pm9b-'))
  console.log(`Created isolated temporary Chromium profile ${profileDir}; it will be removed after browser closure.`)
  let page = await openProfile()
  await page.goto(`${origin}/dashboard`)
  await expect(page.getByRole('heading', { name: 'Dashboard' })).toBeVisible()

  // 1. Fresh seed appears once, then a saved operation survives reload without reseeding.
  let snapshot = await readDatabase(page)
  assert.equal(snapshot.seedCompleted, true)
  assert.equal(snapshot.students.length, 6)
  assert.equal(snapshot.accounts.length, 5)
  assert.equal(snapshot.movements.length, 10)
  assert.equal(accountBalance(snapshot, 201), 125)
  assert.equal(snapshot.revision, 1)
  await page.goto(`${origin}/alumnos/201`)
  await expect(page.locator('.profile-balance-value strong')).toContainText('125')
  await prepareAdjustment(page, 5)
  await page.getByRole('button', { name: 'Confirmar ajuste' }).click()
  await expect(page.locator('.adjustment-success')).toContainText('Ajuste aplicado')
  snapshot = await readDatabase(page)
  assert.equal(accountBalance(snapshot, 201), 130)
  assert.equal(snapshot.movements.length, 11)
  await page.reload()
  await expect(page.locator('.profile-balance-value strong')).toContainText('130')
  snapshot = await readDatabase(page)
  assert.equal(snapshot.students.length, 6)
  assert.equal(snapshot.accounts.length, 5)
  assert.equal(snapshot.movements.length, 11)
  assert.equal(accountBalance(snapshot, 201), 130)
  console.log('PASS 1/6: one-time seed and saved edits survive reload without reseeding.')

  // Prepare an original test image from generated bytes; no repository image is read or changed.
  const firstPng = makePng(2, 2, [220, 20, 60, 255])
  const secondPng = makePng(3, 1, [30, 90, 210, 255])
  const photoInput = page.locator('input[type="file"]')
  await expect(photoInput).toBeEnabled()
  await photoInput.setInputFiles({ name: 'pm9b-first.png', mimeType: 'image/png', buffer: firstPng })
  await expect(page.getByText(/Vista previa local: pm9b-first\.png/)).toBeVisible()
  snapshot = await readDatabase(page)
  assert.equal(snapshot.images.length, 1)
  assert.deepEqual(snapshot.images[0], {
    student_id: 201, mimeType: 'image/png', fileName: 'pm9b-first.png', byteLength: firstPng.length,
    width: 2, height: 2, blobType: 'image/png', blobSize: firstPng.length, bytes: Array.from(firstPng),
  })

  // 2. Reload, close Chromium, then reopen the same isolated profile and origin.
  await page.reload()
  await expect(page.getByText(/Vista previa local: pm9b-first\.png/)).toBeVisible()
  assert.equal(accountBalance(await readDatabase(page), 201), 130)
  await stopChrome()
  page = await openProfile()
  await page.goto(`${origin}/alumnos/201`)
  await expect(page.getByText(/Vista previa local: pm9b-first\.png/)).toBeVisible()
  await expect(page.locator('.profile-balance-value strong')).toContainText('130')
  snapshot = await readDatabase(page)
  assert.equal(snapshot.movements.length, 11)
  assert.equal(snapshot.images[0].student_id, 201)
  console.log('PASS 2/6: account, movement and Blob persist through reload and browser close/reopen on the same profile.')

  // 3. Replacement and removal remain scoped to the correct student ID.
  await expect(page.locator('input[type="file"]')).toBeEnabled()
  await page.locator('input[type="file"]').setInputFiles({ name: 'pm9b-second.png', mimeType: 'image/png', buffer: secondPng })
  await expect(page.getByText(/Vista previa local: pm9b-second\.png/)).toBeVisible()
  snapshot = await readDatabase(page)
  assert.equal(snapshot.images.length, 1)
  assert.equal(snapshot.images[0].student_id, 201)
  assert.equal(snapshot.images[0].fileName, 'pm9b-second.png')
  assert.deepEqual(snapshot.images[0].bytes, Array.from(secondPng))
  await page.reload()
  await expect(page.getByText(/Vista previa local: pm9b-second\.png/)).toBeVisible()
  await page.goto(`${origin}/alumnos/202`)
  await expect(page.getByRole('heading', { name: 'Bruno Vega' })).toBeVisible()
  await expect(page.getByText(/Vista previa local/)).toHaveCount(0)
  await page.goto(`${origin}/alumnos/201`)
  await expect(page.getByRole('button', { name: 'Quitar imagen' })).toBeVisible()
  await page.getByRole('button', { name: 'Quitar imagen' }).click()
  await expect(page.getByText(/Vista previa local/)).toHaveCount(0)
  snapshot = await readDatabase(page)
  assert.equal(snapshot.images.length, 0)
  await page.reload()
  await expect(page.getByText(/Vista previa local/)).toHaveCount(0)
  console.log('PASS 3/6: replacing and removing photos persists and remains scoped to student 201.')

  // 4. Abort the native readwrite transaction at movements.put; accounts must roll back too.
  snapshot = await readDatabase(page)
  const balanceBeforeAbort = accountBalance(snapshot, 201)
  const movementCountBeforeAbort = snapshot.movements.length
  await page.evaluate(() => {
    const prototype = IDBObjectStore.prototype
    const originalPut = prototype.put
    window.__pm9bAbortState = { prototype, originalPut, armed: true }
    prototype.put = function (...args) {
      const state = window.__pm9bAbortState
      if (state?.armed && this.name === 'movements') {
        state.armed = false
        this.transaction.abort()
        throw new DOMException('Controlled PM.9B transaction abort', 'AbortError')
      }
      return originalPut.apply(this, args)
    }
  })
  await prepareAdjustment(page, 3)
  await page.getByRole('button', { name: 'Confirmar ajuste' }).click()
  await expect(page.locator('.adjustment-error')).toContainText('No se aplicó el ajuste')
  await expect(page.locator('.adjustment-success')).toHaveCount(0)
  await expect(page.getByLabel('Cantidad de Áureos')).toHaveValue('3')
  await page.evaluate(() => {
    const state = window.__pm9bAbortState
    if (state) state.prototype.put = state.originalPut
    delete window.__pm9bAbortState
  })
  await page.reload()
  snapshot = await readDatabase(page)
  assert.equal(accountBalance(snapshot, 201), balanceBeforeAbort)
  assert.equal(snapshot.movements.length, movementCountBeforeAbort)
  console.log('PASS 4/6: controlled native transaction abort rolls back balance and movement together; UI reports failure.')

  // 5. Two tabs race against the same expected balance and database revision.
  const secondPage = await context.newPage()
  await Promise.all([page.goto(`${origin}/alumnos/201`), secondPage.goto(`${origin}/alumnos/201`)] )
  await Promise.all([
    expect(page.locator('.profile-balance-value strong')).toContainText(String(balanceBeforeAbort)),
    expect(secondPage.locator('.profile-balance-value strong')).toContainText(String(balanceBeforeAbort)),
  ])
  await Promise.all([prepareAdjustment(page, 2), prepareAdjustment(secondPage, 2)])
  await Promise.all([
    page.getByRole('button', { name: 'Confirmar ajuste' }).click(),
    secondPage.getByRole('button', { name: 'Confirmar ajuste' }).click(),
  ])
  await expect.poll(async () => accountBalance(await readDatabase(page), 201)).toBe(balanceBeforeAbort + 2)
  snapshot = await readDatabase(page)
  assert.equal(snapshot.movements.length, movementCountBeforeAbort + 1)
  const successes = await page.locator('.adjustment-success').count() + await secondPage.locator('.adjustment-success').count()
  assert.equal(successes, 1, 'exactly one concurrent confirmation should report success')
  await Promise.all([page.reload(), secondPage.reload()])
  snapshot = await readDatabase(page)
  assert.equal(accountBalance(snapshot, 201), balanceBeforeAbort + 2)
  assert.equal(snapshot.movements.length, movementCountBeforeAbort + 1)
  await secondPage.close()
  console.log('PASS 5/6: concurrent stale confirmations produce only one balance and movement update.')

  // 6. A separate page with native IndexedDB open failure must show the storage gate, never RAM data.
  const unavailablePage = await context.newPage()
  await unavailablePage.addInitScript(() => {
    const prototype = IDBFactory.prototype
    Object.defineProperty(prototype, 'open', {
      configurable: true,
      writable: true,
      value() { throw new DOMException('Controlled IndexedDB open failure', 'SecurityError') },
    })
  })
  await unavailablePage.goto(`${origin}/dashboard`)
  await expect(unavailablePage.getByRole('heading', { name: 'Almacenamiento local no disponible' })).toBeVisible()
  await expect(unavailablePage.getByRole('heading', { name: 'Dashboard' })).toHaveCount(0)
  await expect(unavailablePage.getByText(/No se cargaron datos de demostración en memoria/)).toBeVisible()
  await unavailablePage.close()
  snapshot = await readDatabase(page)
  assert.equal(accountBalance(snapshot, 201), balanceBeforeAbort + 2)
  assert.equal(snapshot.movements.length, movementCountBeforeAbort + 1)
  console.log('PASS 6/6: IndexedDB open failure is explicit; no success, dashboard or memory fallback is shown.')
}

let exitCode = 0
try {
  await main()
  console.log('PASS: all six PM.9B native IndexedDB browser scenarios.')
} catch (error) {
  exitCode = 1
  console.error(error?.stack ?? error)
  if (serverOutput) console.error(`Vite output:\n${serverOutput}`)
  } finally {
    try {
    if (browser || chromeProcess) await stopChrome()
    } catch (error) { exitCode = 1; console.error(`Failed closing Chromium context: ${error?.stack ?? error}`) }
  try { await stopVite() }
  catch (error) { exitCode = 1; console.error(`Failed stopping temporary Vite: ${error?.stack ?? error}`) }
  if (profileDir && (!chromeProcess || (chromeProcess.exitCode !== null || chromeProcess.signalCode !== null))) {
    try {
      rmSync(profileDir, { recursive: true, force: true })
      assert.equal(existsSync(profileDir), false, 'temporary browser profile should be removed')
      console.log('Removed temporary Chromium profile; verified it no longer exists.')
    } catch (error) { exitCode = 1; console.error(`Failed removing temporary profile ${profileDir}: ${error?.stack ?? error}`) }
  } else if (profileDir) {
    console.error(`Retaining temporary Chromium profile because process closure is unconfirmed: ${profileDir}`)
    exitCode = 1
  }
}

process.exitCode = exitCode
