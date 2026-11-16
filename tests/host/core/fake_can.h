// SPDX-License-Identifier: MIT
// Fake CAN bus and a fake ZLAC8015D CANopen node (object dictionary, SDO
// server, NMT, controlword state machine, heartbeat and EMCY producer).
#pragma once

#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <utility>
#include <vector>

extern "C" {
#include "oamr/can.h"
}

// Captures every frame the code under test transmits.
struct FakeCanTx {
    std::vector<oamr_can_frame_t> sent;
    std::deque<oamr_can_frame_t> pending;
    bool fail = false;

    static bool send(void *ctx, const oamr_can_frame_t *f)
    {
        auto *self = static_cast<FakeCanTx *>(ctx);
        if (self->fail) {
            return false;
        }
        self->sent.push_back(*f);
        self->pending.push_back(*f);
        return true;
    }
    oamr_can_tx_t tx() { return oamr_can_tx_t{&FakeCanTx::send, this}; }
};

inline oamr_can_frame_t make_frame(uint32_t id, std::vector<uint8_t> data, bool ext = false)
{
    oamr_can_frame_t f{};
    f.id = id;
    f.extended = ext;
    f.dlc = static_cast<uint8_t>(data.size());
    for (size_t i = 0; i < data.size() && i < 8; i++) {
        f.data[i] = data[i];
    }
    return f;
}

struct SdoRequest {
    bool write;
    uint16_t index;
    uint8_t sub;
    uint32_t value;
};

struct FakeZlac {
    using Key = std::pair<uint16_t, uint8_t>;
    struct Obj {
        uint32_t value;
        uint8_t size;
    };
    uint8_t node = 1;
    std::map<Key, Obj> od;
    std::set<Key> ignore_writes;   // writes acknowledged but not stored
    std::set<Key> abort_on_access; // SDO abort
    bool silent = false;           // node does not answer at all
    uint8_t nmt = 0x7F;
    bool fault_clearable = true;
    std::vector<SdoRequest> requests;
    std::vector<uint8_t> nmt_commands;

    FakeZlac()
    {
        // Factory values (vendor routine / community EDS).
        od[{0x6060, 0}] = {0, 1};
        od[{0x6083, 1}] = {500, 4};
        od[{0x6083, 2}] = {500, 4};
        od[{0x6084, 1}] = {500, 4};
        od[{0x6084, 2}] = {500, 4};
        od[{0x2000, 0}] = {0, 2};
        od[{0x1017, 0}] = {500, 2};
        od[{0x2010, 0}] = {0, 2};
        od[{0x6040, 0}] = {0, 2};
        od[{0x6041, 0}] = {0x0250, 2};  // switch on disabled
        od[{0x603F, 0}] = {0, 4};
        od[{0x60FF, 1}] = {0, 4};
        od[{0x60FF, 2}] = {0, 4};
        od[{0x606C, 1}] = {0, 4};
        od[{0x606C, 2}] = {0, 4};
    }
    uint32_t get(uint16_t i, uint8_t s) { return od[{i, s}].value; }
    void set(uint16_t i, uint8_t s, uint32_t v) { od[{i, s}].value = v; }

    void controlword(uint32_t cw)
    {
        uint32_t &sw = od[{0x6041, 0}].value;
        if (cw == 0x80) {
            if (fault_clearable) {
                od[{0x603F, 0}].value = 0;
                sw = 0x0250;
            }
            return;
        }
        if (od[{0x603F, 0}].value != 0) {
            sw = 0x0008;  // stays in fault
            return;
        }
        if (cw == 0x06) sw = 0x0231;
        if (cw == 0x07) sw = 0x0233;
        if (cw == 0x0F) sw = 0x0237;
    }
    void inject_fault(uint32_t code)
    {
        od[{0x603F, 0}].value = code;
        od[{0x6041, 0}].value = 0x0008;
    }

    // Handle one frame from the master; returns frames the node answers with.
    std::vector<oamr_can_frame_t> handle(const oamr_can_frame_t &f)
    {
        std::vector<oamr_can_frame_t> out;
        if (f.id == 0x000 && f.dlc == 2 && (f.data[1] == node || f.data[1] == 0)) {
            nmt_commands.push_back(f.data[0]);
            if (f.data[0] == 0x01) nmt = 0x05;
            if (f.data[0] == 0x80) nmt = 0x7F;
            return out;
        }
        if (f.id != 0x600u + node || silent) {
            return out;
        }
        const uint8_t cmd = f.data[0];
        const uint16_t index = static_cast<uint16_t>(f.data[1] | (f.data[2] << 8));
        const uint8_t sub = f.data[3];
        uint32_t v = 0;
        std::memcpy(&v, &f.data[4], 4);
        if (cmd == 0x80) {
            return out;  // abort from the client
        }
        const Key k{index, sub};
        auto reply = [&](uint8_t scs, uint32_t val) {
            std::vector<uint8_t> d = {scs, f.data[1], f.data[2], sub, static_cast<uint8_t>(val),
                                      static_cast<uint8_t>(val >> 8), static_cast<uint8_t>(val >> 16),
                                      static_cast<uint8_t>(val >> 24)};
            out.push_back(make_frame(0x580u + node, d));
        };
        const bool write = (cmd & 0xE0) == 0x20;
        requests.push_back({write, index, sub, v});
        if (abort_on_access.count(k) || od.count(k) == 0) {
            reply(0x80, 0x06020000u);
            return out;
        }
        if (write) {
            if (!ignore_writes.count(k)) {
                od[k].value = v;
                if (index == 0x6040) controlword(v);
            }
            reply(0x60, 0);
        } else {
            const Obj &o = od[k];
            const uint8_t scs = static_cast<uint8_t>(0x43 | ((4 - o.size) << 2));
            reply(scs, o.value);
        }
        return out;
    }

    oamr_can_frame_t heartbeat() const { return make_frame(0x700u + node, {nmt}); }
    oamr_can_frame_t bootup() const { return make_frame(0x700u + node, {0x00}); }

    size_t writes_to(uint16_t index) const
    {
        size_t n = 0;
        for (const auto &r : requests) {
            if (r.write && r.index == index) n++;
        }
        return n;
    }
};
