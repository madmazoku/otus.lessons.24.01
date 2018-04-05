#pragma once

#include <string>
#include <thread>
#include <mutex>

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <tuple>
#include <ctime>
#include <list>

#include "pipe.h"

#include "processor.h"

class TCPConnection : public std::enable_shared_from_this<TCPConnection>
{
private:
    boost::asio::ip::tcp::socket _socket;

    Reader _reader;

    char* _buffer;
    size_t _buffer_size;

public:
    TCPConnection(boost::asio::ip::tcp::socket socket, size_t buffer_size = 10, size_t max_reader_buffer_size = 10) :
        _socket(std::move(socket)),
        _reader(max_reader_buffer_size),
        _buffer(new char[buffer_size]),
        _buffer_size(buffer_size)
    {
    }

    ~TCPConnection()
    {
        delete[] _buffer;
    }

    void attach(Pipe<MixerRecord>& mixer)
    {
        _reader.attach(mixer);
    }

    void detach()
    {
        _reader.detach();
    }

    void start()
    {
        _reader.run();

        do_read();
    }

private:
    void do_read()
    {
        auto self(shared_from_this());
        _socket.async_read_some(
            boost::asio::buffer(_buffer, _buffer_size),
        [this, self](boost::system::error_code ec, std::size_t length) {
            if(!ec) {
                Metrics::get().update("connection.buffer.count", 1);
                Metrics::get().update("connection.buffer.size", length);

                std::string buffer{_buffer, length};
                _reader.put(std::string{_buffer, length});
                do_read();
            } else if ((boost::asio::error::eof == ec) || (boost::asio::error::connection_reset == ec)) {
                // std::cerr << "disconnect: " << this << std::endl;
                _reader.join();
                _reader.detach();
                // std::cerr << "disconnected: " << this << std::endl;
            } else {
                // std::cerr << "read error[" << this << ": " << ec << std::endl;
            }
        }
        );
    }
};