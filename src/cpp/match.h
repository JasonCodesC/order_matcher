#pragma once

#include "spsc_ring.h"
#include "../cpp_helpers/protocols.hpp"

static constexpr uint32_t RING_SIZE = 16384;
using OrderMsgRing = SpscRing<OrderMsg, ORDER_RING_SIZE>;

void match_loop(OrderMsgRing& ring);
