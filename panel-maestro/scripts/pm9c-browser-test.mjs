import assert from 'node:assert/strict'
import { spawn, execFileSync } from 'node:child_process'
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'
import { createHash } from 'node:crypto'
import { fileURLToPath } from 'node:url'
import { deflateSync } from 'node:zlib'
import { chromium, expect } from '@playwright/test'

const appDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const repoDir = path.resolve(appDir, '..')
const viteCli = path.join(appDir, 'node_modules', 'vite', 'bin', 'vite.js')
const origin = 'http://127.0.0.1:4187'
const databaseName = 'banco-escolar-panel-demo'
const storeNames = ['meta', 'students', 'studentImages', 'accounts', 'movements', 'activities', 'claims', 'claimAuthorizations', 'claimEvents', 'academicRecords', 'attendanceRecords', 'academicRuleVersions', 'attendanceRuleVersions', 'schoolApplications', 'operationReceipts']
let viteProcess
let viteClosed
let viteOutput = ''
let browserContext
let profileDir

function inspectPort4187() {
  const script = [
    "$ErrorActionPreference = 'Stop'",
    'try { $listeners = @(Get-NetTCPConnection -State Listen -LocalPort 4187 -ErrorAction Stop) }',
    'catch {',
    '  $message = $_.Exception.Message',
    "  if ($message -match 'MSFT_NetTCPConnection.*no encontr.{1,2} objetos.*coincidan' -or $message -match 'did not find any MSFT_NetTCPConnection objects') { $listeners = @() }",
    '  else { [Console]::Error.WriteLine($_.ToString()); exit 2 }',
    '}',
    '$details = foreach ($listener in $listeners) {',
    '  $owner = Get-CimInstance Win32_Process -Filter "ProcessId=$($listener.OwningProcess)" -ErrorAction Stop',
    '  [pscustomobject]@{ Address=$listener.LocalAddress; Port=$listener.LocalPort; PID=$listener.OwningProcess; CreationDate=$owner.CreationDate; ExecutablePath=$owner.ExecutablePath; CommandLine=$owner.CommandLine }',
    '}',
    'ConvertTo-Json -InputObject @($details) -Depth 4 -Compress',
    'exit 0',
  ].join('\n')
  try {
    const output = execFileSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', script], {
      encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], timeout: 8000, windowsHide: true,
    }).trim()
    if (!output) throw new Error('PowerShell exited successfully without returning listener data.')
    const result = JSON.parse(output)
    if (!Array.isArray(result)) throw new Error('PowerShell returned an invalid listener response.')
    return result
  } catch (error) {
    const status = Number.isInteger(error?.status) ? error.status : 'unknown'
    const stderr = typeof error?.stderr === 'string' ? error.stderr.trim() : ''
    throw new Error(`Port 4187 inspection failed (PowerShell exit ${status}): ${stderr || error?.message || error}`)
  }
}

function trackClose(child) {
  return new Promise((resolve) => {
    child.once('close', (code, signal) => resolve({ code, signal }))
    child.once('error', (error) => resolve({ error }))
  })
}

function verifyViteIdentity() {
  if (process.platform !== 'win32') return true
  const command = `$process = Get-CimInstance Win32_Process -Filter "ProcessId=${viteProcess.pid}"; if ($process -and $process.ExecutablePath -ieq '${process.execPath.replaceAll("'", "''")}' -and $process.CommandLine.Contains('${viteCli.replaceAll("'", "''")}') -and $process.CommandLine.Contains('--port 4187') -and $process.CommandLine.Contains('--strictPort')) { exit 0 } else { exit 1 }`
  try { execFileSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', command], { stdio: 'ignore', timeout: 5000, windowsHide: true }); return true }
  catch (error) { if (error?.status === 1) return false; throw error }
}

async function stopVite() {
  if (!viteProcess) return
  if (viteProcess.exitCode === null && viteProcess.signalCode === null) {
    if (!verifyViteIdentity()) throw new Error(`Vite PID ${viteProcess.pid} no longer matches its executable/port identity; it was not signaled.`)
    console.log(`Verified owned Vite PID ${viteProcess.pid} and port 4187 before targeted stop.`)
    viteProcess.kill()
  }
  const result = await Promise.race([viteClosed, delay(5000).then(() => null)])
  if (!result) throw new Error(`Vite PID ${viteProcess.pid} did not emit close within 5000 ms.`)
  if (result.error) throw result.error
  console.log(`Closed PM.9C Vite PID ${viteProcess.pid}; signal=${result.signal}.`)
}

function pngChunk(name, body) {
  const type = Buffer.from(name); const length = Buffer.alloc(4); length.writeUInt32BE(body.length)
  const crcInput = Buffer.concat([type, body]); let crc = 0xffffffff
  for (const byte of crcInput) { crc ^= byte; for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0) }
  const checksum = Buffer.alloc(4); checksum.writeUInt32BE((crc ^ 0xffffffff) >>> 0)
  return Buffer.concat([length, type, body, checksum])
}

function makePng(width, height, color) {
  const signature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]); const header = Buffer.alloc(13)
  header.writeUInt32BE(width, 0); header.writeUInt32BE(height, 4); header[8] = 8; header[9] = 6
  const rows = Buffer.alloc(height * (1 + width * 4))
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) rows.set(color, y * (1 + width * 4) + 1 + x * 4)
  return Buffer.concat([signature, pngChunk('IHDR', header), pngChunk('IDAT', deflateSync(rows)), pngChunk('IEND', Buffer.alloc(0))])
}

function rechecksum(envelope) {
  const payload = { format: envelope.format, version: envelope.version, workspace: envelope.workspace, schemaVersion: envelope.schemaVersion, createdAt: envelope.createdAt, revision: envelope.revision, collections: envelope.collections }
  envelope.checksum = { algorithm: 'SHA-256', value: createHash('sha256').update(JSON.stringify(payload), 'utf8').digest('hex') }
  return Buffer.from(JSON.stringify(envelope), 'utf8')
}

async function readDb(page) {
  return page.evaluate(async (name) => {
    const db = await new Promise((resolve, reject) => { const req = indexedDB.open(name); req.onsuccess = () => resolve(req.result); req.onerror = () => reject(req.error) })
    const snapshot = await new Promise((resolve, reject) => {
      const tx = db.transaction(['meta', 'students', 'studentImages', 'accounts', 'movements', 'activities', 'claims', 'claimAuthorizations', 'claimEvents', 'academicRecords', 'attendanceRecords', 'academicRuleVersions', 'attendanceRuleVersions', 'schoolApplications', 'operationReceipts'], 'readonly')
      const values = {}; for (const store of tx.objectStoreNames) { const req = tx.objectStore(store).getAll(); req.onsuccess = () => { values[store] = req.result } }
      tx.oncomplete = async () => {
        try { values.studentImages = await Promise.all((values.studentImages ?? []).map(async (image) => ({ ...image, blob: { type: image.blob.type, size: image.blob.size, base64: btoa(String.fromCharCode(...new Uint8Array(await image.blob.arrayBuffer()))) } }))); db.close(); resolve(values) }
        catch (error) { db.close(); reject(error) }
      }
      tx.onerror = () => { db.close(); reject(tx.error) }; tx.onabort = () => { db.close(); reject(tx.error) }
    })
    return snapshot
  }, databaseName)
}

async function waitForVite() {
  for (let i = 0; i < 80; i++) {
    if (viteProcess.exitCode !== null) throw new Error(`Vite exited (${viteProcess.exitCode}). ${viteOutput}`)
    try { const response = await fetch(origin); if (response.ok) return } catch { /* startup */ }
    await delay(100)
  }
  throw new Error(`Vite did not become ready at ${origin}. ${viteOutput}`)
}

async function safetyDownload(page, context, fileBuffer, fileName) {
  const downloadWait = context.waitForEvent('download')
  await page.locator('input[aria-label="Seleccionar archivo de respaldo"]').setInputFiles({ name: fileName, mimeType: 'application/json', buffer: fileBuffer })
  const download = await downloadWait
  assert.ok(await download.path(), 'The safety download must be created by the browser')
}

async function setPhoto(page, fileName, buffer) {
  await page.goto(`${origin}/alumnos/201`)
  const input = page.locator('input[type="file"]')
  await expect(input).toBeEnabled()
  await input.setInputFiles({ name: fileName, mimeType: 'image/png', buffer })
  await expect(page.getByText(new RegExp(`Vista previa local: ${fileName}`))).toBeVisible()
}

async function addAureos(page, amount) {
  await page.goto(`${origin}/alumnos/201`)
  await page.getByRole('button', { name: 'Agregar Áureos' }).click()
  await page.getByLabel('Cantidad de Áureos').fill(String(amount))
  await page.getByRole('button', { name: 'Revisar ajuste' }).click()
  await page.getByRole('button', { name: 'Confirmar ajuste' }).click()
  await expect(page.locator('.adjustment-success')).toContainText('Ajuste aplicado')
}

async function fileForImport(page, context, buffer, fileName, expectValid = true) {
  await page.goto(`${origin}/configuracion`)
  await expect(page.getByRole('button', { name: 'Restaurar respaldo' })).toBeVisible()
  await safetyDownload(page, context, buffer, fileName)
  await expect.poll(async () => (await page.getByRole('region', { name: 'Vista previa del respaldo' }).count()) + (await page.getByRole('alert').count())).toBe(1)
  const rejected = await page.getByRole('alert').count()
  if (expectValid && rejected > 0) throw new Error(`A backup expected to validate was rejected: ${await page.getByRole('alert').allTextContents()}`)
}

async function assertFileRejected(page, context, buffer, name, message) {
  await fileForImport(page, context, buffer, name, false)
  await expect(page.getByRole('alert')).toContainText(message)
  await expect(page.getByRole('region', { name: 'Vista previa del respaldo' })).toHaveCount(0)
}

function screenshotPath(directory, stem) {
  mkdirSync(directory, { recursive: true })
  const escaped = stem.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
  const versions = readdirSync(directory).map((name) => new RegExp(`^${escaped}--v(\\d+)\\.png$`).exec(name)).filter(Boolean).map((match) => Number(match[1]))
  return path.join(directory, `${stem}--v${String(Math.max(0, ...versions) + 1).padStart(2, '0')}.png`)
}

async function verifyKeyboardFocus(page) {
  await page.evaluate(() => document.activeElement instanceof HTMLElement && document.activeElement.blur())
  let reached = false
  for (let i = 0; i < 50; i++) {
    await page.keyboard.press('Tab')
    reached = await page.getByRole('button', { name: 'Descargar respaldo' }).evaluate((button) => document.activeElement === button).catch(() => false)
    if (reached) break
  }
  assert.ok(reached, 'Tab navigation must reach the backup action')
  const outline = await page.getByRole('button', { name: 'Descargar respaldo' }).evaluate((button) => ({ width: getComputedStyle(button).outlineWidth, style: getComputedStyle(button).outlineStyle }))
  assert.notEqual(outline.style, 'none', 'Keyboard focus must have a visible outline')
  assert.notEqual(outline.width, '0px', 'Keyboard focus outline must have width')
}

async function saveVisualReview(page) {
  const cases = [
    { width: 1440, height: 1000, label: 'escritorio' },
    { width: 390, height: 844, label: '390px' },
    { width: 320, height: 720, label: '320px' },
  ]
  for (const theme of ['light', 'dark']) {
    const spanishTheme = theme === 'light' ? 'claro' : 'oscuro'
    for (const viewport of cases) {
      await page.setViewportSize({ width: viewport.width, height: viewport.height })
      await page.evaluate((value) => localStorage.setItem('panel-theme', value), theme)
      await page.goto(`${origin}/configuracion`)
      await expect(page.getByRole('heading', { name: 'Respaldos y restauración' })).toBeVisible()
      await verifyKeyboardFocus(page)
      await page.keyboard.press('Escape')
      await page.evaluate(() => { if (document.activeElement instanceof HTMLElement) document.activeElement.blur(); window.scrollTo(0, 0) })
      assert.equal(await page.locator('.sidebar.sidebar-open').count(), 0, 'Mobile navigation drawer must be closed for the visual capture')
      const layout = await page.evaluate(() => ({ width: document.documentElement.clientWidth, scroll: document.documentElement.scrollWidth }))
      assert.ok(layout.scroll <= layout.width + 1, `No horizontal page overflow at ${viewport.label}/${theme}: ${JSON.stringify(layout)}`)
      const folder = viewport.label === 'escritorio' ? 'escritorio' : 'movil'
      const stem = `2026-10-08--respaldos--${viewport.label}-${spanishTheme}`
      const destination = screenshotPath(path.join(appDir, 'capturas', folder), stem)
      await page.screenshot({ path: destination, fullPage: true, animations: 'disabled' })
      console.log(`CAPTURE ${destination}`)
    }
  }
}

async function main() {
  if (process.platform === 'win32') {
    const listeners = inspectPort4187()
    if (listeners.length > 0) {
      console.error(`Port 4187 is occupied; no existing process will be stopped: ${JSON.stringify(listeners)}`)
      throw new Error('Port 4187 is occupied. The runner did not start Vite.')
    }
    console.log('Port 4187 query succeeded with no matching listener; starting Vite is safe.')
  }
  viteProcess = spawn(process.execPath, [viteCli, '--host', '127.0.0.1', '--port', '4187', '--strictPort'], { cwd: appDir, stdio: ['ignore', 'pipe', 'pipe'], windowsHide: true })
  viteClosed = trackClose(viteProcess)
  viteProcess.stdout.on('data', (data) => { viteOutput += data.toString() })
  viteProcess.stderr.on('data', (data) => { viteOutput += data.toString() })
  console.log(`Started temporary PM.9C Vite PID ${viteProcess.pid} on ${origin}; it will close in finally.`)
  await waitForVite()

  profileDir = mkdtempSync(path.join(tmpdir(), 'banco-escolar-pm9c-'))
  console.log(`Created isolated Chromium profile ${profileDir}; it will be reused for close/reopen and removed after process verification.`)
  const executablePath = chromium.executablePath()
  assert.ok(existsSync(executablePath), `Playwright Chromium is missing: ${executablePath}`)
  browserContext = await chromium.launchPersistentContext(profileDir, { executablePath, headless: true, acceptDownloads: true, viewport: { width: 1440, height: 1000 } })
  const page = browserContext.pages()[0] ?? await browserContext.newPage()
  const firstPng = makePng(2, 2, [220, 20, 60, 255]); const secondPng = makePng(3, 1, [30, 90, 210, 255])
  await page.goto(`${origin}/configuracion`)
  await expect(page.getByRole('heading', { name: 'Respaldos y restauración' })).toBeVisible()

  // Export a representative full state containing an operation and a photo.
  await addAureos(page, 5)
  await setPhoto(page, 'pm9c-original.png', firstPng)
  await page.goto(`${origin}/configuracion`)
  const backupDownload = await Promise.all([browserContext.waitForEvent('download'), page.getByRole('button', { name: 'Descargar respaldo' }).click()]).then(([download]) => download)
  const backupPath = await backupDownload.path(); assert.ok(backupPath)
  const originalBackup = readFileSync(backupPath)
  const envelope = JSON.parse(originalBackup.toString('utf8'))
  assert.equal(envelope.format, 'banco-escolar-panel-backup'); assert.equal(envelope.version, 1); assert.equal(envelope.workspace, databaseName)
  assert.equal(envelope.collections.students.length, 6); assert.equal(envelope.collections.movements.length, 11); assert.equal(envelope.collections.studentImages.length, 1)
  assert.equal(envelope.collections.studentImages[0].blob.type, 'image/png')
  console.log('PASS: export contains versioned demo data, all stores and the original image Blob.')

  // Change current data after backup, including the image, so restore must replace this exact space.
  await addAureos(page, 2)
  await setPhoto(page, 'pm9c-current.png', secondPng)
  const currentBeforeImport = await readDb(page)
  assert.equal(currentBeforeImport.accounts.find((row) => row.student_id === 201).balance, 132)

  // Invalid JSON, format version, workspace, broken references and corrupt photo all leave native IDB unchanged.
  const snapshotBeforeInvalid = JSON.stringify(await readDb(page))
  await assertFileRejected(page, browserContext, Buffer.from('{invalid json'), 'invalid.json', 'JSON válido')
  const incompatible = structuredClone(envelope); incompatible.version = 99
  await assertFileRejected(page, browserContext, rechecksum(incompatible), 'future-version.json', 'no es compatible')
  const wrongSpace = structuredClone(envelope); wrongSpace.workspace = 'banco-escolar-panel-real'
  await assertFileRejected(page, browserContext, rechecksum(wrongSpace), 'wrong-space.json', 'pertenece a otro espacio')
  const brokenReference = structuredClone(envelope); brokenReference.collections.accounts[0].student_id = 9999
  await assertFileRejected(page, browserContext, rechecksum(brokenReference), 'broken-reference.json', 'referencia a un alumno inexistente')
  const invalidValue = structuredClone(envelope); invalidValue.collections.accounts[0].balance = 12.5
  await assertFileRejected(page, browserContext, rechecksum(invalidValue), 'invalid-value.json', 'el importe debe ser un entero')
  const missingCollection = structuredClone(envelope); delete missingCollection.collections.operationReceipts
  await assertFileRejected(page, browserContext, rechecksum(missingCollection), 'missing-collection.json', 'no incluye exactamente todas las colecciones')
  const invalidPhoto = structuredClone(envelope); invalidPhoto.collections.studentImages[0].blob = { type: 'image/png', size: 9, base64: 'bm90LWltYWdl' }; invalidPhoto.collections.studentImages[0].byteLength = 9
  await assertFileRejected(page, browserContext, rechecksum(invalidPhoto), 'invalid-photo.json', 'foto está dañada')
  assert.equal(JSON.stringify(await readDb(page)), snapshotBeforeInvalid, 'Rejected backup files must not change any IndexedDB store.')
  console.log('PASS: malformed JSON, incompatible version, wrong workspace, broken references, invalid values, missing collections and invalid photos rejected without writes.')

  // A controlled native IDB abort after clears/puts begins must roll back every store.
  await fileForImport(page, browserContext, originalBackup, 'abort-import.json')
  await expect(page.getByRole('region', { name: 'Vista previa del respaldo' })).toBeVisible()
  await expect(page.getByText('Fecha del respaldo')).toBeVisible()
  await expect(page.getByText('Demostración', { exact: true })).toBeVisible()
  await expect(page.getByText('11', { exact: true })).toBeVisible()
  await expect(page.getByText('Fotos', { exact: true })).toBeVisible()
  await page.getByLabel(/Comprobé que la descarga de seguridad/).check()
  await page.getByLabel(/Confirmo reemplazar todos los datos/).check()
  const beforeAbort = JSON.stringify(await readDb(page))
  await page.evaluate(() => {
    const prototype = IDBObjectStore.prototype; const originalPut = prototype.put
    Object.defineProperty(window, '__pm9cAbort', { configurable: true, value: { prototype, originalPut, armed: true } })
    prototype.put = function (...args) { const state = window.__pm9cAbort; if (state?.armed && this.name === 'movements') { state.armed = false; this.transaction.abort(); throw new DOMException('Controlled restore transaction abort', 'AbortError') }; return originalPut.apply(this, args) }
  })
  await page.getByRole('button', { name: 'Confirmar restauración' }).click()
  await expect(page.getByRole('alert')).toContainText('No se pudo completar la transacción local')
  await page.evaluate(() => { const state = window.__pm9cAbort; if (state) state.prototype.put = state.originalPut; delete window.__pm9cAbort })
  assert.equal(JSON.stringify(await readDb(page)), beforeAbort, 'Aborted restore must preserve every existing store and photo.')
  console.log('PASS: controlled native IndexedDB abort left the prior state and photo intact.')

  // Cancel is side-effect free.
  await fileForImport(page, browserContext, originalBackup, 'cancel-import.json')
  await page.getByRole('button', { name: 'Cancelar', exact: true }).click()
  assert.equal(JSON.stringify(await readDb(page)), beforeAbort)
  console.log('PASS: cancel closes the preview without modifying the database.')

  // A later native transaction invalidates the safety revision captured before preview.
  await fileForImport(page, browserContext, originalBackup, 'stale-import.json')
  const secondPage = await browserContext.newPage()
  await addAureos(secondPage, 1)
  await page.getByLabel(/Comprobé que la descarga de seguridad/).check()
  await page.getByLabel(/Confirmo reemplazar todos los datos/).check()
  await page.getByRole('button', { name: 'Confirmar restauración' }).click()
  await expect(page.getByRole('alert')).toContainText('Los datos cambiaron después de descargar el respaldo')
  await secondPage.close()
  const afterStale = await readDb(page)
  assert.equal(afterStale.accounts.find((row) => row.student_id === 201).balance, 133)
  assert.equal(afterStale.movements.length, 13)
  console.log('PASS: changed data after the safety download invalidates confirmation and preserves the newer transaction.')

  // Fresh protection download, then double dispatch the explicit confirmation.
  await fileForImport(page, browserContext, originalBackup, 'restore-import.json')
  await page.getByLabel(/Comprobé que la descarga de seguridad/).check()
  await page.getByLabel(/Confirmo reemplazar todos los datos/).check()
  await page.getByRole('button', { name: 'Confirmar restauración' }).evaluate((button) => { button.click(); button.click() })
  await expect(page.getByText('Respaldo restaurado. Las vistas y las fotos se actualizaron desde IndexedDB.')).toBeVisible()
  const restored = await readDb(page)
  assert.equal(restored.accounts.find((row) => row.student_id === 201).balance, 130)
  assert.equal(restored.movements.length, 11)
  assert.deepEqual(restored.schoolApplications, envelope.collections.schoolApplications)
  assert.deepEqual(restored.studentImages[0].blob, envelope.collections.studentImages[0].blob)
  assert.deepEqual(restored.operationReceipts, envelope.collections.operationReceipts)
  console.log('PASS: restore confirmation is single-apply; balances, movements, applications, receipts and photo match the export without recalculation.')

  await browserContext.close(); browserContext = undefined
  browserContext = await chromium.launchPersistentContext(profileDir, { executablePath, headless: true, acceptDownloads: true, viewport: { width: 1440, height: 1000 } })
  const reopened = browserContext.pages()[0] ?? await browserContext.newPage()
  await reopened.goto(`${origin}/alumnos/201`)
  await expect(reopened.getByText(/Vista previa local: pm9c-original\.png/)).toBeVisible()
  const afterReopen = await readDb(reopened)
  assert.equal(afterReopen.accounts.find((row) => row.student_id === 201).balance, 130)
  assert.equal(afterReopen.movements.length, 11)
  assert.deepEqual(afterReopen.studentImages[0].blob, envelope.collections.studentImages[0].blob)
  console.log('PASS: restored records and photo survived browser close/reopen with the same isolated profile.')

  await saveVisualReview(reopened)
  console.log('PASS: Settings screenshots captured in both themes at desktop, 390px and 320px; Tab focus outline verified at each viewport.')
}

let exitCode = 0
try { await main(); console.log('PASS: PM.9C native IndexedDB browser scenarios and visual review.') }
catch (error) { exitCode = 1; console.error(error?.stack ?? error); if (viteOutput) console.error(`Vite output:\n${viteOutput}`) }
finally {
  let profileSafeToRemove = false
  try {
    if (browserContext) { await browserContext.close(); browserContext = undefined }
    profileSafeToRemove = true
  } catch (error) { exitCode = 1; console.error(`Could not close Playwright persistent context: ${error?.stack ?? error}`) }
  try { await stopVite() } catch (error) { exitCode = 1; console.error(`Could not stop temporary Vite: ${error?.stack ?? error}`) }
  if (profileDir && profileSafeToRemove) {
    try { rmSync(profileDir, { recursive: true, force: true }); assert.equal(existsSync(profileDir), false); console.log('Removed and verified isolated PM.9C browser profile.') }
    catch (error) { exitCode = 1; console.error(`Could not remove temporary profile ${profileDir}: ${error?.stack ?? error}`) }
  } else if (profileDir) { exitCode = 1; console.error(`Retaining profile because browser closure is unconfirmed: ${profileDir}`) }
}
process.exitCode = exitCode
