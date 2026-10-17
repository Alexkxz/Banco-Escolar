import { test } from 'node:test';
import assert from 'node:assert/strict';
import { generateKeyPairSync, sign, verify } from 'node:crypto';
import { readFileSync } from 'node:fs';

const source = readFileSync(new URL('../../../src/network/ota_manager.cpp', import.meta.url), 'utf8');
function functionBody(sourceText, signature) {
  const start = sourceText.indexOf(signature);
  assert.notEqual(start, -1, `missing function: ${signature}`);
  const open = sourceText.indexOf('{', start);
  let depth = 0;
  for (let i = open; i < sourceText.length; i++) {
    if (sourceText[i] === '{') depth++;
    if (sourceText[i] === '}' && --depth === 0) return sourceText.slice(open, i + 1);
  }
  assert.fail(`unterminated function: ${signature}`);
}
const canonical = 'WAVESHARE_ESP32_S3_TOUCH_LCD_7_REV1_2\n1.2.4\n4096\n' + 'a'.repeat(64) + '\nhttps://updates.example/fw.bin\n1';
const { privateKey, publicKey } = generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
const signatureDer = sign('sha256', Buffer.from(canonical), privateKey);
const signatureBase64 = signatureDer.toString('base64');

test('P-256 DER signature round-trips through Base64 and verifies', () => {
  assert.ok(signatureDer.length > 0 && signatureDer.length <= 72);
  assert.equal(Buffer.from(signatureBase64, 'base64').toString('hex'), signatureDer.toString('hex'));
  assert.equal(verify('sha256', Buffer.from(canonical), publicKey, Buffer.from(signatureBase64, 'base64')), true);
});

test('signature fails when signed manifest fields are changed', () => {
  assert.equal(verify('sha256', Buffer.from(canonical.replace('1.2.4', '1.2.5')), publicKey, signatureDer), false);
  assert.equal(verify('sha256', Buffer.from(canonical.replace('4096', '4097')), publicKey, signatureDer), false);
  assert.equal(verify('sha256', Buffer.from(canonical.replace('a'.repeat(64), 'b'.repeat(64))), publicKey, signatureDer), false);
  assert.equal(verify('sha256', Buffer.from(canonical.replace('fw.bin', 'evil.bin')), publicKey, signatureDer), false);
  assert.equal(verify('sha256', Buffer.from(canonical.replace(/\n1$/, '\n2')), publicKey, signatureDer), false);
});

test('signature fails for a different public key, a different curve, and corrupted DER', () => {
  const otherP256 = generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
  const p384 = generateKeyPairSync('ec', { namedCurve: 'secp384r1' });
  assert.equal(verify('sha256', Buffer.from(canonical), otherP256.publicKey, signatureDer), false);
  assert.equal(verify('sha256', Buffer.from(canonical), p384.publicKey, signatureDer), false);
  const corrupted = Buffer.from(signatureDer);
  corrupted[corrupted.length - 1] ^= 0x01;
  assert.equal(verify('sha256', Buffer.from(canonical), publicKey, corrupted), false);
});

test('firmware is wired to real mbedTLS P-256 verification and cannot skip it', () => {
  const verifier = functionBody(source, 'bool signature_valid(');
  assert.match(verifier, /mbedtls_pk_parse_public_key/);
  assert.match(verifier, /MBEDTLS_ECP_DP_SECP256R1/);
  assert.match(verifier, /mbedtls_pk_verify\(&key, MBEDTLS_MD_SHA256/);
  assert.match(source, /!signature_valid\(canonical/);
  assert.doesNotMatch(verifier, /return\s+true/);
});
