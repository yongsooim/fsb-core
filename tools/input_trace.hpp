#pragma once
#include "fsb_core/input.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fsb::lab {
struct TimedInput { std::uint32_t ms; core::InputMessage message; };
inline std::vector<TimedInput> read_input_trace(const std::filesystem::path& path) {
    std::ifstream file(path);if(!file)throw std::runtime_error("cannot open input trace "+path.string());
    std::vector<TimedInput> result;std::string line;unsigned number=0;
    while(std::getline(file,line)){
        ++number;std::istringstream row(line);std::vector<std::string> fields;std::string word;
        while(row>>word)fields.push_back(word);
        if(fields.empty()||fields[0][0]=='#'||fields[0]=="ms")continue;
        if(fields.size()!=4&&fields.size()!=6)throw std::runtime_error("input trace line "+std::to_string(number)+": expected4 or6 columns");
        std::uint32_t values[6]={};
        for(unsigned i=0;i<fields.size();++i){
            if(fields[i].empty()||fields[i][0]=='-')throw std::runtime_error("negative input trace value");
            const int base=fields[i].size()>2&&fields[i][0]=='0'&&(fields[i][1]=='x'||fields[i][1]=='X')?16:10;
            std::size_t end=0;const auto value=std::stoull(fields[i],&end,base);
            if(end!=fields[i].size()||value>0xffffffffull)throw std::runtime_error("input trace value is not uint32");values[i]=std::uint32_t(value);
        }
        if(!result.empty()&&values[0]<result.back().ms)throw std::runtime_error("input trace timestamps must be nondecreasing");
        if(values[4]>1||values[5]>1)throw std::runtime_error("input trace modifier snapshots must be0 or1");
        const auto kind=values[1];
        if(kind!=0x100&&kind!=0x101&&kind!=0x104&&kind!=0x105&&kind!=0x403)throw std::runtime_error("unsupported trace input message kind");
        if(kind!=0x403){
            const bool release=kind==0x101||kind==0x105;
            if(((values[3]>>31)!=0)!=release)throw std::runtime_error("input trace key transition flag disagrees with message kind");
        }
        result.push_back({values[0],{kind,values[2],values[3],values[4]!=0,values[5]!=0}});
    }
    return result;
}
} // namespace fsb::lab
