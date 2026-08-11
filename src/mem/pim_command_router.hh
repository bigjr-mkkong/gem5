/*
 * Host-visible MMIO endpoint which broadcasts PIM commands to PESim
 * controllers without making the command page part of physical DRAM.
 */

#ifndef __MEM_PIM_COMMAND_ROUTER_HH__
#define __MEM_PIM_COMMAND_ROUTER_HH__

#include <cstdint>
#include <deque>
#include <memory>
#include <utility>
#include <unordered_set>
#include <vector>

#include "base/addr_range.hh"
#include "mem/port.hh"
#include "params/PIMCommandRouter.hh"
#include "sim/clocked_object.hh"

namespace gem5
{
namespace memory
{

class PESim_rs;

class PIMCommandRouter : public ClockedObject
{
  private:
    class CommandPort : public ResponsePort
    {
      private:
        PIMCommandRouter &router;

      public:
        CommandPort(const std::string &name, PIMCommandRouter &router);

      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
        AddrRangeList getAddrRanges() const override;
    };

    CommandPort port;
    const AddrRange mmioRange;
    std::vector<PESim_rs *> targets;
    uint64_t receivedWrites = 0;
    uint64_t broadcastWrites = 0;
    uint64_t droppedWrites = 0;
    uint64_t receivedQueries = 0;
    uint64_t completedQueries = 0;

    PacketPtr pendingQuery = nullptr;
    Tick pendingQueryReadyTick = 0;
    std::unordered_set<uint32_t> queryResponders;
    uint64_t queryTotal = 0;
    uint64_t queryFinished = 0;
    bool retryQueryReq = false;

    std::deque<std::pair<PacketPtr, Tick>> responseQueue;
    bool retryResp = false;
    EventFunctionWrapper sendResponseEvent;
    std::unique_ptr<Packet> pendingDelete;

    Tick recvAtomic(PacketPtr pkt);
    void recvFunctional(PacketPtr pkt);
    bool recvTimingReq(PacketPtr pkt);
    void recvRespRetry();
    bool broadcastCommand(PacketPtr pkt, bool is_write);
    void queueResponse(PacketPtr pkt, Tick ready_tick);
    void sendResponse();

  public:
    using Params = PIMCommandRouterParams;

    explicit PIMCommandRouter(const Params &p);

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
    void init() override;

    void completePimQuery(uint32_t controller_id, uint64_t progress);

    const AddrRange &getAddrRange() const { return mmioRange; }
};

} // namespace memory
} // namespace gem5

#endif // __MEM_PIM_COMMAND_ROUTER_HH__
