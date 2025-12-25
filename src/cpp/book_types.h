#pragma once

#include "order_book.h"

static constexpr uint32_t PRICE_MIN = 5000;
static constexpr uint32_t PRICE_MAX = 15000;

// Prior std::map-based books (kept for easy comparison).
// #include <functional>
// #include <map>
// #include <vector>
// using BidLevels = std::map<uint32_t, std::vector<Order>, std::greater<uint32_t>>;
// using AskLevels = std::map<uint32_t, std::vector<Order>, std::less<uint32_t>>;
// using BidBook = OrderBook<Order_Type::Buy,  BidLevels>;
// using AskBook = OrderBook<Order_Type::Sell, AskLevels>;

using BidBook = VectorOrderBook<Order_Type::Buy, PRICE_MIN, PRICE_MAX>;
using AskBook = VectorOrderBook<Order_Type::Sell, PRICE_MIN, PRICE_MAX>;

struct Books {
  BidBook bids;
  AskBook asks;
};
