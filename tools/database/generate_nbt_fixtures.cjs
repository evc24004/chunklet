#!/usr/bin/env node
// Usage: node tools/database/generate_nbt_fixtures.cjs PRISMARINE_NBT_MODULE OUTPUT_DIR
// Generates independent little-endian NBT oracle fixtures (prismarine-nbt 2.8.0).
// OUTPUT_DIR contains generated test data only; no world database is opened.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
if (process.argv.length !== 4) throw new Error('expected NBT module path and output directory');
const modulePath = path.resolve(process.argv[2]);
assert.equal(require(path.join(modulePath, 'package.json')).version, '2.8.0');
const nbt = require(modulePath);
const out = path.resolve(process.argv[3]);
fs.mkdirSync(out, { recursive: true });
const tags = [
  { type: 'compound', name: '', value: {
    name: { type: 'string', value: 'minecraft:oak_log' },
    states: { type: 'compound', value: { pillar_axis: { type: 'string', value: 'y' } } },
    version: { type: 'int', value: 18168865 },
  } },
  { type: 'compound', name: '', value: {
    name: { type: 'string', value: 'minecraft:water' },
    states: { type: 'compound', value: { liquid_depth: { type: 'int', value: 0 } } },
    version: { type: 'int', value: 18168865 },
  } },
];
const palettes = tags.map(tag => {
  const bytes = nbt.writeUncompressed(tag, 'little');
  assert.deepEqual(nbt.parseUncompressed(bytes, 'little'), tag);
  return bytes;
});
// 3-bit words have two unused padding bits and a partially used last word.
const words = Buffer.alloc(Math.ceil(4096 / 10) * 4);
for (let i = 0; i < 4096; ++i) {
  const offset = Math.floor(i / 10) * 4;
  words.writeUInt32LE(words.readUInt32LE(offset) | ((i % 2) << ((i % 10) * 3)), offset);
}
const count = Buffer.alloc(4); count.writeUInt32LE(2);
const packed = Buffer.concat([Buffer.from([6]), words, count, ...palettes]);
for (const version of [8, 9]) {
  const header = Buffer.from(version === 9 ? [9, 1, 0] : [8, 1]);
  fs.writeFileSync(path.join(out, `oracle-v${version}.bin`), Buffer.concat([header, packed]));
}
fs.writeFileSync(path.join(out, 'oracle-multilayer.bin'), Buffer.concat([
  Buffer.from([9, 2, 0]), packed, Buffer.from([0]), palettes[1],
]));
console.log('prismarine-nbt 2.8.0: 3 independently encoded subchunk fixtures');
