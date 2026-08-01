import { access } from "node:fs/promises";

await Promise.all([
  access(new URL("../public/wasm/rvemu.js", import.meta.url)),
  access(new URL("../public/wasm/rvemu.wasm", import.meta.url)),
  access(new URL("../public/demo/rv32im-dashboard.elf", import.meta.url)),
]);
