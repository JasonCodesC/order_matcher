#pragma once

#include "order_book.h"

static constexpr uint32_t PRICE_MIN = 5000;
static constexpr uint32_t PRICE_MAX = 15000;
static constexpr uint32_t MAX_ORDER_ID = 200000; // update if order ids exceed this

// Prior std::map-based books
// #include <functional>
// #include <map>
// #include <vector>
// using BidLevels = std::map<uint32_t, std::vector<Order>, std::greater<uint32_t>>;
// using AskLevels = std::map<uint32_t, std::vector<Order>, std::less<uint32_t>>;
// using BidBook = OrderBook<Order_Type::Buy,  BidLevels>;
// using AskBook = OrderBook<Order_Type::Sell, AskLevels>;

using BidBook = VectorOrderBook<Order_Type::Buy, PRICE_MIN, PRICE_MAX, MAX_ORDER_ID>;
using AskBook = VectorOrderBook<Order_Type::Sell, PRICE_MIN, PRICE_MAX, MAX_ORDER_ID>;

struct Books {
  BidBook bids;
  AskBook asks;
};
