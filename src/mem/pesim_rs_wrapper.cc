/*
 * Fixed-latency PESim-rs wrapper stub for gem5 integration testing.
 */

#include "mem/pesim_rs_wrapper.hh"

#include <cassert>
#include <cstdio>

namespace gem5
{
namespace memory
{

PESim_rs_Wrapper::PESim_rs_Wrapper()
{
}

PESim_rs_Wrapper::~PESim_rs_Wrapper()
{
}

void
PESim_rs_Wrapper::printStats()
{
    std::fprintf(stdout,
        "PESim-rs stub stats: tick=%llu submitted: %llu pending=%zu complete=%zu\n",
        static_cast<unsigned long long>(tick_cnt),
        static_cast<unsigned long long>(submit_cnt),
        pend_req.size(), complete_req.size());
}

void
PESim_rs_Wrapper::resetStats()
{
    std::fprintf(stdout, "PESim-rs stub stats reset\n");
}

bool
PESim_rs_Wrapper::canAccept(uint64_t addr, bool is_write) const
{
    return pend_req.size() < _queueSize;
}

void
PESim_rs_Wrapper::enqueue(uint64_t addr, bool is_write)
{
    assert(canAccept(addr, is_write));

    PEsim_rs_MemReq req;
    req.addr = addr;
    req.issue_time = tick_cnt;
    req.is_write = is_write;

    pend_req.push(req);
    submit_cnt += 1;
}

double
PESim_rs_Wrapper::clockPeriod() const
{
    return _clockPeriod;
}

unsigned int
PESim_rs_Wrapper::queueSize() const
{
    return _queueSize;
}

unsigned int
PESim_rs_Wrapper::burstSize() const
{
    return _burstSize;
}

bool
PESim_rs_Wrapper::hasComplete() const
{
    return !complete_req.empty();
}

PEsim_rs_MemReq
PESim_rs_Wrapper::getComplete()
{
    assert(hasComplete());

    PEsim_rs_MemReq req = complete_req.front();
    complete_req.pop();
    return req;
}

void
PESim_rs_Wrapper::tick()
{
    while (!pend_req.empty()) {
        const PEsim_rs_MemReq& req = pend_req.front();
        const uint64_t done_tick = req.issue_time + LatencyCycles;

        if (done_tick > tick_cnt)
            break;

        complete_req.push(req);
        pend_req.pop();
    }

    ++tick_cnt;
}

} // namespace memory
} // namespace gem5
