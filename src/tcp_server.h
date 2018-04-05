#pragma once

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/asio/signal_set.hpp>

#include "tcp_connection.h"
#include "processor.h"

class TCPServer
{
private:
    boost::asio::ip::tcp::socket _socket;
    boost::asio::ip::tcp::acceptor _acceptor;

    boost::asio::signal_set _sigint;

    Mixer _mixer;
    Distributor _distributor;

public:
    TCPServer(boost::asio::io_service& io, short port, size_t N, size_t max_buffer_size = 10)
        : _socket(io),
          _acceptor(io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
          _sigint(io, SIGINT),
          _mixer(N, max_buffer_size),
          _distributor(max_buffer_size)
    {
        start();
    }

    void attach(Pipe<Commands>& subscriber)
    {
        _distributor.attach(subscriber);
    }

    void detach()
    {
        _distributor.detach();
    }

    void start()
    {
        _mixer.attach(_distributor);

        _mixer.run();
        _distributor.run();

        _sigint.async_wait([this](boost::system::error_code ec, int signal) {
            if(!ec) {
                // std::cerr << "signal: " << signal << std::endl;
            } else {
                // std::cerr << "signal error: " << ec << std::endl;
            }
            _acceptor.close();
        }
                          );
        do_accept();
    }

    void done()
    {
        _mixer.join();
        _mixer.detach();

        _distributor.join();
        _distributor.detach();
    }

private:

    void do_accept()
    {
        _acceptor.async_accept(_socket, [this](boost::system::error_code ec) {
            if(!ec) {
                Metrics::get().update("server.connect.count", 1);

                auto new_connection = std::make_shared<TCPConnection>(std::move(_socket));
                new_connection->attach(_mixer, _distributor);
                new_connection->start();
                do_accept();
            } else if (boost::asio::error::operation_aborted == ec) {
                // std::cerr << "close server" << std::endl;
            } else {
                // std::cerr << "accept error: " << ec << std::endl;
            }
        }
                              );
    }
};