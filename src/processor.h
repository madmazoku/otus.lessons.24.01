#pragma once

#include <string>
#include <tuple>
#include <list>
#include <iostream>

#include "pipe.h"
#include "channel.h"

using Command = std::tuple<time_t, std::string>;
using Commands = std::list<Command>;

std::ostream& operator<<(std::ostream& out, const Command& command)
{
    out << " {" << std::get<0>(command) << ", " << std::get<1>(command) << "}";
    return out;
}

std::ostream& operator<<(std::ostream& out, const Commands& commands)
{
    bool first = true;
    out << " [";
    for(auto& c : commands) {
        if(first)
            first = false;
        else
            out << "; ";
        out << c;
    }
    out << " ]";
    return out;
}

class Reader : public Channel<std::string> {
private:

    Pipe<Command>* _mixer;
    Pipe<Commands>* _distributor;

    Commands _commands;
    std::string _data;
    size_t _bracket_counter;

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
    }

    void process_line(const std::string& line)
    {
        if(line == "{") {
            ++_bracket_counter;
        } else if(line == "}") {
            if(_bracket_counter > 0 && --_bracket_counter == 0)
                distribute();
        } else if(_bracket_counter > 0) {
            _commands.push_back(std::make_tuple(std::time(nullptr), line));
        } else {
            mix(std::make_tuple(std::time(nullptr), line));
        }
    }

    void distribute() {
        std::cout << "distribute: " << _commands << std::endl;
        _distributor->put(_commands);
        _commands.clear();
    }

    void mix(const Command& command) {
        std::cout << "mix: " << command << std::endl;
        _mixer->put(command);
    }

    virtual void act(size_t n) override {
        std::string buffer;
        std::cout << "reader act started" << std::endl;
        while(get(buffer)) {
            std::cout << "reader act: '" << buffer << "'" << std::endl;
            _data.append(buffer);
            process_data();
        }
        _commands.clear();
    }

public:
    Reader(size_t max_buffer_size = 10) noexcept 
          : Channel<std::string>(max_buffer_size),
          _mixer(nullptr),
          _distributor(nullptr),
          _bracket_counter(0)
    {
    }
    virtual ~Reader() = default;

    void attach(Pipe<Command>& mixer, Pipe<Commands>& distributor) {
        _mixer = &mixer;
        _distributor = &distributor;
    }

    void detach() {
        _mixer = nullptr;
        _distributor = nullptr;
    }

};

class Mixer : public Channel<Command> {
private:
    size_t _N;
    Pipe<Commands>* _distributor;

    virtual void act(size_t n) final {
        Command command;
        Commands commands;
        while(get(command)) {
            std::cout << "mixer read: " << command << std::endl;
            commands.push_back(command);
            if(_N != 0 && commands.size() == _N) {
                _distributor->put(commands);
                commands.clear();
            }
        }
        if(!commands.empty())
            _distributor->put(commands);
    }

public:
    Mixer(size_t N, size_t max_buffer_size = 10) noexcept : Channel<Command>(max_buffer_size), _N(N) {
    }

    void attach(Pipe<Commands>& distributor) {
        _distributor = &distributor;
    }

    void detach() {
        _distributor = nullptr;
    }
};


class Processor : public Channel<Commands> {
private:
    virtual void act(size_t n) override {
        Commands commands;
        while(get(commands))
            process(n, commands);
    }

    virtual void process(size_t n, const Commands& commands) = 0;

public:
    Processor(size_t max_buffer_size = 10) noexcept : Channel<Commands>(max_buffer_size) {}
    virtual ~Processor() = default;

};

class Distributor : public Processor {
private:
    std::list<Pipe<Commands>*> _subscribers;

    virtual void process(size_t n, const Commands& commands) {
        std::cout << "distributor process: " << commands << std::endl;
        if(commands.empty())
            return;
        for(auto s : _subscribers)
            s->put(commands);
    }

public:
    Distributor(size_t max_buffer_size = 10) noexcept : Processor(max_buffer_size) {
    }

    void attach(Pipe<Commands>& subscriber) {
        _subscribers.push_back(&subscriber);
    }

    void detach() {
        _subscribers.clear();
    }

};

#ifdef COMMENT

class StreamPrint : public Processor
{
private:
    std::ostream& _out;
    std::mutex _out_mutex;

    virtual void process(size_t n, const Commands& commands) final {
        if(commands.size() == 0)
            return;

        std::lock_guard out_lock(_out_mutex);
        _out << "bulk: ";
        bool first = true;
        for(auto c :commands)
        {
            if(first)
                first = false;
            else
                std::cout << ", ";
            std::cout << std::get<1>(c);
        }
        std::cout << std::endl;
    }

public:
    StreamPrint(std::ostream& out, size_t max_buffer_size = 10) noexcept : Processor(max_buffer_size), _out(out) {
    }

    virtual ~StreamPrint() = default;
};

class FilePrint : public Processor
{
private:
    std::map<time_t, size_t> _log_counter;
    std::mutex _log_counter_mutex;

    virtual void process(size_t n, const Commands& commands) final {
        if(commands.size() == 0)
            return;

        std::string name = "bulk";
        time_t tm = std::get<0>(*(commands.begin()));
        name += boost::lexical_cast<std::string>(tm);
        name += "-";

        size_t log_counter = 0;
        {
            std::lock_guard<std::mutex> log_counter_lock(_log_counter_mutex);
            auto it_cnt = _log_counter.find(tm);
            if(it_cnt != _log_counter.end())
                log_counter = ++(it_cnt->second);
            else
                _log_counter[tm] = log_counter;
        }
        std::string cnt = std::to_string(log_counter);

        std::ofstream out(name + cnt + ".log");
        for(auto c :commands)
            out << std::get<1>(c) << std::endl;
        out.close();
    }

public:
    FilePrint(size_t max_buffer_size = 10) : Processor(max_buffer_size) { }
    virtual ~FilePrint() = default;
};

#endif