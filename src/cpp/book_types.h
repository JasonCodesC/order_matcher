#pragma once

#include "order_book.h"
#include <functional>
#include <map>
#include <vector>

using BidLevels = std::map<uint32_t, std::vector<Order>, std::greater<uint32_t>>;
using AskLevels = std::map<uint32_t, std::vector<Order>, std::less<uint32_t>>;

using BidBook = OrderBook<Order_Type::Buy,  BidLevels>;
using AskBook = OrderBook<Order_Type::Sell, AskLevels>;

struct Books {
  BidBook bids;
  AskBook asks;
};
