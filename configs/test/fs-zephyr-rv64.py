import os

from gem5.components.boards.riscv_board import RiscvBoard
from gem5.components.cachehierarchies.classic.no_cache import NoCache
from gem5.components.cachehierarchies.classic.private_l1_private_l2_cache_hierarchy import (
    PrivateL1PrivateL2CacheHierarchy,
)
from gem5.components.memory.dramsim_3 import DualChannelPESim
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import (
    BootloaderResource,
    DiskImageResource,
    KernelResource,
)
from gem5.simulate.simulator import Simulator

from m5.objects import AddrRange, PMAChecker

PESIM_CONFIG_DIR = "/gem5/ext/pesim/pesim-rs/cfg"
PESIM_REGULAR_CONFIG = os.path.join(
    PESIM_CONFIG_DIR, "DDR4_8Gb_x4_2400.ini"
)
PESIM_PIM_CONFIG = os.path.join(
    PESIM_CONFIG_DIR, "DDR4_8Gb_x4_2400_pim.ini"
)

# PIM_on controls the hardware capability: select the _pim DRAM configuration
# for controller 1 and reserve its final 512 KiB as an uncacheable command area.
PIM_on = True
# PIM_full controls test scale only: False exposes 32 engines (2 GiB);
# True prepares 127 engines (8128 MiB) while excluding the final 64 MiB engine.
PIM_full = True

if PIM_full and not PIM_on:
    raise ValueError("PIM_full=True requires PIM_on=True")

PIM_SIZE = (8128 if PIM_full else 2048) * 1024 * 1024 if PIM_on else 0
PIM_CMD_BASE = 0x4_7FF8_0000
PIM_CMD_SIZE = 512 * 1024


def add_pim_uncacheable_pma(board):
    pim_range = AddrRange(PIM_CMD_BASE, size=PIM_CMD_SIZE)

    for core in board.get_processor().get_cores():
        mmu = core.get_mmu()

        old_ranges = []

        if hasattr(mmu, "pma_checker") and mmu.pma_checker is not None:
            try:
                old_ranges = list(mmu.pma_checker.uncacheable)
            except Exception:
                old_ranges = []

        mmu.pma_checker = PMAChecker(
            uncacheable=old_ranges + [pim_range]
        )

        print(f"Installed PIM PMA uncacheable range: {pim_range}")

# FIRMWARE = "/sw-payload/gem5-sw/opensbi/build/platform/generic/firmware/fw_jump.elf"
FIRMWARE = (
    "/sw-payload/gem5-sw/zephyr-proj/mini-cpubench/build/zephyr/zephyr.elf"
)
KERNEL = (
    "/sw-payload/gem5-sw/zephyr-proj/mini-cpubench/build/zephyr/zephyr.elf"
)
DISK = "/sw-payload/gem5-sw/rootfs/out-rootfs/pseudo_disk.img"  # root filesystem image

assert os.path.exists(KERNEL), f"Kernel not found: {KERNEL}"
assert os.path.exists(DISK), f"Disk image not found: {DISK}"

processor = SimpleProcessor(
    cpu_type=CPUTypes.TIMING, num_cores=2, isa=ISA.RISCV
)

cache_hierarchy = PrivateL1PrivateL2CacheHierarchy(
    l1d_size="32KiB", l1i_size="32KiB", l2_size="512KiB"
)
# cache_hierarchy = NoCache()

memory = DualChannelPESim(
    channel0_config=PESIM_REGULAR_CONFIG,
    channel1_config=PESIM_PIM_CONFIG if PIM_on else PESIM_REGULAR_CONFIG,
    channel1_pim_size=PIM_SIZE,
)

board = RiscvBoard(
    clk_freq="2GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

fw_res = BootloaderResource(local_path=FIRMWARE)
kernel_res = KernelResource(local_path=KERNEL)
disk_res = DiskImageResource(local_path=DISK)

board.set_kernel_disk_workload(
    bootloader=fw_res,
    kernel=kernel_res,
    disk_image=disk_res,
    # kernel_args=[
    #     "console=ttyS0",
    #     "root=/dev/vda1",
    #     "rw",
    #     "init=/sbin/init",
    # ],
)

if PIM_on:
    add_pim_uncacheable_pma(board)
sim = Simulator(board=board)

print("[cfg] launching simulation …")
sim.run()
print("[cfg] finished")
