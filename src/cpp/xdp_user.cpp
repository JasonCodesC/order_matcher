#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <poll.h>
#include <sys/mman.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h> 
#include <bpf/xsk.h>

static constexpr uint32_t FRAME_SIZE = 2048;    // size of one packet buffer
static constexpr uint32_t NUM_FRAMES = 4096;    // how many packet buffers in UMEM
static constexpr uint32_t BATCH = 64;           // process packets in chunks
static constexpr int UDP_PORT = 9000;                                

static void die(const char* msg) {                                           // simple error helper
  std::perror(msg);                                                          // print errno string
  std::exit(1);                                                              // quit
}

int main(int argc, char** argv) {
    if (argc != 2) { 
        std::cerr << "usage: " << argv[0] << " <ifname>\n";
        return 1;
    }
    const char* ifname = argv[1];
    const uint32_t queue_id = 0; // use queue 0

    libbpf_set_strict_mode(LIBBPF_STRICT_ALL); //what?

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
    
    for (uint32_t i = 0; i < NUM_FRAMES; i++) { 
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

    int prog_fd = bpf_program__fd(prog);                                        // get file descriptor for the program
    if (prog_fd < 0) { // die if fd is bad
        die("prog_fd");  
    }  

    int ifindex = if_nametoindex(ifname); // convert "eth0" -> numeric ifindex
    if (ifindex == 0) {
        die("if_nametoindex");
    }

    if (bpf_set_link_xdp_fd(ifindex, prog_fd, 0) < 0) {  // attach XDP program to that interface
        die("bpf_set_link_xdp_fd");
    }

    // Create AF_XDP socket 
    xsk_socket* xsk = nullptr;    // AF_XDP socket handle
    xsk_ring_cons rx;  // recieve queue
    xsk_ring_prod tx;  // transmit queue
    std::memset(&rx, 0, sizeof(rx));
    std::memset(&tx, 0, sizeof(tx));

    xsk_socket_config xcfg{};
    xcfg.rx_size = 2048;
    xcfg.tx_size = 0;
    xcfg.libbpf_flags = 0;
    xcfg.xdp_flags = 0;
    xcfg.bind_flags = XDP_COPY; //Copy mode may change to ZERO

    // bind socket to (ifname, queue) and create rings
    if (xsk_socket__create(&xsk, ifname, queue_id, umem, &rx, &tx, &xcfg) != 0) {
        die("xsk_socket__create");
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

    // loop: poll Recv ring, handle packets, then recycle buffers
    while (true) {
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

        for (uint32_t i = 0; i < rcvd; i++) {    // for each received packet
            xdp_desc* d = xsk_ring_cons__rx_desc(&rx, rx_idx + i);
            uint8_t* frame = (uint8_t*)umem_area + d->addr;

            // parse headers

            Packet p;
            std::memcpy(&p, payload_ptr, sizeof(Packet));
            // TODO ntohl 
            // TODO put into matcher shi
        }

        // return the same buffers back into the fill ring for reuse
        uint32_t fq_idx = 0; // fill ring index

        // reserve space to give buffers back
        if (xsk_ring_prod__reserve(&fq, rcvd, &fq_idx) != (int)rcvd) {    
            die("fq reserve (recycle)");   // die if ring is full
        }
        for (uint32_t i = 0; i < rcvd; i++) { // for each packet we consumed
            xdp_desc* d = xsk_ring_cons__rx_desc(&rx, rx_idx + i);  // get its descriptor
            *xsk_ring_prod__fill_addr(&fq, fq_idx + i) = d->addr;  // return its buffer addr to kernel
        }
        xsk_ring_prod__submit(&fq, rcvd); // submit recycled buffers
        xsk_ring_cons__release(&rx, rcvd); // tell kernel we’re done with those RX entries
    }
    
    return 0;
}
