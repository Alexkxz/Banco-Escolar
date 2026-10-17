#!/usr/bin/env node
// Generates a TEST-ONLY P-256 key pair. Never use it for production trust.
// The private key must be written outside the repository and is never printed.
import { generateKeyPairSync } from 'node:crypto';
import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

const [privatePathArg, publicHeaderArg] = process.argv.slice(2);
if (!privatePathArg || !publicHeaderArg) {
  console.error('usage: node create_test_key.mjs <external-private-key.pem> <public-key-header.h>');
  process.exit(2);
}
const { privateKey, publicKey } = generateKeyPairSync('ec', {
  namedCurve: 'prime256v1',
  privateKeyEncoding: { type: 'pkcs8', format: 'pem' },
  publicKeyEncoding: { type: 'spki', format: 'pem' },
});
writeFileSync(resolve(privatePathArg), privateKey, { flag: 'wx' });
const header = '#pragma once\n' +
  '// TEST ONLY: public key paired with an ephemeral lab key stored outside the repository.\n' +
  `#define BANCO_OTA_LAB_PUBLIC_KEY_PEM ${JSON.stringify(publicKey)}\n`;
writeFileSync(resolve(publicHeaderArg), header, { flag: 'wx' });
console.log(`Created TEST-ONLY key pair. Public key header: ${resolve(publicHeaderArg)}; private key: ${resolve(privatePathArg)}`);
