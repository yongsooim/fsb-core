#pragma once
#include "fsb_core/field_menu.hpp"
#include <filesystem>
#include <fstream>

namespace fsb::host {
// The original game file names are confined to an application-owned directory.
class SaveDirectory {
public:
    explicit SaveDirectory(std::filesystem::path directory):directory_(std::move(directory)){}
    void attach(core::FieldMenu& menu){
        menu.read_file=[this](const std::string& name){return read(name);};
        menu.write_file=[this](const std::string& name,const auto& bytes){return write(name,bytes);};
        menu.remove_file=[this](const std::string& name){std::error_code ec;return std::filesystem::remove(path(name),ec)&&!ec;};
    }
    std::optional<core::FieldMenu::Bytes> read(const std::string& name)const{
        std::error_code ec;const auto file=path(name);const auto size=std::filesystem::file_size(file,ec);
        if(ec||size>1024*1024)return std::nullopt;
        core::FieldMenu::Bytes data(static_cast<std::size_t>(size));std::ifstream in(file,std::ios::binary);
        if(!in.read(reinterpret_cast<char*>(data.data()),std::streamsize(data.size())))return std::nullopt;return data;
    }
    bool write(const std::string& name,const core::FieldMenu::Bytes& data)const{
        std::error_code ec;std::filesystem::create_directories(directory_,ec);if(ec)return false;
        const auto file=path(name),temporary=path(name+".tmp");
        {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(data.data()),std::streamsize(data.size()));out.flush();if(!out){std::filesystem::remove(temporary,ec);return false;}}
        std::filesystem::rename(temporary,file,ec);
        if(ec){std::filesystem::remove(temporary);return false;}return true;
    }
    const std::filesystem::path& directory()const{return directory_;}
private:
    std::filesystem::path directory_;
    std::filesystem::path path(const std::string& name)const{
        if(name.find('/')!=std::string::npos||name.find('\\')!=std::string::npos||name=="."||name=="..")throw std::runtime_error("invalid save name");return directory_/name;
    }
};
} // namespace fsb::host
