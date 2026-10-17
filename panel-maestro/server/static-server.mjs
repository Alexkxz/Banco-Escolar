import { spawn } from 'node:child_process'
import { createServer } from 'node:http'
import { appendFileSync, createReadStream, mkdirSync, realpathSync, statSync } from 'node:fs'
import { createInterface } from 'node:readline'
import { homedir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { pipeline } from 'node:stream/promises'

export const HOST = '127.0.0.1'
export const PORT = 4173
export const SERVER_HEADER = 'X-Banco-Escolar-Server'
export const SERVER_ID = 'panel-static-v1'

const DEFAULT_DIST_DIR = fileURLToPath(new URL('../dist/', import.meta.url))
const MIME_TYPES = new Map([
  ['.css', 'text/css; charset=utf-8'],
  ['.gif', 'image/gif'],
  ['.html', 'text/html; charset=utf-8'],
  ['.ico', 'image/x-icon'],
  ['.jpeg', 'image/jpeg'],
  ['.jpg', 'image/jpeg'],
  ['.js', 'text/javascript; charset=utf-8'],
  ['.json', 'application/json; charset=utf-8'],
  ['.map', 'application/json; charset=utf-8'],
  ['.png', 'image/png'],
  ['.svg', 'image/svg+xml'],
  ['.txt', 'text/plain; charset=utf-8'],
  ['.webp', 'image/webp'],
  ['.woff', 'font/woff'],
  ['.woff2', 'font/woff2'],
])

function isInside(root, candidate) {
  const relative = path.relative(root, candidate)
  return relative === '' || (!relative.startsWith(`..${path.sep}`) && relative !== '..' && !path.isAbsolute(relative))
}

function responseHeaders(contentType) {
  return {
    'Cache-Control': 'no-cache',
    'Content-Type': contentType,
    'X-Content-Type-Options': 'nosniff',
    'Referrer-Policy': 'no-referrer',
    [SERVER_HEADER]: SERVER_ID,
  }
}

function sendText(response, status, message, port) {
  const body = `${message}\n`
  response.writeHead(status, {
    ...responseHeaders('text/plain; charset=utf-8'),
    ...(status === 405 ? { Allow: 'GET, HEAD' } : {}),
    'Content-Length': Buffer.byteLength(body),
    [SERVER_HEADER]: SERVER_ID,
    'X-Panel-Port': String(port),
  })
  response.end(body)
}

function decodeRequestPath(requestTarget) {
  if (typeof requestTarget !== 'string' || !requestTarget.startsWith('/') || requestTarget.startsWith('//')) {
    const error = new Error('La ruta HTTP no es válida.')
    error.statusCode = 400
    throw error
  }

  const queryStart = requestTarget.indexOf('?')
  const rawPath = queryStart < 0 ? requestTarget : requestTarget.slice(0, queryStart)
  let pathname
  try {
    pathname = decodeURIComponent(rawPath)
  } catch {
    const error = new Error('La ruta contiene codificación inválida.')
    error.statusCode = 400
    throw error
  }

  if (pathname.includes('\\') || pathname.includes('\0') || pathname.includes(':')) {
    const error = new Error('La ruta solicitada no está permitida.')
    error.statusCode = 403
    throw error
  }
  if (pathname.split('/').some((part) => part === '..' || part === '.')) {
    const error = new Error('La ruta solicitada está fuera del contenido permitido.')
    error.statusCode = 403
    throw error
  }
  return pathname
}

async function findFile(root, pathname) {
  const requested = path.resolve(root, `.${pathname}`)
  if (!isInside(root, requested)) return { status: 403 }

  let resolved = requested
  try {
    resolved = realpathSync(requested)
  } catch (error) {
    if (error?.code !== 'ENOENT' && error?.code !== 'ENOTDIR') throw error
    if (path.extname(pathname)) return { status: 404 }
    resolved = path.join(root, 'index.html')
  }

  if (!isInside(root, resolved)) return { status: 403 }
  let info
  try {
    info = statSync(resolved)
  } catch (error) {
    if (error?.code === 'ENOENT' || error?.code === 'ENOTDIR') return { status: 404 }
    throw error
  }

  if (info.isDirectory()) {
    const directoryIndex = path.join(resolved, 'index.html')
    try {
      resolved = realpathSync(directoryIndex)
      info = statSync(resolved)
    } catch (error) {
      if (error?.code === 'ENOENT' || error?.code === 'ENOTDIR') {
        if (!path.extname(pathname)) return findFile(root, '/index.html')
        return { status: 404 }
      }
      throw error
    }
    if (!isInside(root, resolved)) return { status: 403 }
  }

  if (!info.isFile()) return { status: 404 }
  return { filePath: resolved, size: info.size }
}

function allowedHost(request, port) {
  return request.headers.host === `${HOST}:${port}`
}

export async function startServer({ port = PORT, distDir = DEFAULT_DIST_DIR, logger = console, onListening = () => {} } = {}) {
  if (!Number.isInteger(port) || port < 0 || port > 65535) throw new Error('El puerto configurado no es válido.')

  let root
  try {
    root = realpathSync(distDir)
    if (!statSync(root).isDirectory() || !statSync(path.join(root, 'index.html')).isFile()) {
      throw new Error('Falta dist/index.html.')
    }
  } catch (error) {
    throw new Error(`No se puede iniciar el Panel: no se encontró un build válido en dist/. ${error.message}`)
  }

  const server = createServer((request, response) => {
    void (async () => {
      const boundPort = server.address()?.port ?? port
      if (!allowedHost(request, boundPort)) {
        sendText(response, 403, 'Host local no permitido.', boundPort)
        return
      }
      if (request.method !== 'GET' && request.method !== 'HEAD') {
        request.resume()
        response.shouldKeepAlive = false
        sendText(response, 405, 'Método no permitido.', boundPort)
        return
      }

      let pathname
      try {
        pathname = decodeRequestPath(request.url)
      } catch (error) {
        sendText(response, error.statusCode ?? 400, error.message, boundPort)
        return
      }

      let found
      try {
        found = await findFile(root, pathname)
      } catch (error) {
        logger.error?.(`[HTTP] No se pudo resolver el recurso solicitado: ${error.message}`)
        sendText(response, 500, 'No se pudo leer el recurso solicitado.', boundPort)
        return
      }
      if (found.status === 403) {
        sendText(response, 403, 'La ruta solicitada está fuera del contenido permitido.', boundPort)
        return
      }
      if (found.status === 404) {
        sendText(response, 404, 'No se encontró el recurso solicitado.', boundPort)
        return
      }

      const contentType = MIME_TYPES.get(path.extname(found.filePath).toLowerCase())
      if (!contentType) {
        sendText(response, 415, 'Tipo de archivo no admitido.', boundPort)
        return
      }

      response.writeHead(200, {
        ...responseHeaders(contentType),
        'Content-Length': found.size,
      })
      if (request.method === 'HEAD') {
        response.end()
        return
      }
      try {
        await pipeline(createReadStream(found.filePath), response)
      } catch (error) {
        if (!response.destroyed) {
          logger.error?.(`[HTTP] Falló la transmisión de un recurso: ${error.message}`)
          response.destroy()
        }
      }
    })().catch((error) => {
      logger.error?.(`[HTTP] Error inesperado: ${error.message}`)
      if (!response.headersSent) sendText(response, 500, 'Error interno del servidor local.', server.address()?.port ?? port)
      else response.destroy()
    })
  })

  server.keepAliveTimeout = 2_000
  server.headersTimeout = 10_000
  server.requestTimeout = 15_000

  await new Promise((resolve, reject) => {
    const onError = (error) => reject(error)
    server.once('error', onError)
    server.listen(port, HOST, () => {
      server.off('error', onError)
      resolve()
    })
  })

  const actualPort = server.address().port
  server.on('error', (error) => logger.error?.(`[HTTP] Error del servidor: ${error.message}`))
  const url = `http://${HOST}:${actualPort}/`
  try {
    onListening({ host: HOST, port: actualPort, url })
  } catch (error) {
    await new Promise((resolve) => server.close(resolve))
    throw error
  }
  return { server, host: HOST, port: actualPort, url, close: () => new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve())) }
}

function supportedNodeVersion(version) {
  const [major, minor] = version.split('.').map(Number)
  return (major === 20 && minor >= 19) || (major >= 22 && (major !== 22 || minor >= 12))
}

function createFileLogger() {
  const base = process.env.LOCALAPPDATA || path.join(homedir(), 'AppData', 'Local')
  const logDirectory = path.join(base, 'BancoEscolar', 'logs')
  mkdirSync(logDirectory, { recursive: true })
  const logPath = path.join(logDirectory, 'panel-server.log')
  const write = (level, message) => {
    const line = `[${new Date().toISOString()}] [${level}] ${message}`
    console[level === 'error' ? 'error' : 'log'](line)
    try {
      appendFileSync(logPath, `${line}\r\n`, 'utf8')
    } catch (error) {
      console.error(`No se pudo guardar el registro en ${logPath}: ${error.message}`)
    }
  }
  return { info: (message) => write('info', message), error: (message) => write('error', message), logPath }
}

function openWindowsBrowser(url, logger) {
  if (process.platform !== 'win32') {
    logger.error('La apertura automática del navegador solo está disponible en Windows.')
    return
  }
  const browser = spawn('rundll32.exe', ['url.dll,FileProtocolHandler', url], {
    detached: true,
    stdio: 'ignore',
    windowsHide: true,
  })
  browser.once('error', (error) => logger.error(`El servidor inició, pero no se pudo abrir el navegador: ${error.message}`))
  browser.unref()
}

let cliLogger = console

async function runCli() {
  const logger = createFileLogger()
  cliLogger = logger
  const version = process.versions.node
  if (!supportedNodeVersion(version)) {
    throw new Error(`La versión de Node.js ${version} no es compatible. Se requiere Node.js 20.19+ o 22.12+.`)
  }

  let instance
  try {
    instance = await startServer({
      logger,
      onListening: ({ url }) => {
        logger.info(`Panel disponible en ${url}`)
        if (process.argv.includes('--open')) openWindowsBrowser(url, logger)
        logger.info('Para cerrar Banco Escolar, escribe "salir" y presiona Enter.')
      },
    })
  } catch (error) {
    if (error.code === 'EADDRINUSE') {
      throw new Error(`El puerto ${PORT} está ocupado. Puede que el Panel ya esté abierto. No se inició otra instancia; escribe "salir" y presiona Enter en su ventana para detenerlo.`)
    }
    throw error
  }

  let stopping = false
  let terminalInput
  const stop = (signal) => {
    if (stopping) return
    stopping = true
    terminalInput?.close()
    process.stdin.pause()
    process.stdin.destroy()
    logger.info(`Cierre solicitado (${signal}); esperando terminar las solicitudes activas.`)
    const forceClose = setTimeout(() => instance.server.closeAllConnections(), 5_000)
    forceClose.unref()
    instance.close().then(() => {
      clearTimeout(forceClose)
      logger.info('Servidor detenido correctamente.')
      process.exitCode = 0
    }).catch((error) => {
      clearTimeout(forceClose)
      logger.error(`No se pudo cerrar limpiamente el servidor: ${error.message}`)
      process.exitCode = 1
    })
  }
  process.on('SIGINT', () => stop('SIGINT'))
  process.on('SIGTERM', () => stop('SIGTERM'))
  if (process.platform === 'win32') process.on('SIGBREAK', () => stop('SIGBREAK'))

  terminalInput = createInterface({ input: process.stdin, output: process.stdout })
  terminalInput.on('line', (line) => {
    if (line.trim().toLowerCase() === 'salir') {
      stop('comando salir')
    } else if (line.trim()) {
      logger.info('Comando no reconocido. Escribe "salir" y presiona Enter para detener el Panel.')
    }
  })
  terminalInput.on('close', () => {
    if (!stopping) stop('entrada cerrada')
  })
}

const isMain = process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)
if (isMain) {
  runCli().catch((error) => {
    cliLogger.error?.(`[Error] ${error.message}`)
    process.exitCode = 1
  })
}
