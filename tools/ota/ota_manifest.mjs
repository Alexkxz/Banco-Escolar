#!/usr/bin/env node
import { createHash, createPrivateKey, createPublicKey, generateKeyPairSync, sign, verify } from 'node:crypto';
import { readFileSync, writeFileSync, realpathSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const MODEL = 'WAVESHARE_ESP32_S3_TOUCH_LCD_7_REV1_2';
const KEYS = ['model', 'version', 'size', 'sha256', 'image_url', 'data_schema', 'signature'];

function args(argv) {
  const [command, ...rest] = argv;
  const out = { command };
  for (let i = 0; i < rest.length; i += 2) {
    if (!rest[i]?.startsWith('--') || !rest[i + 1] || out[rest[i].slice(2)]) throw new Error('Argumentos inválidos');
    out[rest[i].slice(2)] = rest[i + 1];
  }
  return out;
}

function required(options, names) {
  for (const name of names) if (!options[name]) throw new Error(`Falta --${name}`);
}

function privateKeyOutsideRepo(filename) {
  const resolved = path.resolve(filename);
  const relative = path.relative(ROOT, resolved);
  if (relative === '' || (!relative.startsWith('..') && !path.isAbsolute(relative))) {
    throw new Error('La clave privada debe guardarse fuera del repositorio');
  }
  return resolved;
}

function validateManifest(manifest) {
  if (!manifest || typeof manifest !== 'object' || Array.isArray(manifest) ||
      Object.keys(manifest).sort().join(',') !== [...KEYS].sort().join(',')) throw new Error('El manifiesto debe contener exactamente los siete campos del contrato');
  if (manifest.model !== MODEL) throw new Error('Modelo no admitido');
  if (typeof manifest.version !== 'string' || !/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(manifest.version) ||
      manifest.version.split('.').some(part => BigInt(part) > 0xffffffffn)) throw new Error('Versión inválida');
  if (!Number.isSafeInteger(manifest.size) || manifest.size <= 0) throw new Error('Tamaño inválido');
  if (!Number.isInteger(manifest.data_schema) || manifest.data_schema < 1 || manifest.data_schema > 65535) throw new Error('Versión de esquema de datos inválida');
  if (typeof manifest.sha256 !== 'string' || !/^[0-9a-f]{64}$/.test(manifest.sha256)) throw new Error('SHA-256 inválido');
  let url;
  try { url = new URL(manifest.image_url); } catch { throw new Error('URL de imagen inválida'); }
  if (url.protocol !== 'https:' || url.username || url.password || url.hash || url.search || url.pathname !== '/firmware.bin') {
    throw new Error('La imagen requiere URL HTTPS terminada exactamente en /firmware.bin, sin credenciales, query ni fragmento');
  }
  if (typeof manifest.signature !== 'string' || !manifest.signature) throw new Error('Firma ausente');
}

function canonical(m) {
  return `${m.model}\n${m.version}\n${m.size}\n${m.sha256}\n${m.image_url}\n${m.data_schema}`;
}

function readJson(filename) {
  return JSON.parse(readFileSync(filename, 'utf8'));
}

function main() {
  const options = args(process.argv.slice(2));
  if (options.command === 'keygen') {
    required(options, ['private-key', 'public-key']);
    const privatePath = privateKeyOutsideRepo(options['private-key']);
    const publicPath = path.resolve(options['public-key']);
    if (privatePath === publicPath) throw new Error('Rutas de clave duplicadas');
    const passphrase = process.env.BANCO_OTA_DEV_SIGNING_KEY_PASSPHRASE;
    const privateKeyEncoding = { type: 'pkcs8', format: 'pem', ...(passphrase ? { cipher: 'aes-256-cbc', passphrase } : {}) };
    const { privateKey, publicKey } = generateKeyPairSync('ec', {
      namedCurve: 'prime256v1',
      privateKeyEncoding,
      publicKeyEncoding: { type: 'spki', format: 'pem' },
    });
    writeFileSync(privatePath, privateKey, { flag: 'wx', mode: 0o600 });
    writeFileSync(publicPath, publicKey, { flag: 'wx', mode: 0o644 });
    console.log('Par P-256 TEMPORAL DE DESARROLLO creado. Mantén la clave privada fuera del repositorio.');
    return;
  }
  if (options.command === 'sign') {
    required(options, ['firmware', 'version', 'image-url', 'data-schema', 'private-key', 'output']);
    const dataSchema = Number(options['data-schema']);
    if (!Number.isInteger(dataSchema) || dataSchema < 1 || dataSchema > 65535) throw new Error('Versión de esquema de datos inválida');
    const image = readFileSync(options.firmware);
    const manifest = {
      model: MODEL,
      version: options.version,
      size: image.length,
      sha256: createHash('sha256').update(image).digest('hex'),
      image_url: options['image-url'],
      data_schema: dataSchema,
      signature: 'pending',
    };
    const privatePem = readFileSync(options['private-key']);
    const passphrase = process.env.BANCO_OTA_DEV_SIGNING_KEY_PASSPHRASE;
    const key = createPrivateKey(passphrase ? { key: privatePem, passphrase } : privatePem);
    if (key.asymmetricKeyType !== 'ec' || key.asymmetricKeyDetails?.namedCurve !== 'prime256v1') {
      throw new Error('Solo se admiten claves privadas ECDSA P-256 de desarrollo');
    }
    manifest.signature = sign('sha256', Buffer.from(canonical(manifest)), { key, dsaEncoding: 'der' }).toString('base64');
    validateManifest(manifest);
    writeFileSync(options.output, `${JSON.stringify(manifest, null, 2)}\n`, { flag: 'wx' });
    console.log(`Manifiesto de desarrollo firmado: ${path.resolve(options.output)}`);
    return;
  }
  if (options.command === 'verify') {
    required(options, ['manifest', 'public-key', 'firmware']);
    const manifest = readJson(options.manifest);
    validateManifest(manifest);
    const image = readFileSync(options.firmware);
    if (image.length !== manifest.size || createHash('sha256').update(image).digest('hex') !== manifest.sha256) {
      throw new Error('La imagen no coincide con tamaño/SHA-256 del manifiesto');
    }
    const key = createPublicKey(readFileSync(options['public-key']));
    if (key.asymmetricKeyType !== 'ec' || key.asymmetricKeyDetails?.namedCurve !== 'prime256v1') {
      throw new Error('Solo se admiten claves públicas ECDSA P-256');
    }
    const valid = verify('sha256', Buffer.from(canonical(manifest)), key, Buffer.from(manifest.signature, 'base64'));
    if (!valid) throw new Error('Firma ECDSA P-256 inválida');
    console.log(`Manifiesto e imagen verificados para ${manifest.model} versión ${manifest.version}`);
    return;
  }
  throw new Error('Uso: ota_manifest.mjs keygen|sign|verify [opciones]');
}

try { main(); } catch (error) { console.error(`Error: ${error.message}`); process.exitCode = 1; }
