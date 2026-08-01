import assert from "node:assert/strict";
import { access } from "node:fs/promises";
import test from "node:test";

async function render() {
  const workerUrl = new URL("../dist/server/index.js", import.meta.url);
  workerUrl.searchParams.set("test", `${process.pid}-${Date.now()}`);
  const { default: worker } = await import(workerUrl.href);
  return worker.fetch(
    new Request("http://localhost/", { headers: { accept: "text/html" } }),
    { ASSETS: { fetch: async () => new Response("Not found", { status: 404 }) } },
    { waitUntil() {}, passThroughOnException() {} },
  );
}

test("server-renders the emulator dashboard shell", async () => {
  const response = await render();
  assert.equal(response.status, 200);
  assert.match(response.headers.get("content-type") ?? "", /^text\/html\b/i);

  const html = await response.text();
  assert.match(html, /<title>RVEMU Browser Lab/);
  assert.match(html, /RVEMU/);
  assert.match(html, /Registers/);
  assert.match(html, /Performance model/);
  assert.match(html, /Memory inspector/);
  assert.match(html, /Guest console/);
  assert.doesNotMatch(html, /codex-preview|react-loading-skeleton/i);
});

test("ships the browser emulator assets", async () => {
  await Promise.all([
    access(new URL("../public/wasm/rvemu.js", import.meta.url)),
    access(new URL("../public/wasm/rvemu.wasm", import.meta.url)),
    access(new URL("../public/demo/rv32im-dashboard.elf", import.meta.url)),
  ]);
});
