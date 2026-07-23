#!/usr/bin/env bash

set -euo pipefail

usage() {
  echo "usage: $0 <pinned-act-checkout> <rvemu-act-smoke> [new-work-directory]" >&2
}

if [[ $# -lt 2 || $# -gt 3 ]]; then
  usage
  exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/../.." && pwd)
act_checkout=$(cd -- "$1" && pwd)
runner=$(cd -- "$(dirname -- "$2")" && pwd)/$(basename -- "$2")
work_dir=${3:-"${repo_root}/build-act4-official"}
if [[ "$work_dir" != /* ]]; then
  work_dir="$(pwd)/${work_dir}"
fi

if [[ ! -x "$runner" ]]; then
  echo "ACT smoke runner is not executable: $runner" >&2
  exit 2
fi

for executable in docker git python3; do
  if ! command -v "$executable" >/dev/null 2>&1; then
    echo "required executable is unavailable: $executable" >&2
    exit 2
  fi
done

read -r act_commit image_ref < <(
  python3 -c '
import json, pathlib, sys
pins = json.loads(pathlib.Path(sys.argv[1]).read_text())
print(pins["riscv_arch_test"]["commit"], pins["container"]["image"] + "@" + pins["container"]["digest"])
' "${script_dir}/pins.json"
)

actual_commit=$(git -C "$act_checkout" rev-parse HEAD)
if [[ "$actual_commit" != "$act_commit" ]]; then
  echo "ACT checkout is at $actual_commit; expected $act_commit" >&2
  exit 2
fi
if ! git -C "$act_checkout" diff --quiet || ! git -C "$act_checkout" diff --cached --quiet; then
  echo "ACT checkout has tracked changes; use a clean checkout of the pinned commit" >&2
  exit 2
fi

if [[ -e "$work_dir" ]]; then
  echo "work directory already exists: $work_dir" >&2
  echo "choose a new path so stale generated files cannot affect the result" >&2
  exit 2
fi

config_root="${work_dir}/config"
mkdir -p "${config_root}/config"
cp "${script_dir}/config/test_config.yaml" "${config_root}/config/test_config.yaml"
cp "${script_dir}/config/rvemu-rv32im.yaml" "${config_root}/config/rvemu-rv32im.yaml"
cp "${script_dir}/link.ld" "${config_root}/link.ld"
cp "${script_dir}/rvmodel_macros.h" "${config_root}/rvmodel_macros.h"
cp "${script_dir}/riscv_arch_test.h" "${config_root}/riscv_arch_test.h"
python3 "${script_dir}/prepare_sail_config.py" \
  "${act_checkout}/config/sail/sail-RVI20U32/sail.json" \
  "${config_root}/sail.json"

docker pull "$image_ref"

jobs=${RVEMU_ACT_JOBS:-2}
docker run --rm \
  --user "$(id -u):$(id -g)" \
  --mount "type=bind,src=${act_checkout},dst=/act4,readonly" \
  --mount "type=bind,src=${script_dir},dst=/rvemu,readonly" \
  --mount "type=bind,src=${work_dir},dst=/work" \
  --env "RVEMU_ACT_JOBS=${jobs}" \
  "$image_ref" \
  /bin/bash -lc '
    set -euo pipefail
    export UV_PROJECT_ENVIRONMENT=/work/.venv
    # The pinned image carries Bundler 4.0.16 while the pinned source lockfile
    # records Bundler 4.0.13. Frozen mode accepts the satisfied dependency set
    # without trying to rewrite the read-only source checksum line.
    export BUNDLE_FROZEN=true
    mise exec -- uv run act /work/config/config/test_config.yaml \
      --workdir /work/act \
      --test-dir /act4/tests \
      --jobs "$RVEMU_ACT_JOBS" \
      --extensions I,M \
      --exclude Sm \
      --fast
    mise exec -- uv run python /rvemu/audit_generated_elfs.py \
      --input-root /work/act/rvemu-rv32im/elfs \
      --report /work/elf-audit.json
  ' 2>&1 | tee "${work_dir}/generation.log"

elf_root="${work_dir}/act/rvemu-rv32im/elfs"
if [[ ! -d "$elf_root" ]]; then
  echo "ACT did not create the expected ELF directory: $elf_root" >&2
  exit 2
fi

{
  while IFS= read -r test_path; do
    if [[ -z "$test_path" || "$test_path" == \#* ]]; then
      continue
    fi
    elf_path="${elf_root}/${test_path}"
    if [[ ! -f "$elf_path" ]]; then
      echo "manifest ELF is missing: $elf_path" >&2
      exit 2
    fi
    "$runner" "$elf_path"
  done < "${script_dir}/smoke-tests.txt"
} 2>&1 | tee "${work_dir}/rvemu-results.log"

echo "ACT 4 smoke selection passed; artifacts: $work_dir"
