#include "match.h"
#include "book_types.h"

void match_loop(OrderMsgRing& ring) {
    Books book;

    while (true) {
        OrderMsg msg;
        if (!ring.try_pop(msg)) {
            continue;
        }

        switch (msg.msg_type) {
            case MsgType::NewLimit:
                if (msg.side == Order_Type::Buy) {
                    book.bids.on_new_limit(msg.order_id, msg.price_tick, msg.qty);
                } else {
                    book.asks.on_new_limit(msg.order_id, msg.price_tick, msg.qty);
                }
                break;

            case MsgType::Cancel:
                if (msg.side == Order_Type::Buy) {
                    book.bids.on_cancel(msg.order_id);
                } else {
                    book.asks.on_cancel(msg.order_id);
                }
            break;

            case MsgType::Modify:
                if (msg.side == Order_Type::Buy) {
                    book.bids.on_modify(msg.order_id, msg.price_tick, msg.qty);
                } else {
                    book.asks.on_modify(msg.order_id, msg.price_tick, msg.qty);
                }
            break;

            default:
            break;
        }
    }
}
