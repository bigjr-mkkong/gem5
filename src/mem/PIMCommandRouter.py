from m5.objects.ClockedObject import ClockedObject
from m5.params import *


class PIMCommandRouter(ClockedObject):
    type = "PIMCommandRouter"
    cxx_header = "mem/pim_command_router.hh"
    cxx_class = "gem5::memory::PIMCommandRouter"

    port = ResponsePort("Port receiving host PIM command MMIO writes")
    mmioRange = Param.AddrRange("Guest-visible PIM command MMIO page")
    # Keep this typed as SimObject rather than AbstractMemory. gem5's
    # Parent.any memory discovery hashes scalar memory parameters and cannot
    # hash a SimObjectVector whose declared element type is AbstractMemory.
    targets = VectorParam.SimObject(
        [], "PIM-enabled PESim controllers receiving each command"
    )
