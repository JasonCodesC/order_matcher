#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>
#include "../cpp_helpers/protocols.hpp"

// One resting order inside a price level
struct Order {
  uint32_t order_id;
  uint32_t qty;
};

// Where an order lives so we can cancel/modify fast
struct info {
  uint32_t price_tick;
  uint32_t pos_in_level; // index in the vector at that price level
};

/* ________ CONTAINERS _________*/

//   std::map<uint32_t, std::vector<Order>, std::greater<uint32_t>> for bids
//   std::map<uint32_t, std::vector<Order>, std::less<uint32_t>> for asks
//   std::vector<uint32_t, std::vector<Order>> But sorted

template <Order_Type T, typename Container>
class OrderBook {
    Container book_;   // price_tick -> vector of orders (FIFO)
    std::unordered_map<uint32_t, info> index_;  // order_id -> where it is

    inline void add_to_book(uint32_t order_id, uint32_t price_tick, uint32_t qty) {
        auto& level = book_[price_tick];
        const uint32_t pos = (uint32_t)level.size();
        level.push_back(Order{order_id, qty});
        index_[order_id] = info{price_tick, pos};
    }

    public:
 
    inline void on_new_limit(uint32_t order_id, uint32_t price_tick, uint32_t qty) {
        add_to_book(order_id, price_tick, qty);
    }

    inline void on_cancel(uint32_t order_id) {
        auto it = index_.find(order_id);
        if (it == index_.end()) {return;}

        const uint32_t price = it->second.price_tick;
        auto lvl_it = book_.find(price);
        if (lvl_it == book_.end()) { index_.erase(it); return; }

        auto& level = lvl_it->second;
        const uint32_t pos = it->second.pos_in_level;

        // swap-erase from vector for O(1)
        const uint32_t last = (uint32_t)level.size() - 1;
        if (pos != last) {
            level[pos] = level[last];
            // fix index for the order
            index_[level[pos].order_id] = info{price, pos};
        }
        level.pop_back();
        index_.erase(it);

        if (level.empty()) {book_.erase(lvl_it);}
    }

    inline void on_modify(uint32_t order_id, uint32_t new_price_tick, uint32_t new_qty) {
        auto it = index_.find(order_id);
        if (it == index_.end()) {return;}

        const uint32_t old_price = it->second.price_tick;
        if (old_price == new_price_tick) {
            // just change qty
            auto lvl_it = book_.find(old_price);
            if (lvl_it == book_.end()) {return;}
            auto& level = lvl_it->second;
            level[it->second.pos_in_level].qty = new_qty;
            return;
        }

        //cancel and add at new price
        on_cancel(order_id);
        on_new_limit(order_id, new_price_tick, new_qty);
    }

    inline const Container& raw_levels() const { return book_; }
};

