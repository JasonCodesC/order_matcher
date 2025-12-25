#include <iostream>
#include <cstring>
#include <map>
#include <cstdio>
#include "recv_helper.h"
#include "match.h"
#include "send_from_engine.h"
#include "../cpp_helpers/protocols.hpp"
#include <cstdlib>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <poll.h>
#include <sys/mman.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <pthread.h>
#include <sched.h>
#include <net/if.h>
#include <linux/if_link.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h> 
#if __has_include(<bpf/xsk.h>)
#include <bpf/xsk.h>
#elif __has_include(<xdp/xsk.h>)
#include <xdp/xsk.h>
#else
#error "xsk.h not found; install libbpf-dev or libxdp-dev"
#endif


static constexpr uint32_t FRAME_SIZE = 2048;    // size of one packet buffer
static constexpr uint32_t NUM_FRAMES = 65536;    // how many packet buffers in UMEM
static constexpr uint32_t BATCH = 64;           // process packets in chunks
static constexpr int UDP_PORT = 9000;                                
static constexpr const char* IFACE_NAME = "ens160";
static constexpr const char* TRADE_DST_IP = "192.168.37.1";
static constexpr uint16_t TRADE_DST_PORT = 9001;

__attribute__((noinline))
static void die(const char* msg) { 
  std::perror(msg);
  std::exit(1);
}

static int g_ifindex = -1;
static std::atomic<bool> g_running(true);
static constexpr uint32_t kXdpFlags = XDP_FLAGS_SKB_MODE;
static constexpr uint32_t kBindFlags = XDP_COPY;

static inline uint64_t steady_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

static int attach_xdp(int ifindex, int prog_fd, uint32_t flags) {
#if defined(LIBBPF_MAJOR_VERSION) && (LIBBPF_MAJOR_VERSION >= 1)
    return bpf_xdp_attach(ifindex, prog_fd, flags, nullptr);
#else
    return bpf_set_link_xdp_fd(ifindex, prog_fd, flags);
#endif
}

static int detach_xdp(int ifindex, uint32_t flags) {
#if defined(LIBBPF_MAJOR_VERSION) && (LIBBPF_MAJOR_VERSION >= 1)
    return bpf_xdp_detach(ifindex, flags, nullptr);
#else
    return bpf_set_link_xdp_fd(ifindex, -1, flags);
#endif
}

static void detach_xdp() {
    if (g_ifindex >= 0) {
        detach_xdp(g_ifindex, kXdpFlags);
    }
}

static void handle_sig(int) {
    g_running.store(false, std::memory_order_release);
}

static bool pin_thread_to_cpu(pthread_t tid, int cpu, const char* label) {
#if defined(__linux__)
    long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu < 0 || cpu_count <= 0 || cpu >= cpu_count) {
        std::cerr << "pin " << label << " skipped: cpu " << cpu
                  << " not in [0," << (cpu_count - 1) << "]\n";
        return false;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    int rc = pthread_setaffinity_np(tid, sizeof(set), &set);
    if (rc != 0) {
        std::cerr << "pin " << label << " to cpu " << cpu
                  << " failed: " << std::strerror(rc) << "\n";
        return false;
    }
    return true;
#else
    (void)tid;
    (void)cpu;
    (void)label;
    return false;
#endif
}

static void pin_current_thread(int cpu, const char* label) {
    pin_thread_to_cpu(pthread_self(), cpu, label);
}

int main() {

    const char* ifname = IFACE_NAME;
    const char* dst_ip = TRADE_DST_IP;
    const uint16_t dst_port = TRADE_DST_PORT;
    const uint32_t queue_id = 0; // use queue 0

    libbpf_set_strict_mode(LIBBPF_STRICT_ALL);
    std::signal(SIGINT, handle_sig);
    std::signal(SIGTERM, handle_sig);

    // Allocate UMEM 
    void* umem_area = nullptr;
    if (posix_memalign(&umem_area, 4096, (size_t)FRAME_SIZE * NUM_FRAMES)) {   // allocate memory paged aligned
        die("posix_memalign"); // die if allocation fails
    }
    std::memset(umem_area, 0, (size_t)FRAME_SIZE * NUM_FRAMES); //clear

    xsk_umem* umem = nullptr;  //holds packet buffers
    xsk_ring_prod fq;   // fill queue
    xsk_ring_cons cq;   //completion queue
    std::memset(&fq, 0, sizeof(fq));
    std::memset(&cq, 0, sizeof(cq));

    xsk_umem_config ucfg{}; 
    ucfg.fill_size = NUM_FRAMES;   // how many entries in fill queue
    ucfg.comp_size = NUM_FRAMES;   // how many entries in comp queue
    ucfg.frame_size = FRAME_SIZE;  // each buffer size
    ucfg.frame_headroom = 0;       // no extra room
    ucfg.flags = 0;

    // register with kernal
    if (xsk_umem__create(&umem, umem_area, (size_t)FRAME_SIZE * NUM_FRAMES, &fq, &cq, &ucfg) != 0) {                               // create rings too
        die("xsk_umem__create");  // die
    }

    // put all buffers into the fill queue so the kernel has buffers to write into
    uint32_t idx = 0;
    if (xsk_ring_prod__reserve(&fq, NUM_FRAMES, &idx) != (int)NUM_FRAMES) {
        die("fq reserve"); // die
    }
    
    for (uint32_t i{}; i < NUM_FRAMES; i++) { 
        // give kernel the address/offset of frame i
        *xsk_ring_prod__fill_addr(&fq, idx + i) = i * FRAME_SIZE; 
    }
    xsk_ring_prod__submit(&fq, NUM_FRAMES); // submit those addresses to kernel

    // load xdp_kernal
    bpf_object* obj = bpf_object__open_file("xdp_kernal.o", nullptr);
    if (!obj) { //open
        die("bpf_object__open_file");  //die
    }                                   
    if (bpf_object__load(obj) != 0) { // load into kernel
        die("bpf_object__load");  
    }  

    bpf_program* prog = bpf_object__find_program_by_name(obj, "xdp_redirect_udp_9000"); // find function
    if (!prog) {
        die("find_program"); // die if not found   
    }

    int prog_fd = bpf_program__fd(prog);  // get file descriptor for the program
    if (prog_fd < 0) { // die if fd is bad
        die("prog_fd");  
    }  

    int ifindex = if_nametoindex(ifname); // convert "eth0" -> numeric ifindex
    if (ifindex == 0) {
        die("if_nametoindex");
    }
    g_ifindex = ifindex;
    std::atexit(detach_xdp);

    if (attach_xdp(ifindex, prog_fd, kXdpFlags) < 0) {  // attach XDP program to that interface
        die("attach_xdp");
    }

    // Create AF_XDP socket 
    xsk_socket* xsk = nullptr;    // AF_XDP socket handle
    xsk_ring_cons rx;  // recieve queue
    xsk_ring_prod tx;  // transmit queue
    std::memset(&rx, 0, sizeof(rx));
    std::memset(&tx, 0, sizeof(tx));

    xsk_socket_config xcfg{};
    xcfg.rx_size = 16384;
    xcfg.tx_size = 8192; // some kernels reject tx_size=0
#ifdef XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD
    xcfg.libbpf_flags = XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD;
#else
    xcfg.libbpf_flags = 0;
#endif
    xcfg.xdp_flags = kXdpFlags;
    xcfg.bind_flags = kBindFlags;

    // bind socket to (ifname, queue) and create rings
    int err = xsk_socket__create(&xsk, ifname, queue_id, umem, &rx, &tx, &xcfg);
    if (err != 0) {
        std::cerr << "xsk_socket__create failed: " << std::strerror(-err)
                  << " (" << err << ")\n";
        std::exit(1);
    }

    int xsk_fd = xsk_socket__fd(xsk);  // fd for polling
    if (xsk_fd < 0) {
        die("xsk_socket__fd");    
    } 

    // queue id -> our xdp socket fd
    int xsks_map_fd = bpf_object__find_map_fd_by_name(obj, "xsks_map"); //get fd
    if (xsks_map_fd < 0) {
        die("find xsks_map");   
    }
    uint32_t key = queue_id; 
    if (bpf_map_update_elem(xsks_map_fd, &key, &xsk_fd, 0) != 0) {  // tell kernel "queue 0 goes to this socket"
        die("bpf_map_update_elem");
    }

    std::cout << "Engine listening on " << ifname  << " queue " << queue_id 
        << " for UDP dst port " << UDP_PORT << "\n";

    
    

    DedupeWindow dd;
    OrderMsgRing ring;
    TradeMsgRing trade_ring;
    std::atomic<uint64_t> orders_total{0};
    std::atomic<uint64_t> trades_total{0};
    std::atomic<bool> stats_started{false};
    std::atomic<uint64_t> stats_start_ns{0};

    pin_current_thread(1, "xdp_recv_main");

    std::thread matcher([&ring, &trade_ring, &trades_total]() {
        match_loop(ring, trade_ring, g_running, trades_total);
    });
    pin_thread_to_cpu(matcher.native_handle(), 2, "matcher");
    std::thread trade_sender = start_trade_sender(trade_ring, dst_ip, dst_port, g_running);
    pin_thread_to_cpu(trade_sender.native_handle(), 3, "trade_sender");
    std::thread stats_thread([&orders_total, &trades_total, &stats_started, &stats_start_ns]() {
        std::filesystem::create_directories("data");
        std::ofstream out("data/stats.csv", std::ios::trunc);
        if (!out) {
            std::perror("stats.csv");
            return;
        }
        out << "sec,orders_per_sec,trades_per_sec,total_orders,total_trades\n";
        uint64_t last_orders = 0;
        uint64_t last_trades = 0;
        uint64_t last_ts = 0;
        uint64_t next_sample = 0;
        const uint64_t start_offset = 2'500'000ULL;
        const uint64_t step = 2'500'000ULL;
        const uint64_t end_offset = 50'000'000ULL;
        while (g_running.load(std::memory_order_acquire)) {
            if (!stats_started.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            uint64_t now = steady_ns();
            if (last_ts == 0) {
                last_ts = stats_start_ns.load(std::memory_order_relaxed);
                last_orders = orders_total.load(std::memory_order_relaxed);
                last_trades = trades_total.load(std::memory_order_relaxed);
                next_sample = last_ts + start_offset;
                continue;
            }
            if (now < next_sample) {
                std::this_thread::sleep_for(std::chrono::microseconds(200));
                continue;
            }
            uint64_t orders = orders_total.load(std::memory_order_relaxed);
            uint64_t trades = trades_total.load(std::memory_order_relaxed);
            uint64_t elapsed = next_sample - last_ts;
            double sec = (next_sample - stats_start_ns.load(std::memory_order_relaxed)) / 1e9;
            double ops = (orders - last_orders) * 1e9 / (double)elapsed;
            double tps = (trades - last_trades) * 1e9 / (double)elapsed;
            out << std::fixed << std::setprecision(3)
                << sec << "," << ops << "," << tps << "," << orders << "," << trades << "\n";
            out.flush();
            last_ts = next_sample;
            last_orders = orders;
            last_trades = trades;
            next_sample += step;
            if (next_sample - stats_start_ns.load(std::memory_order_relaxed) > end_offset) {
                break;
            }
        }
    });
    pin_thread_to_cpu(stats_thread.native_handle(), 4, "stats");
    // loop: poll Recv ring, handle packets, then recycle buffers
    while (g_running.load(std::memory_order_acquire)) {
        pollfd pfd{};
        pfd.fd = xsk_fd; // poll on xsk fd
        pfd.events = POLLIN; // wake up when packets arrive
        int pret = poll(&pfd, 1, 1000); // wait up to 1 sec
        if (pret < 0) {
            die("poll");   
        } 
        if (pret == 0) {
            continue; // timeout: just loop
        }

        uint32_t rx_idx = 0; // where packets start in recv ring
        uint32_t rcvd = xsk_ring_cons__peek(&rx, BATCH, &rx_idx); // grab up to BATCH packets
        if (rcvd == 0) {
            continue; // nothing ready 
        } 

        for (uint32_t i{}; i < rcvd; i++) {
            const xdp_desc* d = xsk_ring_cons__rx_desc(&rx, rx_idx + i);
            uint8_t* frame = (uint8_t*)umem_area + d->addr;

            Packet p;
            if (!parse_packet(frame, d->len, p, 9000)) {
                continue;
            }
            if (dd.is_duplicate(p.seq_num)) {continue;}

            OrderMsg* slot = nullptr;
            SpinWait wait;
            while (!ring.try_acquire_producer_slot(slot)) {
                if (!g_running.load(std::memory_order_acquire)) { break; }
                wait.pause();
            }
            if (!g_running.load(std::memory_order_acquire)) { break; }
            wait.reset();
            if (!stats_started.load(std::memory_order_relaxed)) {
                if (!stats_started.exchange(true, std::memory_order_acq_rel)) {
                    stats_start_ns.store(steady_ns(), std::memory_order_release);
                }
            }
            slot->seq_num = p.seq_num;
            slot->order_id = p.order_id;
            slot->price_tick = p.price_tick;
            slot->qty = p.qty;
            slot->msg_type = p.msg_type;
            slot->side = p.side;
            ring.commit_producer_slot();
            orders_total.fetch_add(1, std::memory_order_relaxed);
        }

        // return the same buffers back into the fill ring for reuse
        uint32_t fq_idx = 0; // fill ring index

        // reserve space to give buffers back
        uint32_t reserved = xsk_ring_prod__reserve(&fq, rcvd, &fq_idx);
        if (reserved != rcvd) {    
            die("fq reserve (recycle)");   // die if ring is full
        }
        for (uint32_t i{}; i < rcvd; i++) { // for each packet we consumed
            const xdp_desc* d = xsk_ring_cons__rx_desc(&rx, rx_idx + i);  // get its descriptor
            *xsk_ring_prod__fill_addr(&fq, fq_idx + i) = d->addr;  // return its buffer addr to kernel
        }
        xsk_ring_prod__submit(&fq, rcvd); // submit recycled buffers
        xsk_ring_cons__release(&rx, rcvd); // tell kernel we’re done with those RX entries
    }
    matcher.join();
    trade_sender.join();
    stats_thread.join();

    return 0;
}
