LD_LIBRARY_PATH=/gem5/ext/pesim/pesim-rs/target/debug:$LD_LIBRARY_PATH \
build/RISCV/gem5.opt configs/test/fs-zephyr-rv64.py &

./utils/term/gem5term localhost 3456
