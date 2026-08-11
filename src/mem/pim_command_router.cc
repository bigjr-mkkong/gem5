/*
 * Host-visible MMIO endpoint which broadcasts PIM commands to PESim
 * controllers without making the command page part of physical DRAM.
 */

#include "mem/pim_command_router.hh"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include "base/callback.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/PIMCommand.hh"
#include "mem/pesim_rs.hh"

namespace gem5
{
namespace memory
{

PIMCommandRouter::PIMCommandRouter(const Params &p)
    : ClockedObject(p),
      port(name() + ".port", *this),
      mmioRange(p.mmioRange),
      targets(),
      sendResponseEvent([this] { sendResponse(); }, name())
{
    for (auto *target : p.targets) {
        auto *pesim = dynamic_cast<PESim_rs *>(target);
        fatal_if(pesim == nullptr,
                 "%s target %s is not a PESim controller", name(),
                 target->name());
        targets.push_back(pesim);
        pesim->setPimQueryCompletionCallback(
            [this](uint32_t controller_id, uint64_t progress) {
                completePimQuery(controller_id, progress);
            });
    }

    fatal_if(targets.empty(),
             "%s requires at least one PIM-enabled target", name());

    registerExitCallback([this]() {
        inform("PIM command router stats: writes_received=%llu "
               "writes_broadcast=%llu writes_dropped=%llu "
               "write_target_deliveries=%llu queries_received=%llu "
               "queries_completed=%llu\n",
               static_cast<unsigned long long>(receivedWrites),
               static_cast<unsigned long long>(broadcastWrites),
               static_cast<unsigned long long>(droppedWrites),
               static_cast<unsigned long long>(broadcastWrites * targets.size()),
               static_cast<unsigned long long>(receivedQueries),
               static_cast<unsigned long long>(completedQueries));
    });
}

void
PIMCommandRouter::init()
{
    ClockedObject::init();
    fatal_if(!port.isConnected(), "%s is unconnected", name());
    port.sendRangeChange();

    inform("PIM command router range [%#llx, %#llx] targets=%llu\n",
           static_cast<unsigned long long>(mmioRange.start()),
           static_cast<unsigned long long>(mmioRange.end()),
           static_cast<unsigned long long>(targets.size()));
}

Port &
PIMCommandRouter::getPort(const std::string &if_name, PortID idx)
{
    if (if_name != "port")
        return ClockedObject::getPort(if_name, idx);
    return port;
}

Tick
PIMCommandRouter::recvAtomic(PacketPtr pkt)
{
    panic_if(!pkt->getAddrRange().isSubset(mmioRange),
             "%s received out-of-range packet %s", name(), pkt->print());

    if (pkt->isRead()) {
        pkt->makeAtomicResponse();
        pkt->setBadAddress();
        return 0;
    }

    panic_if(!pkt->isWrite(), "%s received unsupported packet %s",
             name(), pkt->print());

    broadcastCommand(pkt, true);
    pkt->makeAtomicResponse();
    return 0;
}

bool
PIMCommandRouter::broadcastCommand(PacketPtr pkt, bool is_write)
{
    panic_if(pkt->isWrite() != is_write || pkt->isRead() == is_write,
             "%s received a PIM command with an inconsistent access type",
             name());

    const Addr offset = pkt->getAddr() - mmioRange.start();
    std::vector<uint8_t> payload(pkt->getSize(), 0);
    if (is_write) {
        const uint8_t *data = pkt->getConstPtr<uint8_t>();
        payload.assign(data, data + pkt->getSize());
        ++receivedWrites;
    }

    for (auto *target : targets) {
        if (!target->canAcceptPimCommand(offset, payload, is_write)) {
            if (is_write)
                ++droppedWrites;
            warn("%s dropping PIM command offset=%#llx size=%llu because "
                 "target %s rejected it\n",
                 name(), static_cast<unsigned long long>(offset),
                 static_cast<unsigned long long>(payload.size()),
                 target->name());
            return false;
        }
    }

    for (auto *target : targets)
        target->enqueuePimCommand(offset, payload, is_write);
    if (is_write)
        ++broadcastWrites;
    DPRINTF(PIMCommand,
            "Broadcast offset=%#llx size=%llu to %llu PIM controllers\n",
            static_cast<unsigned long long>(offset),
            static_cast<unsigned long long>(payload.size()),
            static_cast<unsigned long long>(targets.size()));

    return true;
}

bool
PIMCommandRouter::recvTimingReq(PacketPtr pkt)
{
    panic_if(!pkt->getAddrRange().isSubset(mmioRange),
             "%s received out-of-range packet %s", name(), pkt->print());

    if (pkt->isRead()) {
        if (pendingQuery != nullptr) {
            retryQueryReq = true;
            return false;
        }

        DPRINTF(PIMCommand,
                "Accepting PIM_QUERY addr=%#llx size=%u at tick=%llu\n",
                static_cast<unsigned long long>(pkt->getAddr()),
                pkt->getSize(), static_cast<unsigned long long>(curTick()));
        fatal_if(!broadcastCommand(pkt, false),
                 "%s PIM_QUERY was rejected after reaching the router", name());
        pendingQueryReadyTick =
            curTick() + pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        pendingQuery = pkt;
        queryResponders.clear();
        queryTotal = 0;
        queryFinished = 0;
        ++receivedQueries;
        return true;
    }

    panic_if(!pkt->isWrite(), "%s received unsupported packet %s",
             name(), pkt->print());
    broadcastCommand(pkt, true);
    if (pkt->needsResponse()) {
        const Tick ready_tick =
            curTick() + pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        pkt->makeTimingResponse();
        queueResponse(pkt, ready_tick);
    } else {
        pendingDelete.reset(pkt);
    }
    return true;
}

void
PIMCommandRouter::completePimQuery(uint32_t controller_id, uint64_t progress)
{
    DPRINTF(PIMCommand,
            "PIM_QUERY completion controller=%u progress=%#llx at tick=%llu\n",
            controller_id, static_cast<unsigned long long>(progress),
            static_cast<unsigned long long>(curTick()));
    fatal_if(pendingQuery == nullptr,
             "%s received an unexpected PIM_QUERY completion from controller %u",
             name(), controller_id);
    fatal_if(!queryResponders.insert(controller_id).second,
             "%s received duplicate PIM_QUERY completion from controller %u",
             name(), controller_id);

    queryTotal += progress >> 32;
    queryFinished += progress & 0xffff'ffffULL;
    fatal_if(queryTotal > std::numeric_limits<uint32_t>::max()
                 || queryFinished > std::numeric_limits<uint32_t>::max(),
             "%s aggregated PIM_QUERY count exceeds 32 bits", name());

    if (queryResponders.size() != targets.size())
        return;

    const uint64_t packed = (queryTotal << 32) | queryFinished;
    std::array<uint8_t, sizeof(packed)> payload{};
    for (size_t byte = 0; byte < payload.size(); ++byte)
        payload[byte] = static_cast<uint8_t>(packed >> (byte * 8));
    pendingQuery->setData(payload.data());
    pendingQuery->makeTimingResponse();
    DPRINTF(PIMCommand,
            "Aggregated PIM_QUERY total=%llu finished=%llu; queueing response\n",
            static_cast<unsigned long long>(queryTotal),
            static_cast<unsigned long long>(queryFinished));
    queueResponse(pendingQuery, pendingQueryReadyTick);
    pendingQuery = nullptr;
    pendingQueryReadyTick = 0;
    ++completedQueries;

    if (retryQueryReq) {
        retryQueryReq = false;
        port.sendRetryReq();
    }
}

void
PIMCommandRouter::queueResponse(PacketPtr pkt, Tick ready_tick)
{
    responseQueue.emplace_back(pkt, ready_tick);
    if (!retryResp && !sendResponseEvent.scheduled())
        schedule(sendResponseEvent, std::max(curTick(), ready_tick));
}

void
PIMCommandRouter::sendResponse()
{
    panic_if(responseQueue.empty(), "%s has no response to send", name());
    if (port.sendTimingResp(responseQueue.front().first)) {
        DPRINTF(PIMCommand, "Sent PIM MMIO response at tick=%llu\n",
                static_cast<unsigned long long>(curTick()));
        responseQueue.pop_front();
        if (!responseQueue.empty())
            schedule(sendResponseEvent,
                     std::max(curTick(), responseQueue.front().second));
    } else {
        DPRINTF(PIMCommand, "PIM MMIO response blocked at tick=%llu\n",
                static_cast<unsigned long long>(curTick()));
        retryResp = true;
    }
}

void
PIMCommandRouter::recvRespRetry()
{
    panic_if(!retryResp, "%s received an unexpected response retry", name());
    retryResp = false;
    sendResponse();
}

void
PIMCommandRouter::recvFunctional(PacketPtr pkt)
{
    recvAtomic(pkt);
}

PIMCommandRouter::CommandPort::CommandPort(
    const std::string &name, PIMCommandRouter &router)
    : ResponsePort(name), router(router)
{
}

Tick
PIMCommandRouter::CommandPort::recvAtomic(PacketPtr pkt)
{
    return router.recvAtomic(pkt);
}

void
PIMCommandRouter::CommandPort::recvFunctional(PacketPtr pkt)
{
    router.recvFunctional(pkt);
}

bool
PIMCommandRouter::CommandPort::recvTimingReq(PacketPtr pkt)
{
    return router.recvTimingReq(pkt);
}

void
PIMCommandRouter::CommandPort::recvRespRetry()
{
    router.recvRespRetry();
}

AddrRangeList
PIMCommandRouter::CommandPort::getAddrRanges() const
{
    return {router.getAddrRange()};
}

} // namespace memory
} // namespace gem5
