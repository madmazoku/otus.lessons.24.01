#pragma once

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "tcp_connection.h"

class TCPServer {
private:
  boost::asio::ip::tcp::socket _socket;
  boost::asio::ip::tcp::acceptor _acceptor;

public:
    TCPServer(boost::asio::io_service& io, short port) : _socket(io), _acceptor(io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) 
    {
        do_accept();
    }

private:
    void do_accept() 
    {
        _acceptor.async_accept(_socket, [this](boost::system::error_code ec)
            {
                if(!ec) {
                    std::make_shared<TCPConnection>(std::move(_socket))->start();
                } else {
                    std::cout << "accept error: " << ec << std::endl;
                }
                do_accept();
            }
        );
    }
};