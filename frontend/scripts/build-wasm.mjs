import { chmod, copyFile, mkdir } from "node:fs/promises";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const frontendRoot = fileURLToPath(new URL("../", import.meta.url));
const projectRoot = fileURLToPath(new URL("../../", import.meta.url));
const buildRoot = fileURLToPath(new URL("../.wasm-build/", import.meta.url));
const publicRoot = fileURLToPath(new URL("../public/wasm/", import.meta.url));

function run(command, args) {
  const result = spawnSync(command, args, {
    cwd: frontendRoot,
    stdio: "inherit",
  });
  if (result.error) {
    throw result.error;
  }
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}

await mkdir(buildRoot, { recursive: true });
await mkdir(publicRoot, { recursive: true });

run("emcmake", [
  "cmake",
  "-S",
  projectRoot,
  "-B",
  buildRoot,
  "-DBUILD_TESTING=OFF",
  "-DRVEMU_BUILD_CLI=OFF",
  "-DRVEMU_BUILD_WEB=ON",
  "-DCMAKE_BUILD_TYPE=Release",
]);
run("cmake", ["--build", buildRoot, "--target", "rvemu_web", "-j", "4"]);

await copyFile(`${buildRoot}/web/rvemu.js`, `${publicRoot}/rvemu.js`);
await copyFile(`${buildRoot}/web/rvemu.wasm`, `${publicRoot}/rvemu.wasm`);
await chmod(`${publicRoot}/rvemu.js`, 0o644);
await chmod(`${publicRoot}/rvemu.wasm`, 0o644);

console.log("WebAssembly module copied to public/wasm.");
