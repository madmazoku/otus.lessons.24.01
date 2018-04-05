#pragma once

#include <string>
#include <tuple>
#include <list>
#include <iostream>
#include <fstream>

#include <boost/variant.hpp>

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

enum class MixerRecordType {
    command,
    block,
    util
};

enum class MixerRecordUtil {
    eof
};

struct MixerRecord {
    MixerRecordType type;
    boost::variant<Command, Commands, MixerRecordUtil> data;
    std::thread::id thread_id;

    MixerRecord(const Command& command) : type(MixerRecordType::command), data(command), thread_id(std::this_thread::get_id())
    {
    }

    MixerRecord(const Commands& commands) : type(MixerRecordType::block), data(commands), thread_id(std::this_thread::get_id())
    {
    }

    MixerRecord(const MixerRecordUtil& util) : type(MixerRecordType::util), data(util), thread_id(std::this_thread::get_id())
    {
    }

    MixerRecord() = default;
    MixerRecord(const MixerRecord& mr) = default;
    MixerRecord(MixerRecord&& mr) = default;

    MixerRecord& operator=(const MixerRecord&) = delete;
    MixerRecord& operator=(MixerRecord&&) = default;
};

class Mixer : public Channel<MixerRecord>
{
private:
    size_t _N;
    Pipe<Commands>* _distributor;

    using CommandRec = std::tuple<Command, std::thread::id>;
    using CommandRecs = std::list<CommandRec>;

    virtual void act(size_t n) final {
        CommandRecs recs;
        MixerRecord rec;
        while(get(rec))
        {
            switch(rec.type) {
            case MixerRecordType::command:
                recs.push_back(std::make_tuple(boost::get<Command>(rec.data), rec.thread_id));
                if(_N == 0 || recs.size() == _N)
                    distribute(recs);
                break;
            case MixerRecordType::block:
                distribute(rec.thread_id, recs);
                distribute(boost::get<Commands>(rec.data));
                break;
            case MixerRecordType::util:
                switch(boost::get<MixerRecordUtil>(rec.data)) {
                case MixerRecordUtil::eof:
                    distribute(rec.thread_id, recs);
                    break;
                }
                break;
            };
        }
    }

    void distribute(std::thread::id thread_id, CommandRecs& recs)
    {
        if(recs.empty())
            return;

        bool found = false;
        for(auto &r : recs)
            if(std::get<1>(r) == thread_id) {
                found = true;
                break;
            }
        if(found)
            distribute(recs);
    }

    void distribute(CommandRecs& recs)
    {
        Commands commands;
        while(!recs.empty()) {
            commands.push_back(std::get<0>(recs.front()));
            recs.pop_front();
        }
        distribute(commands);
    }

    void distribute(Commands& commands)
    {
        Metrics::get().update("mixer.send.blocks", 1);
        Metrics::get().update("mixer.send.commands", commands.size());
        _distributor->put(commands);
    }

public:
    Mixer(size_t N, size_t max_buffer_size = 10) noexcept : Channel<MixerRecord>(max_buffer_size), _N(N)
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

class Reader : public Channel<std::string>
{
private:

    Pipe<MixerRecord>* _mixer;

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
            ++_bracket_counter;
        } else if(line == "}") {
            if(_bracket_counter > 0 && --_bracket_counter == 0) {
                mix(MixerRecord(_commands));
                _commands.clear();
            }
        } else if(_bracket_counter > 0) {
            _commands.push_back(std::make_tuple(std::time(nullptr), line));
        } else {
            mix(MixerRecord(std::make_tuple(std::time(nullptr), line)));
        }
    }

    void mix(const MixerRecord& rec)
    {
        switch(rec.type) {
        case MixerRecordType::command:
            Metrics::get().update("reader.mix.commands", 1);
            break;
        case MixerRecordType::block:
            Metrics::get().update("reader.mix.block.count", 1);
            Metrics::get().update("reader.mix.block.size", boost::get<Commands>(rec.data).size());
            break;
        case MixerRecordType::util:
            switch(boost::get<MixerRecordUtil>(rec.data)) {
            case MixerRecordUtil::eof:
                Metrics::get().update("reader.mix.eof", 1);
                break;
            }
            break;
        };
        _mixer->put(rec);
    }

    virtual void act(size_t n) override
    {
        std::string buffer;
        while(get(buffer)) {
            _data.append(buffer);
            process_data();
        }
        _commands.clear();
        mix(MixerRecord(MixerRecordUtil::eof));
    }

public:
    Reader(size_t max_buffer_size = 10) noexcept
        : Channel<std::string>(max_buffer_size),
          _mixer(nullptr),
          _bracket_counter(0)
    {
    }
    virtual ~Reader() = default;

    void attach(Pipe<MixerRecord>& mixer)
    {
        _mixer = &mixer;
    }

    void detach()
    {
        _mixer = nullptr;
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
