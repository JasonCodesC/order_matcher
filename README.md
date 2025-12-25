# Order Matching Engine

## Overview

I did this project in an effort to get myself more comfortable with advanced networking techniques (AF_XDP) and to also get better at profiling and finding where I can make speedups in my code. 


add note copy mode vs zero copy for bypass, zero wont work bc ubnutu VM


perf stat not supported on linux

perf record was bad for system-wide and user-space-only bc waiting dominated.


q tracking

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