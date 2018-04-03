#pragma once

#include <string>
#include <thread>
#include <mutex>

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "processor.h"

class TCPConnection : public std::enable_shared_from_this<TCPConnection>, CommandsDistributor {
private:
    boost::asio::ip::tcp::socket _socket;

    char* _buffer;
    size_t _buffer_size;

    Pipe<std::string> _buffers;

    std::queue<Pipes<Commands>&> _subscribers;

    std::string _data;
    Commands _commands;
    size_t _bracket_counter;

public:
    TCPConnection(boost::asio::ip::tcp::socket socket, size_t buffer_size = 10, size_t pipe_size = 10) : 
            _socket(std::move(socket)), 
            _buffer(new char[buffer_size]), 
            _buffer_size(buffer_size),
            _buffers(pipe_size),
            _bracket_counter(0)
    {
    }

    ~TCPConnection() {
        delete[] _buffer;
    }

    void attach(Pipes<std::string>& subscriber) {
        _subscribers.push_back(subscriber);
    }

    void start() 
    {
        _thread = std::thread([this](){
            std::string buffer;
            while(_buffers.get(buffer)) {
                _data.append(buffer);
                process_data();
            }
        });
        do_read();
    }

private:
    void process_data() {
        size_t start_pos = 0;
        while(true)
        {
            size_t end_pos = _data.find('\n', start_pos);
            if(end_pos != std::string::npos) {
                process_line(_data.substr(start_pos, end_pos - start_pos));
                start_pos = end_pos;
                ++start_pos;
            } else {
                _data.erase(0, start_pos);
                break;
            }
        }
        if(_bracket_counter == 0)
            flush();
    }

    void process_line(const std::string& line)
    {
        if(line == "{") {
            if(_bracket_counter++ == 0)
                flush();
        } else if(line == "}") {
            if(_bracket_counter > 0 && --_bracket_counter == 0)
                flush();
        } else {
            _commands.push_back(std::make_tuple(std::time(nullptr), line));
        }
    }

    void flush() {
        if(_commands.empty())
            return;

        std::cout << _commands << std::endl;;
    }


    void do_read()
    {
        auto self(shared_from_this());
        _socket.async_read_some(
            boost::asio::buffer(_buffer, _buffer_size),
            [this, self](boost::system::error_code ec, std::size_t length)
            {
                if(!ec) {
                    _buffers.put(std::string{_buffer, _buffer_size});
                    do_read();
                } else if ((boost::asio::error::eof == ec) || (boost::asio::error::connection_reset == ec)) {
                    _buffers.finish();
                    _thread.join();
                    flush();
                    _subscribers.clear();
                } else {
                    std::cout << "read error: " << ec << std::endl;
                }
            }
        );
    }
};