// Creates a signed identity trailer for the isolated OTA-ARQ.2 installer.
// The signing key must remain outside the repository and is never printed.
import { createHash, createPrivateKey, createPublicKey, sign, verify } from 'node:crypto';
import { existsSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

export const INSTALLER_IDENTITY = Object.freeze({
  format: 'BANCO-OTA-LAB-INSTALLER-V1',
  model: 'WAVESHARE_ESP32_S3_TOUCH_LCD_7_REV1_2',
  version: 'OTA-ARQ.2',
  build_id: 'OTA-ARQ.2-INSTALLER',
});

export const IDENTITY_MAGIC = Buffer.from('BANCOID1', 'ascii');
export const MAX_APP_IMAGE_SIZE = 0x640000;

export function assertSigningKeyMatchesHeader(privateKeyPem, publicHeader) {
  const match = publicHeader.match(/#define BANCO_OTA_LAB_PUBLIC_KEY_PEM "((?:\\.|[^"\\])*)"/);
  if (!match) throw new Error('No se pudo leer la clave pública de laboratorio compilada');
  const publicPem = JSON.parse(`"${match[1]}"`);
  const derived = createPublicKey(createPrivateKey(privateKeyPem))
    .export({ type: 'spki', format: 'pem' }).toString();
  const expected = createPublicKey(publicPem)
    .export({ type: 'spki', format: 'pem' }).toString();
  if (derived !== expected) throw new Error('La clave de firma no coincide con la clave pública compilada');
}

export function canonicalInstallerIdentity(identity) {
  return [
    'BANCO-OTA-LAB-INSTALLER-V1',
    identity.model,
    identity.version,
    identity.build_id,
    String(identity.image_size),
    identity.sha256,
  ].join('\n');
}

export function createInstallerIdentityTrailer(image, privateKeyPem) {
  if (!Buffer.isBuffer(image) || image.length === 0 || image.length > MAX_APP_IMAGE_SIZE) {
    throw new Error('La imagen del instalador está vacía o excede ota_0');
  }
  const privateKey = createPrivateKey(privateKeyPem);
  const details = privateKey.asymmetricKeyDetails;
  if (privateKey.asymmetricKeyType !== 'ec' || details?.namedCurve !== 'prime256v1') {
    throw new Error('La clave de identidad del instalador debe ser ECDSA P-256');
  }

  const identity = {
    ...INSTALLER_IDENTITY,
    image_size: image.length,
    sha256: createHash('sha256').update(image).digest('hex'),
  };
  const signature = sign('sha256', Buffer.from(canonicalInstallerIdentity(identity)), {
    key: privateKey,
    dsaEncoding: 'der',
  }).toString('base64');
  const manifest = { ...identity, signature };
  const manifestBytes = Buffer.from(JSON.stringify(manifest), 'utf8');
  if (manifestBytes.length === 0 || manifestBytes.length > 1024) {
    throw new Error('El manifiesto de identidad excede el límite');
  }

  const header = Buffer.alloc(10);
  IDENTITY_MAGIC.copy(header, 0);
  header.writeUInt16LE(manifestBytes.length, 8);
  return { identity, manifest, manifestBytes, trailer: Buffer.concat([header, manifestBytes]) };
}

export function verifyInstallerIdentityTrailer(image, trailer, publicKeyPem) {
  if (!Buffer.isBuffer(image) || !Buffer.isBuffer(trailer) || trailer.length < 10 ||
      !trailer.subarray(0, 8).equals(IDENTITY_MAGIC)) return false;
  const manifestLength = trailer.readUInt16LE(8);
  if (manifestLength === 0 || manifestLength > 1024 || trailer.length !== 10 + manifestLength) return false;
  let manifest;
  try {
    manifest = JSON.parse(trailer.subarray(10).toString('utf8'));
  } catch {
    return false;
  }
  const expectedKeys = ['format', 'model', 'version', 'build_id', 'image_size', 'sha256', 'signature'];
  if (!manifest || Object.keys(manifest).sort().join(',') !== [...expectedKeys].sort().join(',') ||
      manifest.format !== INSTALLER_IDENTITY.format || manifest.model !== INSTALLER_IDENTITY.model ||
      manifest.version !== INSTALLER_IDENTITY.version || manifest.build_id !== INSTALLER_IDENTITY.build_id ||
      manifest.image_size !== image.length ||
      manifest.sha256 !== createHash('sha256').update(image).digest('hex') ||
      typeof manifest.signature !== 'string') return false;
  try {
    return verify('sha256', Buffer.from(canonicalInstallerIdentity(manifest)), publicKeyPem,
      Buffer.from(manifest.signature, 'base64'));
  } catch {
    return false;
  }
}

function main() {
  const [inputArg, outputArg, keyArg] = process.argv.slice(2);
  if (!inputArg || !outputArg || !keyArg) {
    throw new Error('Uso: node seal_lab_installer.mjs <firmware.bin> <firmware.lab-installer.bin> <clave-privada-lab.pem>');
  }
  const inputPath = resolve(inputArg);
  const outputPath = resolve(outputArg);
  const keyPath = resolve(keyArg);
  if (existsSync(outputPath)) throw new Error('El artefacto sellado ya existe; no se sobrescribe');
  const image = readFileSync(inputPath);
  const privateKeyPem = readFileSync(keyPath);
  const publicHeader = readFileSync(resolve(fileURLToPath(new URL('./ota_lab_test_public_key.h', import.meta.url))), 'utf8');
  assertSigningKeyMatchesHeader(privateKeyPem, publicHeader);
  const sealed = createInstallerIdentityTrailer(image, privateKeyPem);
  const output = Buffer.concat([image, sealed.trailer]);
  writeFileSync(outputPath, output, { flag: 'wx' });
  console.log(`Instalador sellado: ${outputPath}`);
  console.log(`Tamaño de imagen ESP: ${image.length}`);
  console.log(`Tamaño del artefacto sellado: ${output.length}`);
  console.log(`SHA-256 imagen ESP: ${sealed.identity.sha256}`);
  console.log(`SHA-256 artefacto sellado: ${createHash('sha256').update(output).digest('hex')}`);
}

if (process.argv[1] && resolve(process.argv[1]) === resolve(fileURLToPath(import.meta.url))) main();
