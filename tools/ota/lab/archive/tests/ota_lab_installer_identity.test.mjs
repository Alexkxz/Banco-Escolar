import { test } from 'node:test';
import assert from 'node:assert/strict';
import { generateKeyPairSync } from 'node:crypto';
import {
  assertSigningKeyMatchesHeader,
  createInstallerIdentityTrailer,
  verifyInstallerIdentityTrailer,
} from '../lab/seal_lab_installer.mjs';

function testKeyPair() {
  return generateKeyPairSync('ec', {
    namedCurve: 'prime256v1',
    privateKeyEncoding: { type: 'pkcs8', format: 'pem' },
    publicKeyEncoding: { type: 'spki', format: 'pem' },
  });
}

test('installer identity trailer signs the exact image bytes with real P-256 crypto', () => {
  const pair = testKeyPair();
  const image = Buffer.from('synthetic ESP app image bytes');
  const built = createInstallerIdentityTrailer(image, pair.privateKey);
  assert.equal(verifyInstallerIdentityTrailer(image, built.trailer, pair.publicKey), true);
  assert.equal(built.identity.image_size, image.length);
});

test('installer identity rejects modified, truncated, wrong-target and wrong-key data', () => {
  const pair = testKeyPair();
  const other = testKeyPair();
  const image = Buffer.from('synthetic approved installer bytes');
  const built = createInstallerIdentityTrailer(image, pair.privateKey);
  const tamperedImage = Buffer.from(image);
  tamperedImage[0] ^= 1;
  assert.equal(verifyInstallerIdentityTrailer(tamperedImage, built.trailer, pair.publicKey), false);
  assert.equal(verifyInstallerIdentityTrailer(image, built.trailer.subarray(0, -1), pair.publicKey), false);
  assert.equal(verifyInstallerIdentityTrailer(image, built.trailer, other.publicKey), false);

  const changed = Buffer.from(built.trailer);
  changed[12] ^= 1;
  assert.equal(verifyInstallerIdentityTrailer(image, changed, pair.publicKey), false);
});

test('installer identity builder rejects invalid key curves and oversize images', () => {
  const p384 = generateKeyPairSync('ec', {
    namedCurve: 'secp384r1',
    privateKeyEncoding: { type: 'pkcs8', format: 'pem' },
    publicKeyEncoding: { type: 'spki', format: 'pem' },
  });
  assert.throws(() => createInstallerIdentityTrailer(Buffer.from('image'), p384.privateKey), /P-256/);
  const p256 = testKeyPair();
  assert.throws(() => createInstallerIdentityTrailer(Buffer.alloc(0x640001), p256.privateKey), /excede ota_0/);
});

test('signing key must match the public key compiled into the installer', () => {
  const pair = testKeyPair();
  const publicHeader = `#define BANCO_OTA_LAB_PUBLIC_KEY_PEM ${JSON.stringify(pair.publicKey)}`;
  assert.doesNotThrow(() => assertSigningKeyMatchesHeader(pair.privateKey, publicHeader));
  const other = testKeyPair();
  assert.throws(() => assertSigningKeyMatchesHeader(other.privateKey, publicHeader), /no coincide/);
});
