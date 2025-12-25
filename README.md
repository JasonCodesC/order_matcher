# Order Matching Engine

## Overview

I did this project in an effort to get myself more comfortable with advanced networking techniques (AF_XDP) and to also get better at profiling and finding where I can make speedups in my code. 


add note copy mode vs zero copy for bypass, zero wont work bc ubnutu VM


perf stat not supported on linux

perf record was bad for system-wide and user-space-only bc waiting dominated.


q tracking

## Results:


V1 stats:

| sec  | orders_per_sec | trades_per_sec | total_orders | total_trades |
|-----:|---------------:|---------------:|------------:|-------------:|
| 0.003 | 41600.000 | 30400.000 | 126 | 86 |
| 0.005 | 28400.000 | 24400.000 | 197 | 147 |
| 0.007 | 0.000 | 0.000 | 197 | 147 |
| 0.010 | 0.000 | 0.000 | 197 | 147 |
| 0.013 | 0.000 | 0.000 | 197 | 147 |
| 0.015 | 0.000 | 0.000 | 197 | 147 |
| 0.018 | 57200.000 | 45200.000 | 340 | 260 |
| 0.020 | 93200.000 | 70400.000 | 573 | 436 |
| 0.022 | 0.000 | 0.000 | 573 | 436 |
| 0.025 | 190000.000 | 136000.000 | 1048 | 776 |
| 0.028 | 380800.000 | 304400.000 | 2000 | 1537 |
| 0.030 | 0.000 | 0.000 | 2000 | 1537 |
| 0.033 | 0.000 | 0.000 | 2000 | 1537 |
| 0.035 | 0.000 | 0.000 | 2000 | 1537 |
| 0.037 | 0.000 | 0.000 | 2000 | 1537 |
| 0.040 | 0.000 | 0.000 | 2000 | 1537 |
| 0.043 | 0.000 | 0.000 | 2000 | 1537 |
| 0.045 | 0.000 | 0.000 | 2000 | 1537 |
| 0.048 | 0.000 | 0.000 | 2000 | 1537 |
| 0.050 | 0.000 | 0.000 | 2000 | 1537 |



V2:

The zeros in between massive trades/sec tell us this engine isn't dealing with compute-bound slowdowns i.e. are matching logic is pretty solid. So to improve upon V1 we will try and smooth out the input to reduce idle gaps. We will make our rings/buffers bigger to absorb burts of orders and will use a hybrid backoff so that idle spin is reduced. 


| sec  | orders_per_sec | trades_per_sec | total_orders | total_trades |
|-----:|---------------:|---------------:|------------:|-------------:|
| 0.003 | 14400.000 | 8400.000 | 120 | 77 |
| 0.005 | 11600.000 | 8400.000 | 149 | 98 |
| 0.007 | 26000.000 | 19200.000 | 214 | 146 |
| 0.010 | 99200.000 | 78800.000 | 462 | 343 |
| 0.013 | 98000.000 | 80000.000 | 707 | 543 |
| 0.015 | 68800.000 | 52800.000 | 879 | 675 |
| 0.018 | 110800.000 | 87600.000 | 1156 | 894 |
| 0.020 | 59600.000 | 46800.000 | 1305 | 1011 |
| 0.022 | 68800.000 | 50800.000 | 1477 | 1138 |
| 0.025 | 62000.000 | 47600.000 | 1632 | 1257 |
| 0.028 | 60400.000 | 49600.000 | 1783 | 1381 |
| 0.030 | 70000.000 | 51200.000 | 1958 | 1509 |
| 0.033 | 16800.000 | 12000.000 | 2000 | 1539 |
| 0.035 | 0.000 | 0.000 | 2000 | 1539 |
| 0.037 | 0.000 | 0.000 | 2000 | 1539 |
| 0.040 | 0.000 | 0.000 | 2000 | 1539 |
| 0.043 | 0.000 | 0.000 | 2000 | 1539 |
| 0.045 | 0.000 | 0.000 | 2000 | 1539 |
| 0.048 | 0.000 | 0.000 | 2000 | 1539 |
| 0.050 | 0.000 | 0.000 | 2000 | 1539 |


V3:

Now that we arent dealing with the 0s lets try a different container, std::vector.


| sec  | orders_per_sec | trades_per_sec | total_orders | total_trades |
|-----:|---------------:|---------------:|------------:|-------------:|
| 0.003 | 0.000 | 0.000 | 346 | 229 |
| 0.005 | 6400.000 | 6000.000 | 362 | 244 |
| 0.007 | 0.000 | 0.000 | 362 | 244 |
| 0.010 | 19200.000 | 15600.000 | 410 | 283 |
| 0.013 | 70800.000 | 59200.000 | 587 | 431 |
| 0.015 | 114800.000 | 96400.000 | 874 | 672 |
| 0.018 | 93200.000 | 74800.000 | 1107 | 859 |
| 0.020 | 62400.000 | 51200.000 | 1263 | 987 |
| 0.022 | 70800.000 | 42400.000 | 1440 | 1093 |
| 0.025 | 65200.000 | 55600.000 | 1603 | 1232 |
| 0.028 | 73600.000 | 50800.000 | 1787 | 1359 |
| 0.030 | 50800.000 | 50000.000 | 1914 | 1484 |
| 0.033 | 34400.000 | 21200.000 | 2000 | 1537 |
| 0.035 | 0.000 | 0.000 | 2000 | 1537 |
| 0.037 | 0.000 | 0.000 | 2000 | 1537 |
| 0.040 | 0.000 | 0.000 | 2000 | 1537 |
| 0.043 | 0.000 | 0.000 | 2000 | 1537 |
| 0.045 | 0.000 | 0.000 | 2000 | 1537 |
| 0.048 | 0.000 | 0.000 | 2000 | 1537 |
| 0.050 | 0.000 | 0.000 | 2000 | 1537 |

V4:

Use a bitset to track which price levels are non-empty and jump to the next best level with bit-scan ops.

- ctz (count trailing zeros): finds the index of the lowest set bit.
- bsr (bit scan reverse): finds the index of the highest set bit.

Also replaced the order_id -> info lookup from unordered_map to a flat array for faster, cache-friendly access.

