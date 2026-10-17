import assert from 'node:assert/strict'
import { mkdtemp, mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import path from 'node:path'
import test from 'node:test'
import { request as httpRequest } from 'node:http'
import { fileURLToPath } from 'node:url'
import { HOST, PORT, SERVER_HEADER, SERVER_ID, startServer } from '../static-server.mjs'

const quietLogger = { info() {}, error() {} }

async function createFixture() {
  const root = await mkdtemp(path.join(tmpdir(), 'banco-escolar-panel-'))
  await mkdir(path.join(root, 'assets'))
  await writeFile(path.join(root, 'index.html'), '<!doctype html><title>Panel fixture</title><div id="root"></div>')
  await writeFile(path.join(root, 'assets', 'panel.css'), 'body { color: navy; }')
  await writeFile(path.join(root, 'assets', 'panel.js'), 'document.title = "Panel fixture";')
  return root
}

async function serveFixture(t) {
  const distDir = await createFixture()
  const instance = await startServer({ port: 0, distDir, logger: quietLogger })
  t.after(async () => {
    await instance.close()
    await rm(distDir, { recursive: true, force: true })
  })
  return instance
}

function get(url, options = {}) {
  return new Promise((resolve, reject) => {
    const request = httpRequest(url, { agent: false, ...options }, (response) => {
      const chunks = []
      response.on('data', (chunk) => chunks.push(chunk))
      response.on('end', () => resolve({
        status: response.statusCode,
        headers: response.headers,
        body: Buffer.concat(chunks).toString('utf8'),
      }))
    })
    request.on('error', reject)
    request.end()
  })
}

test('inicia correctamente en loopback y anuncia URL al quedar listo', async (t) => {
  const distDir = await createFixture()
  let ready
  const instance = await startServer({ port: 0, distDir, logger: quietLogger, onListening: (info) => { ready = info } })
  t.after(async () => { await instance.close(); await rm(distDir, { recursive: true, force: true }) })

  assert.equal(HOST, '127.0.0.1')
  assert.equal(PORT, 4173)
  assert.deepEqual(instance.server.address(), { address: '127.0.0.1', family: 'IPv4', port: instance.port })
  assert.equal(ready.url, instance.url)
  assert.equal(ready.port, instance.port)
  assert.equal((await get(instance.url)).status, 200)
})

test('usa el puerto de instalación 4173 al iniciar con configuración predeterminada', async (t) => {
  const distDir = await createFixture()
  let instance
  try {
    instance = await startServer({ distDir, logger: quietLogger })
  } catch (error) {
    await rm(distDir, { recursive: true, force: true })
    if (error.code === 'EADDRINUSE') {
      t.skip('El puerto 4173 ya está ocupado en esta computadora.')
      return
    }
    throw error
  }
  t.after(async () => { await instance.close(); await rm(distDir, { recursive: true, force: true }) })
  assert.equal(instance.port, 4173)
  assert.equal(instance.server.address().address, '127.0.0.1')
})

test('entrega HTML, CSS y JavaScript con MIME correcto', async (t) => {
  const instance = await serveFixture(t)
  const html = await get(instance.url)
  const css = await get(`${instance.url}assets/panel.css`)
  const js = await get(`${instance.url}assets/panel.js`)

  assert.equal(html.status, 200)
  assert.match(html.headers['content-type'], /^text\/html;/)
  assert.match(html.body, /Panel fixture/)
  assert.equal(css.status, 200)
  assert.match(css.headers['content-type'], /^text\/css;/)
  assert.match(css.body, /color: navy/)
  assert.equal(js.status, 200)
  assert.match(js.headers['content-type'], /^text\/javascript;/)
  assert.match(js.body, /document\.title/)
})

test('resuelve rutas internas de React Router hacia index.html', async (t) => {
  const instance = await serveFixture(t)
  for (const route of [
    'dashboard',
    'alumnos',
    'cuentas',
    'movimientos',
    'actividades',
    'cobros',
    'configuracion',
    'alumnos/201',
    'movimientos/15',
  ]) {
    const response = await get(`${instance.url}${route}`)
    assert.equal(response.status, 200, route)
    assert.match(response.headers['content-type'], /^text\/html;/)
    assert.match(response.body, /Panel fixture/)
  }
})

test('devuelve 404 para recursos inexistentes con extensión', async (t) => {
  const instance = await serveFixture(t)
  const response = await get(`${instance.url}assets/missing.js`)
  assert.equal(response.status, 404)
  assert.match(response.body, /No se encontró/)
})

test('no ofrece operaciones de escritura ni endpoints bancarios', async (t) => {
  const instance = await serveFixture(t)
  const response = await get(instance.url, { method: 'POST' })
  assert.equal(response.status, 405)
  assert.match(response.headers.allow, /GET, HEAD/)
})

test('rechaza traversal, separadores codificados y Host ajeno', async (t) => {
  const instance = await serveFixture(t)
  for (const requestPath of ['/%2e%2e/secret.txt', '/%2e%2e%2fsecret.txt', '/%5c%5cserver/share']) {
    const response = await get(instance.url, { path: requestPath })
    assert.ok(response.status === 400 || response.status === 403, `${requestPath}: ${response.status}`)
  }
  const hostAttack = await get(instance.url, { headers: { host: `attacker.example:${instance.port}` } })
  assert.equal(hostAttack.status, 403)
})

test('falla claramente si dist/index.html no existe', async () => {
  const root = await mkdtemp(path.join(tmpdir(), 'banco-escolar-empty-'))
  try {
    await assert.rejects(startServer({ port: 0, distDir: root, logger: quietLogger }), /no se encontró un build válido en dist/)
  } finally {
    await rm(root, { recursive: true, force: true })
  }
})

test('reporta puerto ocupado y no crea una segunda instancia', async (t) => {
  const distDir = await createFixture()
  const first = await startServer({ port: 0, distDir, logger: quietLogger })
  t.after(async () => { await first.close(); await rm(distDir, { recursive: true, force: true }) })

  await assert.rejects(startServer({ port: first.port, distDir, logger: quietLogger }), { code: 'EADDRINUSE' })
  assert.equal((await get(first.url)).status, 200)
})

test('trata el segundo inicio como puerto ocupado sin interrumpir el primero', async (t) => {
  const distDir = await createFixture()
  const original = await startServer({ port: 0, distDir, logger: quietLogger })
  t.after(async () => { await original.close(); await rm(distDir, { recursive: true, force: true }) })

  const secondAttempt = startServer({ port: original.port, distDir, logger: quietLogger })
  await assert.rejects(secondAttempt, (error) => error.code === 'EADDRINUSE')
  assert.equal((await get(original.url)).headers[SERVER_HEADER.toLowerCase()], SERVER_ID)
})

test('cierra ordenadamente y libera el puerto para reinicio', async (t) => {
  const distDir = await createFixture()
  const first = await startServer({ port: 0, distDir, logger: quietLogger })
  t.after(async () => { await rm(distDir, { recursive: true, force: true }) })
  const port = first.port

  await first.close()
  const restarted = await startServer({ port, distDir, logger: quietLogger })
  t.after(async () => restarted.close())
  assert.equal((await get(restarted.url)).status, 200)
})

test('no escucha en interfaces externas', async (t) => {
  const instance = await serveFixture(t)
  assert.equal(instance.host, '127.0.0.1')
  assert.equal(instance.server.address().address, '127.0.0.1')
  const response = await get(instance.url)
  assert.equal(response.headers[SERVER_HEADER.toLowerCase()], SERVER_ID)
})

test('sirve el build local existente si está disponible (artefacto, no build actual)', async (t) => {
  const distDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../dist')
  let index
  try {
    index = await readFile(path.join(distDir, 'index.html'), 'utf8')
  } catch {
    t.skip('No hay dist/index.html en el workspace.')
    return
  }

  const instance = await startServer({ port: 0, distDir, logger: quietLogger })
  t.after(async () => instance.close())
  const home = await get(instance.url)
  const route = await get(`${instance.url}dashboard`)
  assert.equal(home.status, 200)
  assert.equal(route.status, 200)
  assert.match(home.body, /Banco Escolar/)
  assert.match(route.body, /Banco Escolar/)

  const assets = [...index.matchAll(/(?:src|href)="([^"]+\.(?:js|css))"/g)].map((match) => match[1])
  assert.ok(assets.length >= 2, 'El HTML del build debe declarar JS y CSS.')
  for (const asset of assets) assert.equal((await get(new URL(asset, instance.url))).status, 200, asset)
})
