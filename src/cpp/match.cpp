#include "match.h"
#include "book_types.h"

void match_loop(OrderMsgRing& ring, TradeMsgRing& trades, std::atomic<bool>& running,
        std::atomic<uint64_t>& trades_total) {

    Books book;
    SpinWait ring_wait;
    SpinWait trade_wait;

    while (running.load(std::memory_order_acquire)) {
        OrderMsg* slot = nullptr;
        while (!ring.try_acquire_consumer_slot(slot)) {
            if (!running.load(std::memory_order_acquire)) { return; }
            ring_wait.pause();
        }
        ring_wait.reset();
        OrderMsg& msg = *slot;
        const bool taker_is_buy = (msg.side == Order_Type::Buy);

        switch (msg.msg_type) {
            case MsgType::NewLimit:
                if (msg.side == Order_Type::Buy) {
                    book.bids.on_new_limit(msg.order_id, msg.price_tick, msg.qty);
                } 
                else {
                    book.asks.on_new_limit(msg.order_id, msg.price_tick, msg.qty);
                }
                break;

            case MsgType::Cancel:
                if (msg.side == Order_Type::Buy) {
                    book.bids.on_cancel(msg.order_id);
                } 
                else {
                    book.asks.on_cancel(msg.order_id);
                }
                break;

            case MsgType::Modify:
                if (msg.side == Order_Type::Buy) {
                    book.bids.on_modify(msg.order_id, msg.price_tick, msg.qty);
                } 
                else {
                    book.asks.on_modify(msg.order_id, msg.price_tick, msg.qty);
                }
                break;

            default:
                break;
        }

        // match crossing book
        uint32_t best_bid_price, best_ask_price;
        while (book.bids.best_price(best_bid_price) && book.asks.best_price(best_ask_price)
               && best_bid_price >= best_ask_price) {

            uint32_t bid_px, ask_px;
            Order* bid_o = book.bids.best_order(bid_px);
            Order* ask_o = book.asks.best_order(ask_px);

            const uint32_t trade_qty = (bid_o->qty < ask_o->qty) ? bid_o->qty : ask_o->qty;
            const uint32_t trade_px = taker_is_buy ? ask_px : bid_px;

            // emit trade
            TradeMsg* tslot = nullptr;
            while (!trades.try_acquire_producer_slot(tslot)) {
                if (!running.load(std::memory_order_acquire)) { return; }
                trade_wait.pause();
            }
            trade_wait.reset();
            tslot->bid_order_id = bid_o->order_id;
            tslot->ask_order_id = ask_o->order_id;
            tslot->price_tick = trade_px;
            tslot->qty = trade_qty;
            trades.commit_producer_slot();
            trades_total.fetch_add(1, std::memory_order_relaxed);

            // apply fills
            bid_o->qty -= trade_qty;
            ask_o->qty -= trade_qty;

            if (bid_o->qty == 0) { book.bids.remove_best(bid_px); }
            if (ask_o->qty == 0) { book.asks.remove_best(ask_px); }
        }

        ring.release_consumer_slot();
    }
}
