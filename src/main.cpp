
#include <iostream>
#include <exception>

#include <boost/asio.hpp>

#include "../bin/version.h"

#include "tcp_server.h"

int main(int argc, char** argv)
{
    try {
        if(argc != 2) {
            std::cerr << "Usage: " << argv[0] << " port" << std::endl;
            return 1;
        }

        boost::asio::io_service io;

        TCPServer srv(io, std::atoi(argv[1]));

        io.run();

        srv.done();
    } catch(std::exception& e) {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}
