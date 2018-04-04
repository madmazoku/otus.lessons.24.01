#pragma once

#include <thread>
#include <queue>

#include "pipe.h"

template<typename T>
class Channel : public Pipe<T> {
private:
    std::queue<std::thread> _threads;

    virtual void act(size_t n) = 0;

public:
    Channel(size_t max_buffer_size = 10) noexcept : Pipe<T>(max_buffer_size) {
    }
    Channel(const Channel<T>&) = delete;
    Channel(Channel<T>&&) = default;

    Channel<T>& operator=(const Channel<T>&) = delete;

    virtual ~Channel() = default;

    void run(size_t threads_size = 1) {
        if(threads_size == 0)
            threads_size = std::thread::hardware_concurrency();
        if(threads_size == 0)
            threads_size = 1;
        std::cout << "run channel: " << threads_size << std::endl;
        for(size_t n = 0; n < threads_size; ++n) {
            std::cout << "add thread: " << n << std::endl;
            _threads.push(std::thread([this, n]{
                std::cout << "thread added: " << n << std::endl;
                act(n);
            }));
        }
    }

    void join() {
        while(!_threads.empty()) {
            _threads.front().join();
            _threads.pop();
        }
    }

};
