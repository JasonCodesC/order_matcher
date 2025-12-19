#pragma once

#include "match.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include <cstring>
#include <iostream>

struct TradeWire {
    uint32_t bid_order_id;
    uint32_t ask_order_id;
    uint32_t price_tick;
    uint32_t qty;
};

inline std::thread start_trade_sender(TradeMsgRing& trades, const char* dst_ip, uint16_t dst_port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::perror("trade sender socket");
        std::exit(1);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(dst_port);
    if (inet_pton(AF_INET, dst_ip, &addr.sin_addr) != 1) {
        std::cerr << "invalid dst_ip\n";
        std::exit(1);
    }

    return std::thread([fd, addr, &trades]() mutable {
        while (true) {
            TradeMsg* slot = nullptr;
            if (!trades.try_acquire_consumer_slot(slot)) { continue; }
            TradeMsg& t = *slot;

            TradeWire wire{
                htonl(t.bid_order_id),
                htonl(t.ask_order_id),
                htonl(t.price_tick),
                htonl(t.qty)
            };

            (void)sendto(fd, &wire, sizeof(wire), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

            trades.release_consumer_slot();
        }
    });
}
