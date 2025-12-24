

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
#include <unordered_map>
#include <vector>
#include <mutex>
#include <filesystem>
#include <fstream>
#include "../cpp_helpers/protocols.hpp"

static constexpr const char* DST_IP = "10.0.0.1";
static constexpr uint16_t DST_PORT = 9000;
static constexpr uint16_t TRADE_LISTEN_PORT = 9001;
static constexpr const char* LATENCY_FILE = "data/latencies.csv";

namespace {
uint64_t now_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}
}

static std::unordered_map<uint32_t, uint64_t> g_send_ts;
static std::vector<uint64_t> g_lat;
static std::mutex g_mu;

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

        {
            std::lock_guard<std::mutex> lg(g_mu);
            g_send_ts[seq] = now_ns();
        }

        if (sendto(fd, &p, sizeof(p), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            std::perror("sendto");
        }

        ++seq; ++counter;
    //    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms(engine)));
    }
}

static void recv_trades_loop() {
    std::filesystem::create_directories("data");
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
    auto flush_lat = []() {
        std::ofstream out(LATENCY_FILE, std::ios::app);
        if (!out) { return; }
        std::lock_guard<std::mutex> lg(g_mu);
        for (uint64_t v : g_lat) {
            out << v << "\n";
        }
        g_lat.clear();
    };

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

        uint64_t sent_ns = 0;
        {
            std::lock_guard<std::mutex> lg(g_mu);
            auto itb = g_send_ts.find(bid);
            auto ita = g_send_ts.find(ask);
            if (itb != g_send_ts.end() && ita != g_send_ts.end()) {
                sent_ns = (itb->second < ita->second) ? itb->second : ita->second;
                g_send_ts.erase(itb);
                g_send_ts.erase(ita);
            } else if (itb != g_send_ts.end()) {
                sent_ns = itb->second;
                g_send_ts.erase(itb);
            } else if (ita != g_send_ts.end()) {
                sent_ns = ita->second;
                g_send_ts.erase(ita);
            }
            if (sent_ns != 0) {
                const uint64_t lat = now_ns() - sent_ns;
                g_lat.push_back(lat);
                if (g_lat.size() >= 100) { flush_lat(); }
            }
        }
    }
}

int main() {
    std::thread t(send_loop);
    recv_trades_loop();
    t.join();
    return 0;
}
