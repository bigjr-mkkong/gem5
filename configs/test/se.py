import os
import sys

from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.cachehierarchies.classic.no_cache import NoCache
from gem5.components.memory.dramsim_3 import SingleChannel
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import BinaryResource
from gem5.simulate.simulator import Simulator

# 1. Define the hardware
# We use a simple Timing CPU (models 1 cycle per instruction + memory latency)
processor = SimpleProcessor(
    cpu_type=CPUTypes.TIMING, isa=ISA.RISCV, num_cores=1
)

# For a simple benchmark, we can start with no caches (direct to memory)
# or replace NoCache() with PrivateL1SharedL2CacheHierarchy()
cache_hierarchy = NoCache()

# Standard DDR3 memory
memory = SingleChannel("DDR4_4Gb_x4_2400", size="4GiB")

# 2. Assemble the Board
board = SimpleBoard(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

# 3. Set the Workload
# Pass the path to your compiled .out file as a command line argument
# if len(sys.argv) < 2:
#     print("Usage: gem5 se_riscv.py <binary_path>")
#     sys.exit(1)

thispath = os.path.dirname(os.path.realpath(__file__))
binary_path = os.path.join(
    thispath, "../../../sw-payload/gem5-sw/mini-cpubench/hello-world.out"
)

board.set_se_binary_workload(BinaryResource(local_path=binary_path))

# 4. Run the Simulation
simulator = Simulator(board=board)
print(f"Beginning simulation with binary: {binary_path}")
simulator.run()
print("Simulation finished.")
