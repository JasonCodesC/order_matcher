#pragma once

#include "book_types.h"
#include <cstdint>
#include <cstring>
#include <array>
#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>

// Do this so we dont have hugeee mem overhead
struct DedupeWindow {
    static constexpr uint32_t W = 4096; // keep last 4096 seq nums
    static constexpr uint32_t MASK = W - 1;
    std::array<uint8_t, W> seen{}; // 0/1 flags
    uint32_t base = 0; // oldest seq tracked

    inline bool is_duplicate(uint32_t seq) {
        if (seq < base) {
            return true;
        }

        // If seq is ahead, slide the window forward and clear old slots
        if (seq >= base + W) {
            uint32_t new_base = seq - (W - 1);
            for (uint32_t s = base; s < new_base; ++s) {
                seen[s & MASK] = 0;
            }
            base = new_base;
        }
        uint32_t idx = seq & MASK;
        if (seen[idx]) {
            return true;
        }
        seen[idx] = 1;
        return false;
    }
};


static inline bool parse_packet(const uint8_t* frame, uint32_t frame_len, 
        Packet& out, uint16_t udp_port) {

    const uint32_t off = sizeof(ethhdr) + sizeof(iphdr) + sizeof(udphdr);

    // check for body
    if (frame_len < off + sizeof(Packet)) {return false;}

    const auto* payload = reinterpret_cast<const Packet*>(frame + off);

    out.seq_num = ntohl(payload->seq_num);
    out.order_id  = ntohl(payload->order_id);
    out.price_tick = ntohl(payload->price_tick);
    out.qty = ntohl(payload->qty);
    out.msg_type = payload->msg_type;
    out.side = payload->side;

    return true;
}
