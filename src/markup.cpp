#include "fsb_core/markup.hpp"
#include <algorithm>
#include <string_view>

namespace fsb::core {
namespace {
std::optional<std::uint32_t> decimal(std::string_view text) {
    if (text.empty()) return std::nullopt;
    bool negative = text.front() == '-'; unsigned at = negative || text.front() == '+'; std::uint32_t result = 0;
    if (at == text.size()) return std::nullopt;
    for (; at < text.size(); ++at) { if (text[at] < '0' || text[at] > '9') return std::nullopt; result = result * 10 + unsigned(text[at] - '0'); }
    return negative ? 0u - result : result;
}
std::pair<std::uint32_t,unsigned> compact(std::string_view text) {
    std::uint32_t result = 0; unsigned count = 0;
    for (auto c : text) {
        if (count == 4 || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) break;
        result = (result << 8) | unsigned(c); ++count;
    }
    return {result,count};
}
unsigned direction(const Memory& memory, unsigned digit) {
    // The historical helper is named "mask", but its original table maps the
    // keypad digits to actor direction selectors0..7.
    if (digit < 1 || digit > 9 || digit == 5) throw Fault(digit,"invalid markup direction digit");
    return memory.read(0x5ab778 + digit * 4);
}
}
MarkupToken tokenize_markup(const Memory& memory, Address at) {
    const auto first = memory.read(at,1);
    if (first & 128) { if (!memory.read(at + 1,1)) throw Fault(at,"truncated CP949 pair"); return {0,2,0,0}; }
    if (!first) return {1,1,0,0};
    switch (first) {
    case '@': return memory.read(at+1,1) == '@' ? MarkupToken{2,2,0,0} : MarkupToken{5,1,0,0};
    case '|': return {3,1,0,0}; case ';': return {4,1,0,0};
    case '^': return {6,1,0,0}; case '*': return {7,1,0,0}; case '/': return {0x2e,1,0,0};
    default: if (first != '<') return {};
    }
    std::string raw, upper; bool closed = false;
    for (unsigned i = 1; i <= 512; ++i) {
        const auto c = memory.read(at+i,1); if (!c) break; if (c == '>') { closed = true; break; }
        raw.push_back(char(c)); upper.push_back(char(c >= 'a' && c <= 'z' ? c-32 : c));
    }
    if (!closed) {if(upper.starts_with("EVENTTITLE_"))throw Fault(at,"unterminated EventTitle markup");return {};}
    const auto token = [&](unsigned code,std::uint32_t argument=0,std::uint32_t auxiliary=0) { return MarkupToken{code,unsigned(raw.size()+2),argument,auxiliary}; };
    struct Fixed { const char* tag; unsigned code, argument; };
    static constexpr Fixed fixed[] = {
        {"-",8,0},{"+",9,0},{"M+",10,0},{"M-",10,1},{"?",11,1},{"?-",11,0},{"!",12,1},{"!-",12,0},{"SWEAT",13,0},
        {"NET",14,1},{"TRANS",14,0},{"SOLID",14,2},{"WOOD",14,3},{"PAPER",14,4},
        {"WIDTHS",15,0},{"WIDTHM",15,1},{"WIDTHL",15,2},{"WIDTHXL",15,3},{"WIDTHHUGE",15,4},{"WIDTHMAX",15,5},
        {"TAIL",20,1},{"TAILX",20,0},{"ID_ON",21,1},{"ID_OFF",21,0},{"INPUT",23,0},{"AUTOINPUT",23,1},{"FORCEIN",24,0},{"OUTOK",24,1},
        {"POSSUB_D",26,0x09a4},{"POSSUB_U",26,0x21c2},{"POSSUB_L",26,0x12da},{"POSSUB_R",26,0x188c},
        {"POSSUB_82H",26,0x0339},{"POSSUB_2H8",26,0x0102},{"POSSUB_H82",26,0x0246},{"POSSUB_28H",26,0x011d},{"POSSUB_8H2",26,0x0354},{"POSSUB_H28",26,0x0210},{"POSSUB_HV",26,0x32},{"POSSUB_VH",26,5},
        {"LR+",27,1},{"LR-",27,0},{"B",29,1},{"T",29,0},{"ITEM",30,0},{"SELECT",31,0},{"PUSH",34,0},{"POP",34,1},
        {"BT_SIL",35,0},{"BT_LET",35,16},{"BT_RIM",35,32},{"BT_R",35,10},
        {"PLAIN",36,0},{"BR",36,1},{"BRX",36,2},{"BR_R",36,3},{"2UP",36,4},{"2DN",36,5},{"2X",36,6},{"UNDERBAR",36,7},{"UNDERBARX",36,8},{"ACCENT",36,9},{"ACCENTX",36,10},{"X",36,11},
        {"SIMUL",37,1},{"NOSIMUL",37,0},{"FAST",38,1},{"FAST-",38,0},{"TURNMANON",42,1},{"TURNMANOFF",42,0},{"WA",50,0}
    };
    for (auto rule : fixed) if (upper == rule.tag) return token(rule.code,rule.argument);
    struct Numeric { const char* prefix; unsigned code; };
    static constexpr Numeric numeric[] = {{"HMAX",17},{"HMIN",16},{"HEIGHT",18},{"CS",19},{"INDENTNEXT",33},{"INDENT",32},{"W=",40},{"D",39},{"SE",54},{"BGM",55},{"&ITEM",48},{"&FACE",49}};
    for (auto rule : numeric) {
        const std::string_view prefix(rule.prefix);
        if (std::string_view(upper).starts_with(prefix)) if (const auto value = decimal(std::string_view(raw).substr(prefix.size()))) return token(rule.code,*value);
    }
    if (upper.size() >= 3 && upper[0] == 'W' && (upper[1] == '+' || upper[1] == '-')) if (auto value = decimal(std::string_view(raw).substr(1))) return token(41,*value);
    if (upper.size() >= 2 && upper[0] == 'F' && upper[1] >= '0' && upper[1] <= '9') {
        if (upper.size() == 2) return token(28,unsigned(upper[1]-'0'));
        if (upper.size() == 3 && (upper[2] == 'T' || upper[2] == 'B')) return token(28,unsigned(upper[1]-'0') | (unsigned(upper[2] == 'T' ? 1 : 2) << 16));
    }
    if (upper.size() >= 4 && upper.starts_with("POS") && upper[3] >= '0' && upper[3] <= '9') {
        if (upper.size() == 4) return token(25,unsigned(upper[3]-'0'));
        if (upper.size() == 5 && upper[4] == 'H') return token(25,unsigned(upper[3]-'0'),1);
    }
    if (upper.starts_with("IDS_")) return token(22,at+5);
    if (upper.starts_with("MSG_ID=")) { const auto id = compact(std::string_view(raw).substr(7)); if (id.second == raw.size()-7) return token(43,id.first); }
    if (upper.starts_with("$=")) { const auto id = compact(std::string_view(raw).substr(2)); if (id.second == raw.size()-2) return token(44,id.first); }
    if (!raw.empty() && raw.front() == '$') {
        const auto id = compact(std::string_view(raw).substr(1)); const auto suffix = raw.substr(1+id.second);
        if (suffix.empty()) return token(45,id.first);
        if (suffix.front() == ':') { if (const auto value = decimal(std::string_view(suffix).substr(1))) return token(45,id.first,*value | 0x8000); }
        const Fixed controls[] = {{"!",0,16},{"!-",0,32},{"?",0,64},{"?-",0,128},{"-",0,1},{"^",0,2}};
        for (auto control : controls) if (suffix == control.tag) return token(45,id.first,control.argument);
    }
    if (!raw.empty() && raw.front() == '#') {
        if (raw == "#?") return token(46,0); // Body returns0, despite an older annotation calling this fallback.
        const auto id = compact(std::string_view(raw).substr(1)); if (id.second == raw.size()-1) return token(46,id.first ? id.first : 0xffffffffu);
    }
    if (upper.size() >= 4 && upper.starts_with("DIR") && upper[3] >= '0' && upper[3] <= '9') {
        if (upper.size() == 4) return token(51,0,direction(memory,unsigned(upper[3]-'0')));
        if (upper[4] == ':') if (const auto value = decimal(std::string_view(raw).substr(5))) return token(51,*value,direction(memory,unsigned(upper[3]-'0')));
    }
    if(upper.size()>=7&&upper.starts_with("WALK")&&upper[4]>='0'&&upper[4]<='9'&&upper[5]==','){
        const auto tail=std::string_view(raw).substr(6);const auto colon=tail.find(':');const auto destination=decimal(tail.substr(0,colon));
        const auto extra=colon==std::string_view::npos?std::optional<std::uint32_t>(0):decimal(tail.substr(colon+1));
        if(destination&&extra)return token(52,(*destination<<16)|(*extra&65535),direction(memory,unsigned(upper[4]-'0')));
    }
    if(upper.substr(0,11)=="EVENTTITLE_"){
        if(raw.size()-11>=128)throw Fault(at,"EventTitle exceeds original128-byte scratch buffer");
        return token(53,at+12);
    }
    return {}; // Original fallback draws literal '<', never drops an unknown tag.
}
MarkupToken tokenize_markup(Memory& memory,Address at){
    auto token=tokenize_markup(static_cast<const Memory&>(memory),at);
    if(token.code==53){
        // 40c632 copies only title bytes and the NUL, preserving the rest of
        // this shared buffer. Measurement also calls this parser.
        const auto length=token.bytes-13;
        for(unsigned i=0;i<length;++i)memory.write(0x7683e0+i,memory.read(at+12+i,1),1);
        memory.write(0x7683e0+length,0,1);token.argument=0;
    }
    return token;
}
DialogMeasure measure_dialog(Memory& memory, Address text, int width, int initial_indent, int next_indent, int hmax, int hmin, int forced) {
    if (width < 1 || width > 4096) throw Fault(text,"invalid dialog wrap width");
    int max_line = 0, line = 0, current_indent = initial_indent, measured = initial_indent, column = initial_indent, trim = 0;
    bool auto_wrap = false, line_start = true;
    for (unsigned offset = 0;;) {
        if (offset > 1024*1024) throw Fault(text,"dialog measurement exceeded source limit");
        const auto byte = memory.read(text+offset,1); if (!byte) break;
        const auto token = tokenize_markup(memory,text+offset);
        if (token.code == 2) break;
        int next_indent_value = current_indent;
        if (!token.code) {
            if (width < column + int(token.bytes)) { ++line; line_start = false; measured = std::max(measured,column); auto_wrap = true; column = next_indent; }
            if (byte != ' ' || line_start || column != next_indent) column += int(token.bytes);
        } else if (token.code == 3 || token.code == 5) {
            if (token.code == 3) ++line; else { max_line = std::max(max_line,line); line = 0; }
            line_start = true; measured = std::max(measured,column); column = current_indent;
        } else if (token.code == 16) hmax = signed32(token.argument);
        else if (token.code == 17) hmin = signed32(token.argument);
        else if (token.code == 18) forced = signed32(token.argument);
        else if (token.code == 32) next_indent_value = signed32(token.argument);
        else if (token.code == 33) next_indent = signed32(token.argument);
        current_indent = next_indent_value; offset += token.bytes;
    }
    measured = std::max(measured,column); int visible = std::max(line,max_line) + 1;
    if (!auto_wrap) {
        if (measured == current_indent) visible = 0;
        if (next_indent < current_indent) { measured -= initial_indent-next_indent; trim = initial_indent-next_indent; }
    }
    const auto even_width = measured + measured % 2;
    if (!auto_wrap) width = std::min(width,even_width + (visible == 1 ? 2 : 0));
    const auto auto_height = std::max(1,visible);
    if (forced <= 0) {
        if (hmax <= 0) forced = hmin > 0 ? std::min(auto_height,hmin) : std::min(auto_height,5);
        else if (hmin <= 0) forced = hmax > 4 || auto_height <= hmax ? hmax : std::min(auto_height,5);
        else if (hmax <= hmin) forced = auto_height <= hmax ? hmax : std::min(auto_height,hmin);
        else forced = std::min(auto_height,5);
    }
    if (forced < 1 || forced > 20) throw Fault(text,"dialog height outside original valid domain");
    return {width,forced,trim};
}
} // namespace fsb::core
