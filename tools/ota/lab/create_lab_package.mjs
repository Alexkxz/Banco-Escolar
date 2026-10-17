#!/usr/bin/env node
// TEST-ONLY package builder. The input private key must be a disposable lab key.
import { createPrivateKey, createHash, sign, createPublicKey, verify } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export const LAB_MODEL = 'WAVESHARE_ESP32_S3_TOUCH_LCD_7_REV1_2';
export const OTA1 = Object.freeze({ name: 'ota_1', offset: 0x650000, slot_size: 0x640000 });
export const LAB_BUILD_ID = 'OTA-ARQ.2-CANDIDATE';
export const LAB_VERSION = '1.2.4-lab';

export function canonicalManifest(m) {
  return `BANCO-OTA-LAB-MANIFEST-V1\n${m.model}\n${m.version}\n${m.target}\n` +
    `${m.offset.toString(16).padStart(8, '0')}\n${m.slot_size.toString(16).padStart(8, '0')}\n` +
    `${m.size}\n${m.sha256}\n${m.build_id}\n${m.data_schema}\n${String(m.rollback_compatible)}`;
}

export function createSignedPackage(image, privateKeyPem, version = LAB_VERSION) {
  const key = createPrivateKey(privateKeyPem);
  const details = key.asymmetricKeyDetails;
  if (key.asymmetricKeyType !== 'ec' || details?.namedCurve !== 'prime256v1') {
    throw new Error('La clave debe ser ECDSA P-256 (solo laboratorio)');
  }
  if (!Buffer.isBuffer(image) || image.length === 0 || image.length > OTA1.slot_size) {
    throw new Error('Tamaño de imagen inválido para ota_1');
  }
  const manifest = {
    model: LAB_MODEL,
    version,
    target: OTA1.name,
    offset: OTA1.offset,
    slot_size: OTA1.slot_size,
    size: image.length,
    sha256: createHash('sha256').update(image).digest('hex'),
    build_id: LAB_BUILD_ID,
    data_schema: 1,
    rollback_compatible: true,
  };
  manifest.signature = sign('sha256', Buffer.from(canonicalManifest(manifest), 'utf8'), key).toString('base64');
  const manifestBytes = Buffer.from(JSON.stringify(manifest), 'utf8');
  if (manifestBytes.length > 2048) throw new Error('Manifiesto demasiado grande');
  const prefix = Buffer.alloc(2);
  prefix.writeUInt16LE(manifestBytes.length);
  const packageBytes = Buffer.concat([prefix, manifestBytes, image]);
  return { manifest, manifestBytes, packageBytes };
}

export function verifySignedManifest(manifest, publicKeyPem) {
  const key = createPublicKey(publicKeyPem);
  return verify('sha256', Buffer.from(canonicalManifest(manifest), 'utf8'), key,
    Buffer.from(manifest.signature, 'base64'));
}

function arg(name) {
  const i = process.argv.indexOf(name);
  return i >= 0 ? process.argv[i + 1] : undefined;
}

if (process.argv[1] && resolve(process.argv[1]) === resolve(fileURLToPath(import.meta.url))) {
  const imagePath = arg('--image');
  const keyPath = arg('--test-private-key');
  const packagePath = arg('--output');
  const manifestPath = arg('--manifest');
  const version = arg('--version') ?? LAB_VERSION;
  if (!imagePath || !keyPath || !packagePath || !manifestPath) {
    console.error('usage: node create_lab_package.mjs --image <bin> --test-private-key <TEST-ONLY-key.pem> --version <version> --output <package> --manifest <manifest.json>');
    process.exit(2);
  }
  try {
    const image = readFileSync(imagePath);
    const privateKey = readFileSync(keyPath, 'utf8');
    const result = createSignedPackage(image, privateKey, version);
    const publicKey = createPublicKey(createPrivateKey(privateKey));
    if (!verify('sha256', Buffer.from(canonicalManifest(result.manifest), 'utf8'), publicKey,
        Buffer.from(result.manifest.signature, 'base64'))) throw new Error('autoverificación de firma falló');
    writeFileSync(resolve(packagePath), result.packageBytes, { flag: 'wx' });
    writeFileSync(resolve(manifestPath), `${JSON.stringify(result.manifest, null, 2)}\n`, { flag: 'wx' });
    console.log(`Paquete TEST-ONLY: ${resolve(packagePath)}`);
    console.log(`Manifiesto: ${resolve(manifestPath)}`);
    console.log(`Tamaño imagen: ${image.length}`);
    console.log(`SHA-256 imagen: ${result.manifest.sha256}`);
  } catch (error) {
    console.error(`RECHAZADO: ${error.message}`);
    process.exit(1);
  }
}
