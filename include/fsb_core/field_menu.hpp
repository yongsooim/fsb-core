#pragma once
#include "primitives.hpp"
#include <functional>
#include <map>

namespace fsb::core {
class Runtime;class RecoveredBattle;
class FieldMenu {
public:
    explicit FieldMenu(Runtime& runtime):runtime_(runtime){}
    using Bytes=std::vector<std::uint8_t>;
    // The core only handles bytes. Hosts restrict these names to their save directory.
    std::function<std::optional<Bytes>(const std::string&)> read_file;
    std::function<bool(const std::string&,const Bytes&)> write_file;
    std::function<bool(const std::string&)> remove_file;
    void open(unsigned slot=0);
    bool active()const;
    void tick();
    void field_input();
    bool service(Address entry,RecoveredBattle& call);
    void after_call(Address entry,RecoveredBattle& call);
    bool save(unsigned slot);
    bool load_snapshot(const Bytes& bytes); // Original codec; no host file is overwritten.
    bool valid_save(const Bytes& bytes)const;
    Bytes settings()const;
    bool load_settings(const Bytes& bytes);
    const std::string& last_error()const{return error_;}
private:
    Runtime& runtime_;
    struct Stream {std::string name;Bytes bytes;std::size_t cursor=0;bool writing=false;};
    std::map<Address,Stream> streams_;
    std::map<std::string,Bytes> memory_files_;
    std::map<std::string,Bytes> pending_backups_;
    std::optional<Bytes> pending_load_;
    Address next_stream_=0x70000000;
    std::string error_;
    bool write_failed_=false;
    std::optional<Bytes> read(const std::string& name);
    bool write(const std::string& name,const Bytes& bytes);
    bool remove(const std::string& name);
};
} // namespace fsb::core
