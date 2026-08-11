import os

from m5.objects import PMAChecker

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

PESIM_CONFIG_DIR = "/gem5/ext/pesim/pesim-rs/cfg"
PESIM_REGULAR_CONFIG = os.path.join(PESIM_CONFIG_DIR, "DDR4_8Gb_x4_2400.ini")
PESIM_PIM_CONFIG = os.path.join(PESIM_CONFIG_DIR, "DDR4_8Gb_x4_2400_pim.ini")
PESIM_PRECACT_CONFIG = os.path.join(
    PESIM_CONFIG_DIR, "DDR4_8Gb_x4_2400_pim_prec_act.ini"
)

# PIM_on controls the hardware capability: select the _pim DRAM configuration
# for both controllers and install their shared out-of-range command page.
PIM_on = os.environ.get("PIM_ON", "1") != "0"
# PIM_full controls test scale only: False exposes 32 engines (2 GiB) on
# controller 0; True prepares all 256 engines (16 GiB) across both controllers.
PIM_full = os.environ.get("PIM_FULL", "1") != "0"
PIM_scenario = os.environ.get("PIM_SCENARIO", "parallel-fast-switch")

if PIM_scenario not in (
    "sequential",
    "parallel-prec-act",
    "parallel-fast-switch",
):
    raise ValueError(f"unsupported PIM_SCENARIO={PIM_scenario}")

PESIM_SELECTED_PIM_CONFIG = (
    PESIM_PIM_CONFIG
    if PIM_scenario == "parallel-fast-switch"
    else PESIM_PRECACT_CONFIG
)

if PIM_full and not PIM_on:
    raise ValueError("PIM_full=True requires PIM_on=True")

if PIM_on:
    CHANNEL0_PIM_SIZE = (8192 if PIM_full else 2048) * 1024 * 1024
    CHANNEL1_PIM_SIZE = 8192 * 1024 * 1024 if PIM_full else 0
else:
    CHANNEL0_PIM_SIZE = 0
    CHANNEL1_PIM_SIZE = 0


def add_pim_uncacheable_pma(board):
    pim_range = board.get_memory().get_pim_mmio_range()
    if pim_range is None:
        raise ValueError("PIM MMIO range requested without a PIM router")

    for core in board.get_processor().get_cores():
        mmu = core.get_mmu()

        old_ranges = []

        if hasattr(mmu, "pma_checker") and mmu.pma_checker is not None:
            try:
                old_ranges = list(mmu.pma_checker.uncacheable)
            except Exception:
                old_ranges = []

        mmu.pma_checker = PMAChecker(uncacheable=old_ranges + [pim_range])

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
    channel0_config=(
        PESIM_SELECTED_PIM_CONFIG if PIM_on else PESIM_REGULAR_CONFIG
    ),
    channel1_config=(
        PESIM_SELECTED_PIM_CONFIG
        if PIM_on and PIM_full
        else PESIM_REGULAR_CONFIG
    ),
    channel0_pim_size=CHANNEL0_PIM_SIZE,
    channel1_pim_size=CHANNEL1_PIM_SIZE,
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
    readfile_contents=os.environ.get("GEM5_READFILE_CONTENTS"),
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
