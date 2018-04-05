
#include <iostream>
#include <exception>

#include <boost/asio.hpp>

#include "../bin/version.h"

#include "tcp_server.h"
#include "metrics.h"

int main(int argc, char** argv)
{
    try {
        if(argc != 3) {
            std::cerr << "Usage: " << argv[0] << " <port> <bulk_size>" << std::endl;
            return 1;
        }

        boost::asio::io_service io;

        TCPServer srv(io, std::atoi(argv[1]), std::atoi(argv[2]));
        StreamPrint sp(std::cout);
        FilePrint fp;

        srv.attach(sp);
        srv.attach(fp);

        sp.run();
        fp.run(2);

        io.run();

        srv.done();

        sp.join();
        fp.join();

        Metrics::get().dump();

    } catch(std::exception& e) {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}
