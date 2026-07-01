/*
 * Fixed-latency PESim-rs wrapper stub for gem5 integration testing.
 */

#include "mem/pesim_rs_wrapper.hh"
#include "pesim_rs_ffi.h"
#include "pesim_rs_wrapper.hh"

#include "base/logging.hh"

#include <cassert>
#include <cstdio>
#include <vector>

// #define PSEUDO_SIM

namespace gem5
{
namespace memory
{

PESim_rs_Wrapper::PESim_rs_Wrapper()
{
}

void
PESim_rs_Wrapper::init()
{
#ifndef PSEUDO_SIM
    fatal_if(sim != nullptr, "PESim_rs_Wrapper initialized more than once");
    sim = pesim_new();
    fatal_if(sim == nullptr, "pesim_new() returned a null PESim_body");
#endif
}

PESim_rs_Wrapper::~PESim_rs_Wrapper()
{
#ifndef PSEUDO_SIM
    if (sim != nullptr)
        pesim_free(sim);
    sim = nullptr;
#endif
}

void
PESim_rs_Wrapper::printStats()
{
#ifdef PSEUDO_SIM
    std::fprintf(stdout,
        "PESim-rs stub stats: tick=%llu submitted: %llu pending=%zu complete=%zu\n",
        static_cast<unsigned long long>(tick_cnt),
        static_cast<unsigned long long>(submit_cnt),
        pend_req.size(), complete_req.size());
#else
    if (sim != nullptr)
        pesim_print_stats(sim);
#endif
}

void
PESim_rs_Wrapper::resetStats()
{
#ifdef PSEUDO_SIM
    std::fprintf(stdout, "PESim-rs stub stats reset\n");
#else
    pesim_reset_stats(sim);
#endif
}

bool
PESim_rs_Wrapper::canAccept(uint64_t addr, bool is_write) const
{
#ifdef PSEUDO_SIM
    return pend_req.size() < _queueSize;
#else
    return pesim_canAccept(sim, addr, is_write);
#endif
}

void
PESim_rs_Wrapper::enqueue(uint64_t addr, bool is_write)
{
#ifdef PSEUDO_SIM
    assert(canAccept(addr, is_write));

    PEsim_rs_MemReq req;
    req.addr = addr;
    req.issue_time = tick_cnt;
    req.is_write = is_write;

    pend_req.push(req);
    submit_cnt += 1;
#else
    std::fprintf(stderr,
        "PESim_rs_Wrapper::enqueue() deprecated, use enqueue_with_payload instead\n");
#endif
}

void
PESim_rs_Wrapper::enqueue_with_payload(
    uint64_t addr,
    std::vector<uint8_t> &payload,
    bool is_write)
{
    std::fprintf(stdout,
    "[C++ enqueue] %s addr=0x%llx payload_size=%zu\n",
    is_write ? "WRITE" : "READ",
    static_cast<unsigned long long>(addr),
    payload.size());
#ifdef PSEUDO_SIM
    assert(canAccept(addr, is_write));

    PEsim_rs_MemReq req;
    req.addr = addr;
    req.issue_time = tick_cnt;
    req.is_write = is_write;

    pend_req.push(req);
    submit_cnt += 1;

    // if (is_write) {
    //     assert(payload.size() >= 64);

    //     // This only prints traces.
    //     std::fprintf(stdout,
    //         "---------* Write Trace addr: 0x%llx *---------\n",
    //         static_cast<unsigned long long>(addr));

    //     for (int i = 0; i < 64; i++) {
    //         std::fprintf(stdout,
    //             "Byte %d is : %u\n",
    //             i,
    //             static_cast<unsigned>(payload[i]));
    //     }
    // }

#else
    PESim_payload gem5_payload{};

    fatal_if(payload.size() > 64,
        "PESim payload larger than 64 bytes is unsupported: addr=%#llx size=%zu is_write=%d",
        static_cast<unsigned long long>(addr),
        payload.size(),
        static_cast<int>(is_write));

    gem5_payload.payload_sz_bytes = static_cast<uint32_t>(payload.size());

    if (is_write && !payload.empty()) {
        std::fprintf(stdout,
            "---------* Write Trace addr: 0x%llx payload_sz=%u *---------\n",
            static_cast<unsigned long long>(addr),
            gem5_payload.payload_sz_bytes);

        for (size_t i = 0; i < payload.size(); i++) {
            const size_t dword_idx = i / 8;
            const size_t byte_idx = i % 8;

            gem5_payload.dword_payload[dword_idx] |=
                static_cast<uint64_t>(payload[i]) << (8 * byte_idx);
        }

        for (int i = 0; i < 8; i++) {
            std::fprintf(stdout,
                "PAYLOAD[%d] is: %llu\n",
                i,
                static_cast<unsigned long long>(gem5_payload.dword_payload[i]));
        }
    } else {
        std::fprintf(stdout,
            "---------* %s Trace addr: 0x%llx payload_sz=%u *---------\n",
            is_write ? "Write-empty" : "Read",
            static_cast<unsigned long long>(addr),
            gem5_payload.payload_sz_bytes);
    }

    bool accepted = pesim_enqueue_with_data(sim, addr, gem5_payload, is_write);

    fatal_if(!accepted,
        "PESim rejected request: addr=%#llx size=%u is_write=%d",
        static_cast<unsigned long long>(addr),
        gem5_payload.payload_sz_bytes,
        static_cast<int>(is_write));
#endif
}

double
PESim_rs_Wrapper::clockPeriod() const
{
#ifdef PSEUDO_SIM
    return _clockPeriod;
#else
    return pesim_clock_period(sim);
#endif
}

unsigned int
PESim_rs_Wrapper::queueSize() const
{
#ifdef PSEUDO_SIM
    return _queueSize;
#else
    return pesim_queue_size(sim);
#endif
}

unsigned int
PESim_rs_Wrapper::burstSize() const
{
#ifdef PSEUDO_SIM
    return _burstSize;
#else
    return pesim_burst_size(sim);
#endif
}

bool
PESim_rs_Wrapper::hasComplete() const
{
#ifdef PSEUDO_SIM
    return !complete_req.empty();
#else
    return pesim_has_complete(sim);
#endif
}

PEsim_rs_MemReq
PESim_rs_Wrapper::getComplete()
{
#ifdef PSEUDO_SIM
    assert(hasComplete());

    PEsim_rs_MemReq req = complete_req.front();
    complete_req.pop();

    return req;
#else
    return pesim_get_complete(sim);
#endif
}

void
PESim_rs_Wrapper::tick()
{
#ifdef PSEUDO_SIM
    while (!pend_req.empty()) {
        const PEsim_rs_MemReq& req = pend_req.front();
        const uint64_t done_tick = req.issue_time + LatencyCycles;

        if (done_tick > tick_cnt)
            break;

        complete_req.push(req);
        pend_req.pop();
    }

    ++tick_cnt;
#else
    pesim_tick(sim);
#endif
}

} // namespace memory
} // namespace gem5
