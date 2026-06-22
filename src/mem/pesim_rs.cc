/*
 * PESim-rs gem5 memory object, adapted from gem5 DRAMsim3 integration.
 */

#include "mem/pesim_rs.hh"

#include "base/callback.hh"
#include "base/trace.hh"
#include "debug/DRAMsim3.hh"
#include "debug/Drain.hh"
#include "pesim_rs_wrapper.hh"
#include "sim/system.hh"
#include <cstdio>
#include <vector>
#include <cassert>

namespace gem5
{
namespace memory
{

PESim_rs::PESim_rs(const Params &p) :
    AbstractMemory(p),
    port(name() + ".port", *this),
    wrapper(),
    retryReq(false), retryResp(false), startTick(0),
    nbrOutstandingReads(0), nbrOutstandingWrites(0),
    sendResponseEvent([this]{ sendResponse(); }, name()),
    tickEvent([this]{ tick(); }, name())
{
    registerExitCallback([this]() { wrapper.printStats(); });
}

void
PESim_rs::init()
{
    AbstractMemory::init();
    wrapper.init();

    DPRINTF(DRAMsim3,
            "Instantiated PESim_rs with clock %f ns, queue size %u, "
            "burst size %u\n",
            wrapper.clockPeriod(), wrapper.queueSize(), wrapper.burstSize());

    if (!port.isConnected()) {
        fatal("PESim_rs %s is unconnected!\n", name());
    } else {
        port.sendRangeChange();
    }

    if (system()->cacheLineSize() != wrapper.burstSize()) {
        fatal("PESim_rs burst size %u does not match cache line size %u\n",
              wrapper.burstSize(), system()->cacheLineSize());
    }
}

void
PESim_rs::startup()
{
    startTick = curTick();
    schedule(tickEvent, clockEdge());
}

void
PESim_rs::resetStats()
{
    wrapper.resetStats();
}

void
PESim_rs::sendResponse()
{
    assert(!retryResp);
    assert(!responseQueue.empty());

    DPRINTF(DRAMsim3, "Attempting to send response\n");

    bool success = port.sendTimingResp(responseQueue.front());
    if (success) {
        responseQueue.pop_front();

        DPRINTF(DRAMsim3, "Have %u read, %u write, %zu responses outstanding\n",
                nbrOutstandingReads, nbrOutstandingWrites,
                responseQueue.size());

        if (!responseQueue.empty() && !sendResponseEvent.scheduled())
            schedule(sendResponseEvent, curTick());

        if (nbrOutstanding() == 0)
            signalDrainDone();
    } else {
        retryResp = true;
        DPRINTF(DRAMsim3, "Waiting for response retry\n");
        assert(!sendResponseEvent.scheduled());
    }
}

unsigned int
PESim_rs::nbrOutstanding() const
{
    return nbrOutstandingReads + nbrOutstandingWrites + responseQueue.size();
}

void
PESim_rs::tick()
{
    if (system()->isTimingMode()) {
        wrapper.tick();

        while (wrapper.hasComplete()) {
            const PEsim_rs_MemReq req = wrapper.getComplete();

            DPRINTF(DRAMsim3, "PESim_rs completion addr=%#llx is_write=%d\n",
                    static_cast<unsigned long long>(req.addr), req.is_write);

            if (req.is_write)
                writeComplete(0, req.addr);
            else
                readComplete(0, req.addr);
        }

        if (retryReq && nbrOutstanding() < wrapper.queueSize()) {
            retryReq = false;
            port.sendRetryReq();
        }
    }

    schedule(tickEvent,
        curTick() + wrapper.clockPeriod() * sim_clock::as_int::ns);
}

Tick
PESim_rs::recvAtomic(PacketPtr pkt)
{
    access(pkt);

    // Fixed-latency stub: 50 ns when a cache is not already responding.
    return pkt->cacheResponding() ? 0 : 50 * sim_clock::as_int::ns;
}

void
PESim_rs::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(name());

    functionalAccess(pkt);

    for (auto i = responseQueue.begin(); i != responseQueue.end(); ++i)
        pkt->trySatisfyFunctional(*i);

    pkt->popLabel();
}

bool
PESim_rs::recvTimingReq(PacketPtr pkt)
{
    if (pkt->cacheResponding()) {
        pendingDelete.reset(pkt);
        return true;
    }

    if (retryReq)
        return false;

    const Addr addr = pkt->getAddr();
    const bool is_write = pkt->isWrite();
    size_t payload_sz = pkt->getSize();

    std::vector<uint8_t> payload;
    if (is_write) {
        panic_if(payload_sz != 64,
         "gem5: Expect DRAM payload to be 64 bytes, got %d\n",
         payload_sz);

        const uint8_t *ptr = pkt->getConstPtr<uint8_t>();

        payload.assign(ptr, ptr + 64);
    } else {
        payload.resize(64, 0);
    }
    const bool can_accept =
        nbrOutstanding() < wrapper.queueSize() &&
        wrapper.canAccept(addr, is_write);

    if (pkt->isRead()) {
        if (can_accept) {
            outstandingReads[addr].push(pkt);
            ++nbrOutstandingReads;
        }
    } else if (pkt->isWrite()) {
        if (can_accept) {
            outstandingWrites[addr].push(pkt);
            ++nbrOutstandingWrites;

            // Preserve gem5 DRAMsim3 behavior: writes respond immediately.
            accessAndRespond(pkt);
        }
    } else {
        accessAndRespond(pkt);
        return true;
    }

    if (can_accept) {
        DPRINTF(DRAMsim3, "Enqueueing address %#llx is_write=%d\n",
                static_cast<unsigned long long>(addr), is_write);

        // wrapper.enqueue(addr, is_write);
        wrapper.enqueue_with_payload(addr, payload, is_write);
        return true;
    }

    retryReq = true;
    return false;
}

void
PESim_rs::recvRespRetry()
{
    DPRINTF(DRAMsim3, "Retrying response\n");

    assert(retryResp);
    retryResp = false;
    sendResponse();
}

void
PESim_rs::accessAndRespond(PacketPtr pkt)
{
    DPRINTF(DRAMsim3, "Access for address %#llx\n",
            static_cast<unsigned long long>(pkt->getAddr()));

    bool needsResponse = pkt->needsResponse();

    access(pkt);

    if (needsResponse) {
        assert(pkt->isResponse());

        Tick time = curTick() + pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;

        DPRINTF(DRAMsim3, "Queuing response for address %#llx\n",
                static_cast<unsigned long long>(pkt->getAddr()));

        responseQueue.push_back(pkt);

        if (!retryResp && !sendResponseEvent.scheduled())
            schedule(sendResponseEvent, time);
    } else {
        pendingDelete.reset(pkt);
    }
}

void
PESim_rs::readComplete(unsigned id, uint64_t addr)
{
    DPRINTF(DRAMsim3, "Read to address %#llx complete\n",
            static_cast<unsigned long long>(addr));

    auto p = outstandingReads.find(addr);
    assert(p != outstandingReads.end());

    PacketPtr pkt = p->second.front();
    p->second.pop();

    if (p->second.empty())
        outstandingReads.erase(p);

    assert(nbrOutstandingReads != 0);
    --nbrOutstandingReads;

    accessAndRespond(pkt);
}

void
PESim_rs::writeComplete(unsigned id, uint64_t addr)
{
    DPRINTF(DRAMsim3, "Write to address %#llx complete\n",
            static_cast<unsigned long long>(addr));

    auto p = outstandingWrites.find(addr);
    assert(p != outstandingWrites.end());

    p->second.pop();
    if (p->second.empty())
        outstandingWrites.erase(p);

    assert(nbrOutstandingWrites != 0);
    --nbrOutstandingWrites;

    if (nbrOutstanding() == 0)
        signalDrainDone();
}

Port&
PESim_rs::getPort(const std::string &if_name, PortID idx)
{
    if (if_name != "port")
        return ClockedObject::getPort(if_name, idx);

    return port;
}

DrainState
PESim_rs::drain()
{
    return nbrOutstanding() != 0 ? DrainState::Draining : DrainState::Drained;
}

PESim_rs::MemoryPort::MemoryPort(const std::string& _name,
                                 PESim_rs& _memory)
    : ResponsePort(_name), mem(_memory)
{
}

AddrRangeList
PESim_rs::MemoryPort::getAddrRanges() const
{
    AddrRangeList ranges;
    ranges.push_back(mem.getAddrRange());
    return ranges;
}

Tick
PESim_rs::MemoryPort::recvAtomic(PacketPtr pkt)
{
    return mem.recvAtomic(pkt);
}

void
PESim_rs::MemoryPort::recvFunctional(PacketPtr pkt)
{
    mem.recvFunctional(pkt);
}

bool
PESim_rs::MemoryPort::recvTimingReq(PacketPtr pkt)
{
    return mem.recvTimingReq(pkt);
}

void
PESim_rs::MemoryPort::recvRespRetry()
{
    mem.recvRespRetry();
}

} // namespace memory
} // namespace gem5
