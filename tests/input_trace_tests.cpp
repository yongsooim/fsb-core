#include "../tools/input_trace.hpp"
#include <iostream>

int main(int argc,char** argv){
    try{
        if(argc!=2)return 2;const auto directory=std::filesystem::path(argv[1]);std::filesystem::create_directories(directory);const auto file=directory/"fixture.tsv";
        const auto write=[&](const std::string& text){std::ofstream output(file);output<<text;if(!output)throw std::runtime_error("fixture write failed");};
        unsigned checks=0,failures=0;const auto check=[&](bool value){++checks;if(!value)++failures;};
        write("ms\tkind\tkey\tflags\tshift\tcontrol\n001800\t0x100\t13\t0x1c0001\t1\t0\n1800\t0x101\t13\t0xc01c0001\t0\t0\n");
        auto events=fsb::lab::read_input_trace(file);check(events.size()==2&&events[0].ms==1800&&events[1].ms==1800&&events[0].message.shift&&!events[1].message.shift);
        write("# source message observations\n10 1027 131 0\n11 1027 152 0\n");events=fsb::lab::read_input_trace(file);check(events.size()==2&&events[0].message.kind==0x403);
        for(const auto& text:{"2 256 13 1835009\n1 257 13 3223060481\n","0 257 13 0\n","0 256 13 0xc01c0001\n","0 256 13 0x1c0001 2 0\n","0 256 13 4294967296\n","0 256 13 12garbage\n","0 256 13\n","0 999 13 0\n","-1 256 13 0\n"}){
            write(text);bool rejected=false;try{fsb::lab::read_input_trace(file);}catch(const std::exception&){rejected=true;}check(rejected);
        }
        std::cout<<"input_trace_checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
