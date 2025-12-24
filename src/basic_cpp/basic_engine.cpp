#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <iostream>

#include "../cpp_helpers/protocols.hpp"
#include "../cpp/book_types.h"

static constexpr uint16_t LISTEN_PORT = 9000;
static constexpr const char* TRADE_DST_IP = "10.0.0.1";
static constexpr uint16_t TRADE_DST_PORT = 9001;

int main() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(LISTEN_PORT);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        return 1;
    }

    Books book;
    uint8_t buf[2048];

    int trade_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (trade_fd < 0) {
        std::perror("trade socket");
        return 1;
    }
    sockaddr_in trade_addr{};
    trade_addr.sin_family = AF_INET;
    trade_addr.sin_port = htons(TRADE_DST_PORT);
    if (inet_pton(AF_INET, TRADE_DST_IP, &trade_addr.sin_addr) != 1) {
        std::cerr << "bad trade dst ip\n";
        return 1;
    }

    while (true) {
        ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n < (ssize_t)sizeof(Packet)) { continue; }

        const Packet* p = reinterpret_cast<const Packet*>(buf);
        OrderMsg msg{};
        msg.seq_num = ntohl(p->seq_num);
        msg.order_id = ntohl(p->order_id);
        msg.price_tick = ntohl(p->price_tick);
        msg.qty = ntohl(p->qty);
        msg.msg_type = p->msg_type;
        msg.side = p->side;

        switch (msg.msg_type) {
            case MsgType::NewLimit:
                if (msg.side == Order_Type::Buy) {
                    book.bids.on_new_limit(msg.order_id, msg.price_tick, msg.qty);
                } else {
                    book.asks.on_new_limit(msg.order_id, msg.price_tick, msg.qty);
                }
                break;
            case MsgType::Cancel:
                if (msg.side == Order_Type::Buy) {
                    book.bids.on_cancel(msg.order_id);
                } else {
                    book.asks.on_cancel(msg.order_id);
                }
                break;
            case MsgType::Modify:
                if (msg.side == Order_Type::Buy) {
                    book.bids.on_modify(msg.order_id, msg.price_tick, msg.qty);
                } else {
                    book.asks.on_modify(msg.order_id, msg.price_tick, msg.qty);
                }
                break;
            default:
                break;
        }

        uint32_t best_bid_price, best_ask_price;
        while (book.bids.best_price(best_bid_price) && book.asks.best_price(best_ask_price)
               && best_bid_price >= best_ask_price) {

            uint32_t bid_px, ask_px;
            Order* bid_o = book.bids.best_order(bid_px);
            Order* ask_o = book.asks.best_order(ask_px);
            if (!bid_o || !ask_o) { break; }

            const uint32_t trade_qty = (bid_o->qty < ask_o->qty) ? bid_o->qty : ask_o->qty;
            const uint32_t trade_px = (msg.side == Order_Type::Buy) ? ask_px : bid_px;

            TradeMsg wire{
                bid_o->order_id,
                ask_o->order_id,
                trade_px,
                trade_qty
            };
            TradeMsg out{
                htonl(wire.bid_order_id),
                htonl(wire.ask_order_id),
                htonl(wire.price_tick),
                htonl(wire.qty)
            };
            sendto(trade_fd, &out, sizeof(out), 0,
                         reinterpret_cast<sockaddr*>(&trade_addr), sizeof(trade_addr));

            bid_o->qty -= trade_qty;
            ask_o->qty -= trade_qty;
            if (bid_o->qty == 0) { book.bids.remove_best(bid_px); }
            if (ask_o->qty == 0) { book.asks.remove_best(ask_px); }
        }
    }

    close(fd);
    close(trade_fd);
    return 0;
}
