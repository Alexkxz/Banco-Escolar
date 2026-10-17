import { after, before, test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync, spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import https from 'node:https';
import { once } from 'node:events';
import { createOtaHttpsServer } from '../ota_https_server.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../../..');
const TEMP = mkdtempSync(path.join(tmpdir(), 'banco-ota-tools-'));
const manifestTool = path.join(ROOT, 'tools/ota/ota_manifest.mjs');
let server;
let serverPort;

before(async () => {
  const powershell = process.env.POWERSHELL || 'powershell.exe';
  execFileSync(powershell, ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', path.join(HERE, 'create_ephemeral_tls_certificate.ps1'), '-OutputDirectory', TEMP], { stdio: 'ignore' });
  writeFileSync(path.join(TEMP, 'firmware.bin'), Buffer.from('development firmware fixture'));
  writeFileSync(path.join(TEMP, 'signer-fixture.bin'), Buffer.from('separate signer fixture'));
  writeFileSync(path.join(TEMP, 'manifest.json'), JSON.stringify({ test: true }));
  server = createOtaHttpsServer({
    bind: '127.0.0.1', hostname: 'localhost',
    pfx: path.join(TEMP, 'server.pfx'), certificate: path.join(TEMP, 'server.cer'),
    manifest: path.join(TEMP, 'manifest.json'), firmware: path.join(TEMP, 'firmware.bin'),
  });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  serverPort = server.address().port;
});

after(async () => {
  if (server?.listening) {
    server.close();
    await once(server, 'close');
  }
  rmSync(TEMP, { recursive: true, force: true });
});

function request(pathname, options = {}) {
  return new Promise((resolve, reject) => {
    const req = https.get({ hostname: 'localhost', family: 4, port: serverPort, path: pathname, ...options }, res => {
      const chunks = [];
      res.on('data', chunk => chunks.push(chunk));
      res.on('end', () => resolve({ status: res.statusCode, body: Buffer.concat(chunks).toString() }));
    });
    req.on('error', reject);
  });
}

test('dev key and manifest tools sign and verify exact firmware and contract', () => {
  const privateKey = path.join(TEMP, 'development-private.pem');
  const publicKey = path.join(TEMP, 'development-public.pem');
  const manifestPath = path.join(TEMP, 'signed-manifest.json');
  const firmwarePath = path.join(TEMP, 'signer-fixture.bin');
  execFileSync(process.execPath, [manifestTool, 'keygen', '--private-key', privateKey, '--public-key', publicKey], { stdio: 'ignore' });
  execFileSync(process.execPath, [manifestTool, 'sign', '--firmware', firmwarePath, '--version', '1.2.4', '--image-url', 'https://localhost/firmware.bin', '--data-schema', '1', '--private-key', privateKey, '--output', manifestPath], { stdio: 'ignore' });
  execFileSync(process.execPath, [manifestTool, 'verify', '--manifest', manifestPath, '--public-key', publicKey, '--firmware', firmwarePath], { stdio: 'ignore' });
  writeFileSync(firmwarePath, Buffer.from('tampered firmware fixture'));
  const tampered = spawnSync(process.execPath, [manifestTool, 'verify', '--manifest', manifestPath, '--public-key', publicKey, '--firmware', firmwarePath], { encoding: 'utf8' });
  assert.notEqual(tampered.status, 0);
});

test('HTTPS serves only the two read-only artifact routes', async () => {
  const pem = `-----BEGIN CERTIFICATE-----\n${readFileSync(path.join(TEMP, 'server.cer')).toString('base64').match(/.{1,64}/g).join('\n')}\n-----END CERTIFICATE-----\n`;
  const manifest = await request('/manifest.json', { ca: pem });
  assert.equal(manifest.status, 200);
  assert.equal(JSON.parse(manifest.body).test, true);
  const firmware = await request('/firmware.bin', { ca: pem });
  assert.equal(firmware.status, 200);
  assert.equal(firmware.body, 'development firmware fixture');
  assert.equal((await request('/activate', { ca: pem })).status, 404);
  const post = await new Promise((resolve, reject) => {
    const req = https.request({ hostname: 'localhost', family: 4, port: serverPort, path: '/firmware.bin', method: 'POST', ca: pem }, res => {
      res.resume(); res.on('end', () => resolve(res.statusCode));
    });
    req.on('error', reject); req.end('write attempt');
  });
  assert.equal(post, 405);
});

test('untrusted certificate, public bind and unavailable server fail closed', async () => {
  await assert.rejects(request('/manifest.json'), error =>
    ['CERT_AUTHORITY_INVALID', 'DEPTH_ZERO_SELF_SIGNED_CERT'].includes(error.code) || /self-signed certificate/i.test(error.message));
  assert.throws(() => createOtaHttpsServer({ bind: '0.0.0.0', hostname: 'localhost', cert: 'unused', key: 'unused', manifest: 'unused', firmware: 'unused' }), /loopback o IPv4 privada/);
  const closedPort = serverPort;
  server.close();
  await once(server, 'close');
  await assert.rejects(new Promise((resolve, reject) => {
    https.get({ hostname: 'localhost', family: 4, port: closedPort, path: '/manifest.json', rejectUnauthorized: false }, resolve).on('error', reject);
  }), error => error.code === 'ECONNREFUSED');
});
