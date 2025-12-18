

struct order_header {
    uint32_t seq_num;
    uint32_t order_id;
};

struct order_body {
    uint32_t price;
    float qty;
    bool type; // 1 == buy,   0 == sell
}