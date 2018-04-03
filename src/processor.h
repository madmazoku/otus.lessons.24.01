#pragma once

#include <string>
#include <tuple>
#include <queue>
#include <iostream>

#include "pipe.h"

using Command = std::tuple<time_t, std::string>;
using Commands = std::queue<Command>;

std::ostream& operator<<(std::ostream& out, const Command& command)
{
    out << " {" << std::get<0>(command) << ", " << std::get<1>(command) << "}";
    return out;
}

template<typename T>
class Distributor {
private:
    std::queue<Pipe<T>&> _subscribers;

public:

    void attach(Pipe<T>& subscriber) {
        _subscribers.push_back(subscriber);
    }

    void promote(Distributor<T>& next) {
        for(auto& s : _subscribers)
            next->attach(s);
    }

    void send(const T& t) {
        for(auto& s : _subscribers)
            s.put(t);
    }
};

using CommandsDistributor = Distributor<Commands>;

class Mixer : Distributor<Commands> {
private:
    size_t _N;
    Commands _commands;

public:
    void push(const Commands& commands) {
        for(auto c : commands) {
            _commands.push_back(c);
            if(_commands.size() == _N) {
                send(_commands);
                _commands.clear();
            }
        }
    }
};