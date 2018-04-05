#pragma once

#include <string>
#include <tuple>
#include <list>
#include <iostream>
#include <fstream>

#include "pipe.h"
#include "channel.h"
#include "metrics.h"

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

class Reader : public Channel<std::string>
{
private:

    Pipe<Command>* _mixer;
    Pipe<Commands>* _distributor;

    Commands _commands;
    std::string _data;
    size_t _bracket_counter;

    void process_data()
    {
        size_t start_pos = 0;
        while(true) {
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
        Metrics::get().update("reader.line.count", 1);
        Metrics::get().update("reader.line.size", line.size());

        if(line == "{") {
            if(_bracket_counter++ == 0) {
                mix(std::make_tuple(0, ""));
            }
        } else if(line == "}") {
            if(_bracket_counter > 0 && --_bracket_counter == 0)
                distribute();
        } else if(_bracket_counter > 0) {
            _commands.push_back(std::make_tuple(std::time(nullptr), line));
        } else {
            mix(std::make_tuple(std::time(nullptr), line));
        }
    }

    void distribute()
    {
        Metrics::get().update("reader.distribute.blocks", 1);
        Metrics::get().update("reader.distribute.commands", _commands.size());
        _distributor->put(_commands);
        _commands.clear();
    }

    void mix(const Command& command)
    {
        if(std::get<0>(command) != 0)
            Metrics::get().update("reader.mix.commands", 1);
        else
            Metrics::get().update("reader.mix.commands_util", 1);
        _mixer->put(command);
    }

    virtual void act(size_t n) override
    {
        std::string buffer;
        while(get(buffer)) {
            _data.append(buffer);
            process_data();
        }
        _commands.clear();
        mix(std::make_tuple(0, ""));
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

    void attach(Pipe<Command>& mixer, Pipe<Commands>& distributor)
    {
        _mixer = &mixer;
        _distributor = &distributor;
    }

    void detach()
    {
        _mixer = nullptr;
        _distributor = nullptr;
    }

};

class Mixer : public Channel<Command>
{
private:
    size_t _N;
    Pipe<Commands>* _distributor;

    virtual void act(size_t n) final {
        Command command;
        Commands commands;
        while(get(command))
        {
            if(std::get<0>(command) != 0) {
                commands.push_back(command);
                Metrics::get().update("mixer.receive.commands", 1);
            } else
                Metrics::get().update("mixer.receive.commands_util", 1);

            if((_N != 0 && commands.size() == _N) || (std::get<0>(command) == 0 && commands.size() > 0)) {
                _distributor->put(commands);
                commands.clear();
            }
        }
        if(!commands.empty())
            _distributor->put(commands);
    }

    void distribute(const Commands& commands)
    {
        Metrics::get().update("mixer.send.blocks", 1);
        Metrics::get().update("mixer.send.commands", commands.size());
        _distributor->put(commands);
    }

public:
    Mixer(size_t N, size_t max_buffer_size = 10) noexcept : Channel<Command>(max_buffer_size), _N(N)
    {
    }

    void attach(Pipe<Commands>& distributor)
    {
        _distributor = &distributor;
    }

    void detach()
    {
        _distributor = nullptr;
    }
};


class Processor : public Channel<Commands>
{
private:
    virtual void act(size_t n) override
    {
        Commands commands;
        while(get(commands))
            process(n, commands);
    }

    virtual void process(size_t n, const Commands& commands) = 0;

public:
    Processor(size_t max_buffer_size = 10) noexcept : Channel<Commands>(max_buffer_size) {}
    virtual ~Processor() = default;

};

class Distributor : public Processor
{
private:
    std::list<Pipe<Commands>*> _subscribers;

    virtual void process(size_t n, const Commands& commands)
    {
        if(commands.empty())
            return;

        Metrics::get().update("distributor.blocks", 1);
        Metrics::get().update("distributor.commands", commands.size());

        for(auto s : _subscribers)
            s->put(commands);
    }

public:
    Distributor(size_t max_buffer_size = 10) noexcept : Processor(max_buffer_size)
    {
    }

    void attach(Pipe<Commands>& subscriber)
    {
        _subscribers.push_back(&subscriber);
    }

    void detach()
    {
        _subscribers.clear();
    }

};

class StreamPrint : public Processor
{
private:
    std::ostream& _out;
    std::mutex _out_mutex;

    virtual void process(size_t n, const Commands& commands) final {
        if(commands.size() == 0)
            return;

        Metrics::get().update("console.blocks", 1);
        Metrics::get().update("console.commands", commands.size());

        std::lock_guard<std::mutex> out_lock(_out_mutex);
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
    StreamPrint(std::ostream& out, size_t max_buffer_size = 10) noexcept : Processor(max_buffer_size), _out(out)
    {
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

        std::string prefix = "file.";
        Metrics::get().update(prefix + "blocks", 1);
        Metrics::get().update(prefix + "commands", commands.size());
        prefix += std::to_string(n) + ".";
        Metrics::get().update(prefix + "blocks", 1);
        Metrics::get().update(prefix + "commands", commands.size());

        std::string name = "bulk";
        time_t tm = std::get<0>(*(commands.begin()));
        name += std::to_string(tm);
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
