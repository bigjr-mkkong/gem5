#!/usr/bin/env bash
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
project_root=${PIMTLB_ROOT:-$(cd -- "$script_dir/.." && pwd -P)}

exec docker run -u "$(id -u):$(id -g)" \
    --volume "$project_root/gem5:/gem5" \
    --volume "$project_root/sw-payload:/sw-payload" \
    --rm -it gem5env
