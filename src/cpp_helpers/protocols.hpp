#include <cstdint>
#include <type_traits>

enum class MsgType : uint8_t { NewLimit = 1, Cancel=2, Modify=3};
enum class Order_Type : uint8_t { Sell = 0, Buy = 1 }; //uint8 bc may support more stuff in the future

#pragma pack(push, 1)
struct Packet {
  uint32_t seq_num;     // for dupes
  uint32_t order_id;    // unique id 
  uint32_t price_tick;  // 1 => $0.01 so 10123 = $101.23
  uint32_t qty;         // qty
  MsgType msg_type;     // New limit, cancel or modify
  Order_Type side;      // Sell, Buy
};
#pragma pack(pop)

static_assert(sizeof(Packet) == 18);
