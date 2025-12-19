

#include <sys/types.h>
#include <sys/socket.h>
#include <random>
#include <ctime>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <iostream>

// NEED TO POPULATE BUFFER WITH TEST DATA

void send() {
    std::mt19937 engine(static_cast<unsigned int>(std::time(nullptr))); //Send info at random times
    int min = 2;
    int max = 10;
    std::uniform_int_distribution<int> dist(min, max);

    int fd = socket(AF_INET, SOCK_DGRAM, 0); //UDP socket
    if (fd == -1) {
        std::cerr << "WTF";
        exit(1);
    }
    const void* buf;
    size_t sent = 0;
    while (sent < sizeof(buf)) {
        sent += sendto(fd, buf, sizeof(buf), 0, 0, 0);
        int rand_num = dist(engine);
        std::this_thread::sleep_for(std::chrono::milliseconds(rand_num)); //Simulate jitter in the network
    }
    close(fd);
}


int main() {send();}