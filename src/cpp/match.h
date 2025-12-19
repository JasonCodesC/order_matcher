#pragma once

#include "spsc_ring.h"
#include "../cpp_helpers/protocols.hpp"

static constexpr uint32_t ORDER_RING_SIZE = 16384;
static constexpr uint32_t TRADE_RING_SIZE = 16384;
using OrderMsgRing = SpscRing<OrderMsg, ORDER_RING_SIZE>;
using TradeMsgRing = SpscRing<TradeMsg, TRADE_RING_SIZE>;

void match_loop(OrderMsgRing& ring, TradeMsgRing& trades);
