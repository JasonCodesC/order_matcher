CC      := clang
CXX     := g++
CFLAGS  := -O3 -g -Wall -Wextra -std=c11
BPF_CFLAGS := -O3 -g -Wall -Wextra -std=gnu11
CXXFLAGS:= -O3 -g -Wall -Wextra -std=c++17 -march=native -pthread
INCLUDES:= -Isrc/cpp -Isrc -Isrc/cpp_helpers
LDFLAGS := -lxdp -lbpf -lelf -pthread

BPF_OBJ := xdp_kernal.o
ENGINE  := xdp_recv
SENDER  := send_to_engine

.PHONY: all clean

all: $(BPF_OBJ) $(ENGINE) $(SENDER)

$(BPF_OBJ): src/cpp/xdp_kernal.c
	$(CC) $(BPF_CFLAGS) -target bpf -c $< -o $@

$(ENGINE): src/cpp/xdp_recv.cpp src/cpp/match.cpp $(BPF_OBJ)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

$(SENDER): src/cpp/send_to_engine.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ -pthread

clean:
	rm -f $(BPF_OBJ) $(ENGINE) $(SENDER)
