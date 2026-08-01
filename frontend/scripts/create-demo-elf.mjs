import { mkdir, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const base = 0x80000000;
const segmentOffset = 0x100;
const encoder = new TextEncoder();
const message = encoder.encode("rvemu: hello from RV32IM\n");

function iType(opcode, rd, funct3, rs1, immediate) {
  return (
    ((immediate & 0xfff) << 20) |
    (rs1 << 15) |
    (funct3 << 12) |
    (rd << 7) |
    opcode
  ) >>> 0;
}

function sType(funct3, rs1, rs2, immediate) {
  const encoded = immediate & 0xfff;
  return (
    (((encoded >>> 5) & 0x7f) << 25) |
    (rs2 << 20) |
    (rs1 << 15) |
    (funct3 << 12) |
    ((encoded & 0x1f) << 7) |
    0x23
  ) >>> 0;
}

function bType(funct3, rs1, rs2, immediate) {
  const encoded = immediate & 0x1fff;
  return (
    (((encoded >>> 12) & 1) << 31) |
    (((encoded >>> 5) & 0x3f) << 25) |
    (rs2 << 20) |
    (rs1 << 15) |
    (funct3 << 12) |
    (((encoded >>> 1) & 0xf) << 8) |
    (((encoded >>> 11) & 1) << 7) |
    0x63
  ) >>> 0;
}

const codeSize = 20 * 4;
const messageAddress = base + codeSize;
const dataAddress =
  ((messageAddress + message.byteLength + 3) & 0xfffffffc) >>> 0;

const instructions = [
  iType(0x13, 10, 0, 0, 1),
  (11 << 7) | 0x17,
  iType(0x13, 11, 0, 11, messageAddress - (base + 4)),
  iType(0x13, 12, 0, 0, message.byteLength),
  iType(0x13, 17, 0, 0, 64),
  0x00000073,
  iType(0x13, 5, 0, 0, 0),
  iType(0x13, 6, 0, 0, 32),
  (7 << 7) | 0x17,
  iType(0x13, 7, 0, 7, dataAddress - (base + 32)),
  sType(2, 7, 5, 0),
  iType(0x03, 28, 2, 7, 0),
  iType(0x13, 29, 7, 5, 1),
  bType(0, 29, 0, 8),
  iType(0x13, 30, 0, 30, 3),
  iType(0x13, 5, 0, 5, 1),
  bType(1, 5, 6, -24),
  iType(0x13, 10, 0, 0, 0),
  iType(0x13, 17, 0, 0, 93),
  0x00000073,
];

const segmentFileSize = dataAddress - base;
const segmentMemorySize = segmentFileSize + 64;
const image = new Uint8Array(segmentOffset + segmentFileSize);
const view = new DataView(image.buffer);

image.set([0x7f, 0x45, 0x4c, 0x46, 1, 1, 1, 0], 0);
view.setUint16(16, 2, true);
view.setUint16(18, 243, true);
view.setUint32(20, 1, true);
view.setUint32(24, base, true);
view.setUint32(28, 52, true);
view.setUint32(32, 0, true);
view.setUint32(36, 0, true);
view.setUint16(40, 52, true);
view.setUint16(42, 32, true);
view.setUint16(44, 1, true);
view.setUint16(46, 0, true);
view.setUint16(48, 0, true);
view.setUint16(50, 0, true);

view.setUint32(52, 1, true);
view.setUint32(56, segmentOffset, true);
view.setUint32(60, base, true);
view.setUint32(64, base, true);
view.setUint32(68, segmentFileSize, true);
view.setUint32(72, segmentMemorySize, true);
view.setUint32(76, 7, true);
view.setUint32(80, 0x100, true);

instructions.forEach((instruction, index) => {
  view.setUint32(segmentOffset + index * 4, instruction, true);
});
image.set(message, segmentOffset + codeSize);

const outputDirectory = fileURLToPath(new URL("../public/demo/", import.meta.url));
await mkdir(outputDirectory, { recursive: true });
await writeFile(`${outputDirectory}/rv32im-dashboard.elf`, image);
console.log("Built the browser demo ELF.");
