import os

from gem5.components.boards.x86_board import X86Board
from gem5.components.cachehierarchies.classic.private_l1_private_l2_cache_hierarchy import (
    PrivateL1PrivateL2CacheHierarchy,
)
from gem5.components.memory.dramsim_3 import SingleChannelDDR4_2400
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import (
    BootloaderResource,
    DiskImageResource,
    KernelResource,
)
from gem5.simulate.simulator import Simulator

# Paths you provided
KERNEL = "/sw-payload/linux-x86/vmlinux"
DISK = "/sw-payload/rootfs/out-rootfs/rootfs.ext4"

# Fail fast if payloads are missing
assert os.path.exists(KERNEL), f"Kernel not found: {KERNEL}"
assert os.path.exists(DISK), f"Disk image not found: {DISK}"

# CPU
processor = SimpleProcessor(cpu_type=CPUTypes.TIMING, num_cores=1, isa=ISA.X86)

# Private L1 + Private L2
cache_hierarchy = PrivateL1PrivateL2CacheHierarchy(
    l1i_size="32KiB",
    l1d_size="32KiB",
    l2_size="512KiB",
)

# DRAMSim3 single-channel DDR4-2400 (uses DDR4_4Gb_x8_2400.ini internally)
memory = SingleChannelDDR4_2400(size="2GiB")

# Board
board = X86Board(
    clk_freq="2GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

# Workload
kernel_res = KernelResource(local_path=KERNEL)
disk_res = DiskImageResource(local_path=DISK)

board.set_kernel_disk_workload(
    kernel=kernel_res,
    disk_image=disk_res,
    kernel_args="console=ttyS0 root=/dev/sda rw init=/sbin/init",
)

sim = Simulator(board=board)
sim.run()
