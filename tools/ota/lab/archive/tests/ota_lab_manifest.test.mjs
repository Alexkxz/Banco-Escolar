import { test } from 'node:test';
import assert from 'node:assert/strict';
import { generateKeyPairSync, verify, createPublicKey } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { createSignedPackage, canonicalManifest, verifySignedManifest, OTA1 } from '../lab/create_lab_package.mjs';

const firmware = Buffer.from('ESP32-S3 OTA lab candidate bytes for deterministic crypto tests');

test('P-256 signed manifest and framed package verify with real Node crypto', () => {
  const pair = generateKeyPairSync('ec', {
    namedCurve: 'prime256v1',
    privateKeyEncoding: { type: 'pkcs8', format: 'pem' },
    publicKeyEncoding: { type: 'spki', format: 'pem' },
  });
  const built = createSignedPackage(firmware, pair.privateKey);
  assert.equal(verifySignedManifest(built.manifest, pair.publicKey), true);
  assert.equal(verify('sha256', Buffer.from(canonicalManifest(built.manifest)),
    createPublicKey(pair.publicKey), Buffer.from(built.manifest.signature, 'base64')), true);
  const manifestLength = built.packageBytes.readUInt16LE(0);
  assert.equal(built.packageBytes.subarray(2, 2 + manifestLength).toString(), built.manifestBytes.toString());
  assert.deepEqual(built.packageBytes.subarray(2 + manifestLength), firmware);
  assert.equal(built.manifest.target, 'ota_1');
  assert.equal(built.manifest.offset, OTA1.offset);
});

test('manifest signature rejects changed version, image hash, size, slot or compatibility', () => {
  const pair = generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
  const built = createSignedPackage(firmware, pair.privateKey.export({ type: 'pkcs8', format: 'pem' }));
  for (const [key, value] of Object.entries({ version: '1.2.5-lab', sha256: '0'.repeat(64), size: firmware.length + 1,
      offset: 0x10000, slot_size: 0x1000, rollback_compatible: false })) {
    const changed = { ...built.manifest, [key]: value };
    assert.equal(verifySignedManifest(changed, pair.publicKey.export({ type: 'spki', format: 'pem' })), false, key);
  }
});

test('builder rejects non-P-256 keys and oversize candidates', () => {
  const p384 = generateKeyPairSync('ec', { namedCurve: 'secp384r1' });
  assert.throws(() => createSignedPackage(firmware, p384.privateKey.export({ type: 'pkcs8', format: 'pem' })), /P-256/);
  const p256 = generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
  assert.throws(() => createSignedPackage(Buffer.alloc(OTA1.slot_size + 1), p256.privateKey.export({ type: 'pkcs8', format: 'pem' })), /Tamaño/);
});

test('installer embeds a clearly labeled public test key only', () => {
  const header = readFileSync(new URL('../lab/ota_lab_test_public_key.h', import.meta.url), 'utf8');
  assert.match(header, /TEST ONLY/);
  assert.match(header, /BEGIN PUBLIC KEY/);
  assert.doesNotMatch(header, /BEGIN PRIVATE KEY/);
});
