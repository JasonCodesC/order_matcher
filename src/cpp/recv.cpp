
#include "../cpp_helpers/protocols.hpp"
#include "order_book.cpp"
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

    // Returns true if seq is a duplicate
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
        if (seen[idx]) return true;
        seen[idx] = 1;
        return false;
    }
};


static inline bool parse_packet(const uint8_t* frame, uint32_t frame_len, Packet& out, uint16_t udp_port) {

    // too small
    if (frame_len < sizeof(ethhdr)) {return false;}

    const auto* eth = reinterpret_cast<const ethhdr*>(frame);
    uint16_t eth_type = ntohs(eth->h_proto);

    if (eth_type != ETH_P_IP) {return false;}

    // too small
    uint32_t off = sizeof(ethhdr);
    if (frame_len < off + sizeof(iphdr)) {return false;}

    // wrong protocol
    const auto* ip = reinterpret_cast<const iphdr*>(frame + off);
    if (ip->version != 4) {return false;}
    if (ip->protocol != IPPROTO_UDP) {return false;}

    // too small
    uint32_t ip_hl = ip->ihl * 4;
    if (ip_hl < 20) {return false;}
    if (frame_len < off + ip_hl + sizeof(udphdr)) {return false;}

    off += ip_hl;
    const auto* udp = reinterpret_cast<const udphdr*>(frame + off);
    uint16_t dst_port = ntohs(udp->dest);
    // wrong port (9000)
    if (dst_port != udp_port) {return false;}

    off += sizeof(udphdr);

    // Payload have our 18-byte Packet
    if (frame_len < off + sizeof(Packet)) {return false;}

    std::memcpy(&out, frame + off, sizeof(Packet));

    out.seq_num = ntohl(out.seq_num);
    out.order_id  = ntohl(out.order_id);
    out.price_tick = ntohl(out.price_tick);
    out.qty = ntohl(out.qty);

    return true;
}

    // -------------------- handle_packet: dedupe + switch + call book --------------------
static inline void handle_packet(const Packet& p, DedupeWindow& dd, OrderBook& book) {
    // dupes
    if (dd.is_duplicate(p.seq_num)) {return};

    //send to engine
    const bool is_buy = (p.side == Order_Type::Buy);
    switch (p.msg_type) {
        case MsgType::NewLimit:
            book.on_new_limit(p.order_id, is_buy, p.price_tick, p.qty);
            break;
        case MsgType::Cancel:
            book.on_cancel(p.order_id);
            break;
        case MsgType::Modify:
            book.on_modify(p.order_id, p.price_tick, p.qty);
            break;
        default:
            break;
    }
}