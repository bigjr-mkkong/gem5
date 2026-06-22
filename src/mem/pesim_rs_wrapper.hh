/*
 * Fixed-latency PESim-rs wrapper stub for gem5 integration testing.
 */

#ifndef __MEM_PESIM_RS_WRAPPER_HH__
#define __MEM_PESIM_RS_WRAPPER_HH__

#include <cstdint>
#include <queue>
#include "pesim_rs_ffi.h"

namespace gem5
{
namespace memory
{


class PESim_rs_Wrapper
{
  private:
    static constexpr uint64_t LatencyCycles = 50;
    static constexpr unsigned int QueueSize = 1024;
    static constexpr unsigned int BurstSize = 64;

    uint64_t tick_cnt = 0;
    std::queue<PEsim_rs_MemReq> pend_req;
    std::queue<PEsim_rs_MemReq> complete_req;

    uint64_t submit_cnt = 0;

    // Memory clock period in ns. Latency is modeled as LatencyCycles ticks.
    double _clockPeriod = 1.0;
    unsigned int _queueSize = QueueSize;
    unsigned int _burstSize = BurstSize;

    PESim_body *sim = nullptr;

  public:
    PESim_rs_Wrapper();
    ~PESim_rs_Wrapper();

    // Construct the Rust simulator only after gem5 has initialized the
    // owning SimObject.  Rust FFI access before this point is invalid.
    void init();

    void printStats();
    void resetStats();

    bool canAccept(uint64_t addr, bool is_write) const;
    void enqueue(uint64_t addr, bool is_write);
    void enqueue_with_payload(uint64_t addr, std::vector<uint8_t> &payload, bool is_write);

    double clockPeriod() const;
    unsigned int queueSize() const;
    unsigned int burstSize() const;

    bool hasComplete() const;
    PEsim_rs_MemReq getComplete();

    void tick();
};

} // namespace memory
} // namespace gem5

#endif // __MEM_PESIM_RS_WRAPPER_HH__
