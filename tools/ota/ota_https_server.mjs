#!/usr/bin/env node
import { createServer } from 'node:https';
import { readFileSync, statSync, createReadStream } from 'node:fs';
import { X509Certificate } from 'node:crypto';
import { isIPv4 } from 'node:net';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

function parseArgs(argv) {
  const result = {};
  for (let i = 0; i < argv.length; i += 2) {
    const key = argv[i];
    if (!key?.startsWith('--') || !argv[i + 1] || result[key.slice(2)]) throw new Error('Argumentos inválidos');
    result[key.slice(2)] = argv[i + 1];
  }
  return result;
}

function localAddress(value) {
  if (value === 'localhost') return true;
  if (!isIPv4(value)) return false;
  const octets = value.split('.').map(Number);
  return octets[0] === 10 || octets[0] === 127 ||
    (octets[0] === 172 && octets[1] >= 16 && octets[1] <= 31) ||
    (octets[0] === 192 && octets[1] === 168) ||
    (octets[0] === 169 && octets[1] === 254);
}

function send(res, status, message) {
  const body = Buffer.from(`${message}\n`);
  res.writeHead(status, { 'Content-Type': 'text/plain; charset=utf-8', 'Content-Length': body.length, 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' });
  res.end(body);
}

export function createOtaHttpsServer({ bind, hostname, cert, key, pfx, certificate: certificatePath, manifest, firmware }) {
  if (!localAddress(bind)) throw new Error('La dirección de enlace debe ser loopback o IPv4 privada; no se permite 0.0.0.0 ni una IP pública');
  if (!hostname || hostname === '*' || hostname.includes('://')) throw new Error('--hostname debe ser el DNS/IP incluido en el certificado');
  const certPem = cert ? readFileSync(cert) : null;
  const keyPem = key ? readFileSync(key) : null;
  const pfxBytes = pfx ? readFileSync(pfx) : null;
  if (!(pfxBytes || (certPem && keyPem))) throw new Error('Se requieren --cert y --key PEM, o un PFX de prueba');
  const certificate = new X509Certificate(certPem || readFileSync(certificatePath));
  const now = Date.now();
  if (Date.parse(certificate.validFrom) > now || Date.parse(certificate.validTo) <= now) {
    throw new Error('El certificado TLS no está vigente');
  }
  const identity = isIPv4(hostname) ? certificate.checkIP(hostname) : certificate.checkHost(hostname);
  if (!identity) throw new Error('El certificado no contiene SAN coincidente con --hostname');
  const manifestPath = path.resolve(manifest);
  const firmwarePath = path.resolve(firmware);
  if (!statSync(manifestPath).isFile() || !statSync(firmwarePath).isFile()) throw new Error('Manifiesto o firmware inexistente');

  return createServer({ ...(pfxBytes ? { pfx: pfxBytes } : { cert: certPem, key: keyPem, passphrase: process.env.BANCO_OTA_TLS_KEY_PASSPHRASE }), minVersion: 'TLSv1.2' }, (req, res) => {
    if (req.method !== 'GET' && req.method !== 'HEAD') {
      res.setHeader('Allow', 'GET, HEAD');
      return send(res, 405, 'Method not allowed');
    }
    const target = req.url;
    const file = target === '/manifest.json' ? manifestPath : target === '/firmware.bin' ? firmwarePath : null;
    if (!file) return send(res, 404, 'Not found');
    let size;
    try { size = statSync(file).size; } catch { return send(res, 404, 'Not found'); }
    res.writeHead(200, {
      'Content-Type': target === '/manifest.json' ? 'application/json; charset=utf-8' : 'application/octet-stream',
      'Content-Length': size,
      'Cache-Control': 'no-store',
      'X-Content-Type-Options': 'nosniff',
    });
    if (req.method === 'HEAD') return res.end();
    const stream = createReadStream(file);
    stream.on('error', () => res.destroy());
    stream.pipe(res);
  });
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    const options = parseArgs(process.argv.slice(2));
    for (const name of ['bind', 'hostname', 'cert', 'key', 'manifest', 'firmware']) {
      if (!options[name]) throw new Error(`Falta --${name}`);
    }
    const port = options.port === undefined ? 8443 : Number(options.port);
    if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('--port debe ser entero entre 1 y 65535');
    const server = createOtaHttpsServer(options);
    server.listen(port, options.bind, () => {
      const address = server.address();
      console.log(`Servidor OTA de desarrollo HTTPS escuchando en ${address.address}:${address.port}`);
      console.log('Solo GET/HEAD /manifest.json y /firmware.bin; sin escritura ni endpoint de activación. Ctrl+C para cerrar.');
    });
    const close = () => server.close(() => process.exit(0));
    process.once('SIGINT', close);
    process.once('SIGTERM', close);
  } catch (error) {
    console.error(`Error: ${error.message}`);
    process.exitCode = 1;
  }
}
