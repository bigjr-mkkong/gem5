/*
 * PESim-rs gem5 memory object, adapted from gem5 DRAMsim3 integration.
 */

#ifndef __MEM_PESIM_RS_HH__
#define __MEM_PESIM_RS_HH__

#include <deque>
#include <functional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "mem/abstract_mem.hh"
#include "mem/pesim_rs_wrapper.hh"
#include "mem/qport.hh"
#include "params/DRAMsim3.hh"

namespace gem5
{
namespace memory
{

class PESim_rs : public AbstractMemory
{
  private:
    class MemoryPort : public ResponsePort
    {
      private:
        PESim_rs& mem;

      public:
        MemoryPort(const std::string& _name, PESim_rs& _memory);

      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
        AddrRangeList getAddrRanges() const override;
    };

    MemoryPort port;
    PESim_rs_Wrapper wrapper;
    const std::string configFile;
    const std::string outputDir;
    const uint32_t controllerId;
    const uint64_t pimSize;

    bool retryReq;
    bool retryResp;
    Tick startTick;

    std::unordered_map<Addr, std::queue<PacketPtr>> outstandingReads;
    std::unordered_map<Addr, std::queue<PacketPtr>> outstandingWrites;

    unsigned int nbrOutstandingReads;
    unsigned int nbrOutstandingWrites;

    std::deque<PacketPtr> responseQueue;

    unsigned int nbrOutstanding() const;

    void accessAndRespond(PacketPtr pkt);
    void sendResponse();

    EventFunctionWrapper sendResponseEvent;

    void tick();
    EventFunctionWrapper tickEvent;

    std::unique_ptr<Packet> pendingDelete;
    std::function<void(uint32_t, uint64_t)> pimQueryCompletionCallback;

  public:
    // This keeps compatibility with gem5's existing DRAMsim3.py SimObject.
    typedef DRAMsim3Params Params;

    PESim_rs(const Params &p);

    void readComplete(unsigned id, uint64_t addr);
    void writeComplete(unsigned id, uint64_t addr);

    DrainState drain() override;

    Port& getPort(const std::string& if_name,
                  PortID idx = InvalidPortID) override;

    void init() override;
    void startup() override;
    void resetStats() override;

    bool canAcceptPimCommand(
        uint64_t offset, const std::vector<uint8_t> &payload,
        bool is_write) const;
    void enqueuePimCommand(
        uint64_t offset, const std::vector<uint8_t> &payload,
        bool is_write);
    void setPimQueryCompletionCallback(
        std::function<void(uint32_t, uint64_t)> callback);

  protected:
    Tick recvAtomic(PacketPtr pkt);
    void recvFunctional(PacketPtr pkt);
    bool recvTimingReq(PacketPtr pkt);
    void recvRespRetry();
};

} // namespace memory
} // namespace gem5

#endif // __MEM_PESIM_RS_HH__
