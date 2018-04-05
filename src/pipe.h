#pragma once

#include <mutex>
#include <condition_variable>
#include <queue>
#include <exception>

template<typename T>
class Pipe
{
public:
    using Ts = std::queue<T>;

private:
    Ts _ts;
    bool _eof;
    size_t _max_buffer_size;

    std::mutex _ts_mutex;
    std::condition_variable _ts_cv;

    size_t _put_count;
    size_t _get_count;

public:
    Pipe(size_t max_buffer_size = 10) noexcept : _eof(false), _max_buffer_size(max_buffer_size), _put_count(0), _get_count(0)
    {
    }
    Pipe(const Pipe<T>&) = delete;
    Pipe(Pipe<T>&&) = default;

    Pipe<T>& operator=(const Pipe<T>&) = delete;

    virtual ~Pipe() = default;

    void start()
    {
        std::lock_guard<std::mutex>  lock_ts(_ts_mutex);
        _eof = false;
    }

    void finish()
    {
        std::lock_guard<std::mutex>  lock_ts(_ts_mutex);
        _eof = true;
        _ts_cv.notify_all();
    }

    size_t size()
    {
        std::lock_guard<std::mutex>  lock_ts(_ts_mutex);
        return _ts.size();
    }

    size_t put_count()
    {
        std::lock_guard<std::mutex>  lock_ts(_ts_mutex);
        return _put_count;
    }

    size_t get_count()
    {
        std::lock_guard<std::mutex>  lock_ts(_ts_mutex);
        return _get_count;
    }

    void put(const T& t)
    {
        std::unique_lock<std::mutex>  lock_ts(_ts_mutex);
        if(_eof)
            throw std::runtime_error("attempt to put data to closed pipe");

        _ts_cv.wait(lock_ts, [this]() {
            return _max_buffer_size == 0 || _ts.size() < _max_buffer_size;
        });

        _ts.push(t);
        ++_put_count;
        _ts_cv.notify_all();
    }

    bool get(T& t)
    {
        std::unique_lock<std::mutex>  lock_ts(_ts_mutex);

        _ts_cv.wait(lock_ts, [this]() {
            return _eof || !_ts.empty();
        });

        if(_ts.empty())
            return false;

        t = std::move(_ts.front());
        _ts.pop();
        ++_get_count;
        _ts_cv.notify_all();

        return true;
    }
};
