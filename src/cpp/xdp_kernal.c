#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/udp.h>

struct {   // define a BPF map
  __uint(type, BPF_MAP_TYPE_XSKMAP);  // map type = XSKMAP (AF_XDP sockets)
  __uint(max_entries, 64);   // how many queues/entries max
  __type(key, __u32);     // key is queue id
  __type(value, __u32);   // value is xdp socket fd
} xsks_map SEC(".maps");  // put map in the ".maps" section

SEC("xdp") // this function runs at XDP hook
int xdp_redirect_udp_9000(struct xdp_md *ctx) { 

    void *data = (void *)(long)ctx->data; // start pointer (bounds)              
    void *data_end = (void *)(long)ctx->data_end; // end pointer (bounds)

    struct ethhdr *eth = data;  // interpret start as Ethernet header
    if ((void *)(eth + 1) > data_end) {
      return XDP_PASS;
    }
    if (eth->h_proto != __bpf_htons(ETH_P_IP)) { // only ipv4
      return XDP_PASS;
    }

    struct iphdr *ip = (void *)(eth + 1); // IP header starts after eth
    if ((void *)(ip + 1) > data_end) {
      return XDP_PASS; 
    }
    if (ip->protocol != IPPROTO_UDP) { //only udp
      return XDP_PASS;
    }

    int ihl_bytes = ip->ihl * 4;   // IP header length in bytes
    struct udphdr *udp = (void *)ip + ihl_bytes;  // UDP header starts after IP
    if ((void *)(udp + 1) > data_end) {
      return XDP_PASS;
    }

    if (udp->dest != __bpf_htons(9000)) { // only port 9000
      return XDP_PASS; 
    }

    int qid = ctx->rx_queue_index; // which recv queue this packet came in on
    return bpf_redirect_map(&xsks_map, qid, 0); // send packet to AF_XDP socket for qid
}

char _license[] SEC("license") = "GPL";
