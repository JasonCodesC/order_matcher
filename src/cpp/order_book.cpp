

class OrderBook {

    public:
    
    inline void on_new_limit(uint32_t order_id, bool is_buy, uint32_t price_tick, uint32_t qty);
    inline void on_cancel(uint32_t order_id);
    inline void on_modify(uint32_t order_id, uint32_t new_price_tick, uint32_t new_qty);
};