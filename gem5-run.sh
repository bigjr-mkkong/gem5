# LD_LIBRARY_PATH=/gem5/ext/pesim/pesim-rs/target/release:$LD_LIBRARY_PATH \
# build/RISCV/gem5.fast configs/test/fs-zephyr-rv64.py &

# ./utils/term/gem5term localhost 3456

#!/usr/bin/env bash
set -e

export LD_LIBRARY_PATH="/gem5/ext/pesim/pesim-rs/target/release:${LD_LIBRARY_PATH:-}"

build/RISCV/gem5.fast configs/test/fs-zephyr-rv64.py &
gem5_pid=$!

./utils/term/gem5term localhost 3456 &
term_pid=$!

wait "$gem5_pid"
kill "$term_pid" 2>/dev/null || true
