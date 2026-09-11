#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <stdexcept>

namespace fsb::host {
inline std::array<std::uint16_t,8> local_time(){
    const auto now=std::chrono::system_clock::now();const auto time=std::chrono::system_clock::to_time_t(now);
    const auto* converted=std::localtime(&time);if(!converted)throw std::runtime_error("host local time unavailable");
    const auto tm=*converted;
    const auto milliseconds=std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()%1000;
    return {std::uint16_t(tm.tm_year+1900),std::uint16_t(tm.tm_mon+1),std::uint16_t(tm.tm_wday),std::uint16_t(tm.tm_mday),
            std::uint16_t(tm.tm_hour),std::uint16_t(tm.tm_min),std::uint16_t(tm.tm_sec),std::uint16_t(milliseconds)};
}
}
