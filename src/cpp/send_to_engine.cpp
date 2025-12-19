

#include <sys/types.h>
#include <sys/socket.h>
#include <random>
#include <ctime>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <iostream>
#include <cstdint>
#include <arpa/inet.h>
#include "../cpp_helpers/protocols.hpp"

static constexpr const char* DST_IP = "10.0.0.1";
static constexpr uint16_t DST_PORT = 9000;
static constexpr uint16_t TRADE_LISTEN_PORT = 9001;

static void send_loop() {
    std::mt19937 engine(static_cast<unsigned int>(std::time(nullptr)));
    //std::uniform_int_distribution<int> sleep_ms(1, 2);
    std::uniform_int_distribution<int> qty_dist(1, 100);
    std::uniform_int_distribution<uint32_t> price_delta(-500, 500);
    std::uniform_int_distribution<int> side_dist(0, 1);
    const uint32_t base_price = 10000;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == -1) {
        std::perror("socket");
        std::exit(1);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DST_PORT);
    if (inet_pton(AF_INET, DST_IP, &addr.sin_addr) != 1) {
        std::cerr << "bad DST_IP\n";
        std::exit(1);
    }

    uint32_t seq = 1;
    int counter = 0;
    while (counter < 20000) {
        // randomish orders
        Packet p{};
        p.seq_num = htonl(seq);
        p.order_id = htonl(seq);
        const uint32_t px = base_price + price_delta(engine);
        p.price_tick = htonl(px > 1 ? px : 1u);
        p.qty = htonl((uint32_t)qty_dist(engine));
        p.msg_type = MsgType::NewLimit;
        p.side = side_dist(engine) ? Order_Type::Buy : Order_Type::Sell;

        if (sendto(fd, &p, sizeof(p), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::perror("sendto");
        }

        ++seq; ++counter;
    //    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms(engine)));
    }
}

static void recv_trades_loop() {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::perror("trade recv socket");
        std::exit(1);
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(TRADE_LISTEN_PORT);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind trade recv");
        std::exit(1);
    }

    uint64_t total_notional = 0;

    while (true) {
        uint8_t buf[64]{};
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n < (ssize_t)sizeof(uint32_t) * 4) { continue; }
        const uint32_t* w = reinterpret_cast<const uint32_t*>(buf);
        uint32_t bid = ntohl(w[0]);
        uint32_t ask = ntohl(w[1]);
        uint32_t px  = ntohl(w[2]);
        uint32_t qty = ntohl(w[3]);
        total_notional += static_cast<uint64_t>(px) * qty;
        std::cout << "TRADE bid=" << bid << " ask=" << ask << " px=" << px << " qty=" << qty << "\n";
        std::cout << "TOTAL_NOTIONAL=" << total_notional << "\n";
    }
}

int main() {
    std::thread t(send_loop);
    recv_trades_loop();
    t.join();
    return 0;
}
