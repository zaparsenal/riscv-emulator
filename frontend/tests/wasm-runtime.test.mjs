import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

test("loads and executes the demo ELF in the WebAssembly core", async () => {
  const [{ default: createRvemuModule }, wasmBinary, elf] = await Promise.all([
    import(new URL("../public/wasm/rvemu.js", import.meta.url)),
    readFile(new URL("../public/wasm/rvemu.wasm", import.meta.url)),
    readFile(new URL("../public/demo/rv32im-dashboard.elf", import.meta.url)),
  ]);

  const wasm = await createRvemuModule({
    instantiateWasm(imports, receive) {
      WebAssembly.instantiate(wasmBinary, imports).then(({ instance }) => {
        receive(instance);
      });
      return {};
    },
  });

  const pointer = wasm._rvemu_allocate(elf.byteLength);
  assert.notEqual(pointer, 0);
  wasm.HEAPU8.set(elf, pointer);
  const loaded = JSON.parse(
    wasm.UTF8ToString(wasm._rvemu_load_elf(pointer, elf.byteLength)),
  );
  wasm._rvemu_deallocate(pointer);

  assert.equal(loaded.ok, true);
  assert.equal(loaded.status, "ready");
  assert.equal(loaded.load.entryPoint, "0x80000000");

  const finished = JSON.parse(
    wasm.UTF8ToString(wasm._rvemu_run_batch(1000)),
  );
  assert.equal(finished.status, "exited");
  assert.match(finished.stdout, /hello from RV32IM/);
  assert.equal(finished.statistics.retired, 219);
  assert.equal(finished.statistics.instructionCache.misses, 2);
  assert.equal(finished.statistics.dataCache.misses, 1);
  assert.equal(finished.statistics.branches.predictions, 64);
});
