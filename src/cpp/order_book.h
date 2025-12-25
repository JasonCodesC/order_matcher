#pragma once

#include <array>
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


template <Order_Type T, typename Container>
class OrderBook {
    Container book_;   // price_tick -> vector of orders
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

    inline bool best_price(uint32_t& out_price) const {
        if (book_.empty()) {return false;}
        out_price = book_.begin()->first;
        return true;
    }

    inline Order* best_order(uint32_t& price_tick) {
        if (book_.empty()) {return nullptr;}
        auto it = book_.begin();
        price_tick = it->first;
        return &it->second.back();
    }

    inline void remove_best(uint32_t price_tick) {
        auto it = book_.find(price_tick);
        if (it == book_.end()) {return;}
        auto& level = it->second;
        const uint32_t oid = level.back().order_id;
        level.pop_back();
        index_.erase(oid);
        if (level.empty()) {book_.erase(it);}
    }
};

template <Order_Type Side, uint32_t MinPrice, uint32_t MaxPrice, uint32_t MaxOrderId>
class VectorOrderBook {
    static_assert(MinPrice <= MaxPrice, "invalid price range");
    static constexpr uint32_t kRange = MaxPrice - MinPrice + 1;
    static constexpr uint32_t kWordBits = 64;
    static constexpr uint32_t kNumWords = (kRange + kWordBits - 1) / kWordBits;
    static constexpr uint32_t kReservePerLevel = (MaxOrderId / kRange) + 1;

    struct Level {
        std::vector<Order> orders;
    };

    std::vector<Level> levels_{kRange};
    struct IndexSlot {
        info data{};
        bool used{false};
    };
    std::vector<IndexSlot> index_{MaxOrderId + 1};
    std::array<uint64_t, kNumWords> level_bits_{};
    bool has_best_{false};
    uint32_t best_price_{0};

public:
    VectorOrderBook() {
        for (auto& level : levels_) {
            level.orders.reserve(kReservePerLevel);
        }
    }

private:
    inline bool in_range(uint32_t price) const {
        return price >= MinPrice && price <= MaxPrice;
    }

    inline bool valid_order_id(uint32_t order_id) const {
        return order_id <= MaxOrderId;
    }

    inline uint32_t idx(uint32_t price) const {
        return price - MinPrice;
    }

    inline void set_level_bit(uint32_t price) {
        const uint32_t i = idx(price);
        level_bits_[i / kWordBits] |= (1ULL << (i % kWordBits));
    }

    inline void clear_level_bit(uint32_t price) {
        const uint32_t i = idx(price);
        level_bits_[i / kWordBits] &= ~(1ULL << (i % kWordBits));
    }

    inline void update_best_on_add(uint32_t price) {
        if (!has_best_) {
            best_price_ = price;
            has_best_ = true;
            return;
        }
        if constexpr (Side == Order_Type::Buy) {
            if (price > best_price_) { best_price_ = price; }
        } else {
            if (price < best_price_) { best_price_ = price; }
        }
    }

    inline bool find_best_from(uint32_t start) {
        if constexpr (Side == Order_Type::Buy) {
            uint32_t s = (start > MaxPrice) ? MaxPrice : start;
            uint32_t s_idx = idx(s);
            int32_t word = (int32_t)(s_idx / kWordBits);
            uint32_t bit = s_idx % kWordBits;
            uint64_t mask = (bit == 63) ? ~0ULL : ((1ULL << (bit + 1)) - 1);
            uint64_t w = level_bits_[(size_t)word] & mask;
            if (w != 0) {
                uint32_t pos = 63 - (uint32_t)__builtin_clzll(w);
                best_price_ = MinPrice + (uint32_t)word * kWordBits + pos;
                has_best_ = true;
                return true;
            }
            for (--word; word >= 0; --word) {
                w = level_bits_[(size_t)word];
                if (w != 0) {
                    uint32_t pos = 63 - (uint32_t)__builtin_clzll(w);
                    best_price_ = MinPrice + (uint32_t)word * kWordBits + pos;
                    has_best_ = true;
                    return true;
                }
            }
        } else {
            uint32_t s = (start < MinPrice) ? MinPrice : start;
            uint32_t s_idx = idx(s);
            uint32_t word = s_idx / kWordBits;
            uint32_t bit = s_idx % kWordBits;
            uint64_t mask = ~0ULL << bit;
            uint64_t w = level_bits_[word] & mask;
            if (w != 0) {
                uint32_t pos = (uint32_t)__builtin_ctzll(w);
                best_price_ = MinPrice + word * kWordBits + pos;
                has_best_ = true;
                return true;
            }
            for (++word; word < kNumWords; ++word) {
                w = level_bits_[word];
                if (w != 0) {
                    uint32_t pos = (uint32_t)__builtin_ctzll(w);
                    best_price_ = MinPrice + word * kWordBits + pos;
                    has_best_ = true;
                    return true;
                }
            }
        }
        has_best_ = false;
        return false;
    }

    inline void add_to_book(uint32_t order_id, uint32_t price_tick, uint32_t qty) {
        auto& level = levels_[idx(price_tick)].orders;
        const bool was_empty = level.empty();
        const uint32_t pos = (uint32_t)level.size();
        level.push_back(Order{order_id, qty});
        index_[order_id].data = info{price_tick, pos};
        index_[order_id].used = true;
        if (was_empty) {
            set_level_bit(price_tick);
        }
        update_best_on_add(price_tick);
    }

    inline void refresh_best_after_remove(uint32_t price_tick) {
        if (!has_best_ || price_tick != best_price_) { return; }
        if constexpr (Side == Order_Type::Buy) {
            if (price_tick > MinPrice) {
                find_best_from(price_tick - 1);
                return;
            }
        } else {
            if (price_tick < MaxPrice) {
                find_best_from(price_tick + 1);
                return;
            }
        }
        has_best_ = false;
    }

    public:

    inline void on_new_limit(uint32_t order_id, uint32_t price_tick, uint32_t qty) {
        if (!valid_order_id(order_id) || !in_range(price_tick)) { return; }
        add_to_book(order_id, price_tick, qty);
    }

    inline void on_cancel(uint32_t order_id) {
        if (!valid_order_id(order_id)) { return; }
        auto& slot = index_[order_id];
        if (!slot.used) { return; }

        const uint32_t price = slot.data.price_tick;
        if (!in_range(price)) { slot.used = false; return; }
        auto& level = levels_[idx(price)].orders;
        const uint32_t pos = slot.data.pos_in_level;

        const uint32_t last = (uint32_t)level.size() - 1;
        if (pos != last) {
            level[pos] = level[last];
            index_[level[pos].order_id].data = info{price, pos};
        }
        level.pop_back();
        slot.used = false;

        if (level.empty()) {
            clear_level_bit(price);
            refresh_best_after_remove(price);
        }
    }

    inline void on_modify(uint32_t order_id, uint32_t new_price_tick, uint32_t new_qty) {
        if (!valid_order_id(order_id)) { return; }
        auto& slot = index_[order_id];
        if (!slot.used) { return; }

        const uint32_t old_price = slot.data.price_tick;
        if (old_price == new_price_tick) {
            if (!in_range(old_price)) { return; }
            auto& level = levels_[idx(old_price)].orders;
            level[slot.data.pos_in_level].qty = new_qty;
            return;
        }

        on_cancel(order_id);
        if (in_range(new_price_tick)) {
            on_new_limit(order_id, new_price_tick, new_qty);
        }
    }

    inline const std::vector<Level>& raw_levels() const { return levels_; }

    inline bool best_price(uint32_t& out_price) {
        if (!has_best_) {
            const uint32_t start = (Side == Order_Type::Buy) ? MaxPrice : MinPrice;
            if (!find_best_from(start)) { return false; }
        }
        out_price = best_price_;
        return true;
    }

    inline Order* best_order(uint32_t& price_tick) {
        if (!best_price(price_tick)) { return nullptr; }
        auto& level = levels_[idx(price_tick)].orders;
        if (level.empty()) {
            if (!find_best_from(price_tick)) { return nullptr; }
        }
        price_tick = best_price_;
        return &levels_[idx(price_tick)].orders.back();
    }

    inline void remove_best(uint32_t price_tick) {
        if (!in_range(price_tick)) { return; }
        auto& level = levels_[idx(price_tick)].orders;
        if (level.empty()) { return; }
        const uint32_t oid = level.back().order_id;
        level.pop_back();
        if (valid_order_id(oid)) {
            index_[oid].used = false;
        }
        if (level.empty()) {
            clear_level_bit(price_tick);
            refresh_best_after_remove(price_tick);
        }
    }
};
