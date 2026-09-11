#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/symbols.hpp"
#include <limits>
#include <cstring>
#include <cmath>
#include <charconv>

namespace fsb::core {
std::uint64_t RecoveredBattle::x87_truncate(double value){
    // Original498130 sets truncate rounding for FISTP qword. With the game's
    // masked x87 exceptions, NaN/overflow produce the integer-indefinite value.
    if(!std::isfinite(value)||value>=9223372036854775808.0||value< -9223372036854775808.0)return 0x8000000000000000ull;
    return std::uint64_t(std::int64_t(value));
}
void RecoveredBattle::float_to_integer(){
    if(fp_.empty())throw Fault(read(r[4]),"empty recovered x87 stack");
    const auto value=x87_truncate(fp_.back());fp_.pop_back();r[0]=std::uint32_t(value);r[2]=std::uint32_t(value>>32);pop();
}
double RecoveredBattle::x87_square_root(double value){
    if(std::isnan(value))return std::bit_cast<double>(std::bit_cast<std::uint64_t>(value)|0x0008000000000000ull);
    if(value<0)return std::bit_cast<double>(0xfff8000000000000ull);
    return std::sqrt(value); //Preserves signed zero and positive infinity.
}
void RecoveredBattle::float_square_root(){
    const auto bits=std::uint64_t(argument(0))|(std::uint64_t(argument(1))<<32);const auto value=std::bit_cast<double>(bits);
    if(value<0||std::isnan(value))memory_.write(globals::crt_math_errno,0x21);
    fp_.push_back(x87_square_root(value));pop(); //cdecl double argument, x87 ST0 result.
}
void RecoveredBattle::float_arctangent(){
    //499394 loads a cdecl double, executes FLD1/FPATAN, and returns ST0.
    //463038/4630ed retain the original quadrant, scale and __ftol steps.
    const auto bits=std::uint64_t(argument(0))|(std::uint64_t(argument(1))<<32);
    const auto value=std::bit_cast<double>(bits);
    //Coincident endpoints produce0/0. The original CRT error tail4a11b0
    //sets its own errno cell; host errno values are not the guest contract.
    constexpr unsigned original_math_domain_error=0x21;
    if(std::isnan(value))memory_.write(globals::crt_math_errno,original_math_domain_error);
    fp_.push_back(std::atan(value));pop();
}
namespace {
constexpr Address stack_base=0x01000000,stack_top=0x0100f000;
std::uint32_t mask(unsigned width){return width==4?0xffffffffu:(1u<<(width*8))-1;}
std::int64_t sign_extend(std::uint32_t value,unsigned width){
    const auto bits=width*8,sign=std::uint32_t(1)<<(bits-1);value&=mask(width);
    return value&sign?std::int64_t(value)-(std::int64_t(1)<<bits):value;
}
}
std::uint32_t RecoveredBattle::read(Address address,unsigned width)const{
    // Imports are stable service IDs in translated code, never host pointers.
    if(width==4&&is_import(address))return address;
    if(address>=stack_base&&std::uint64_t(address)+width<=stack_base+stack_.size()){
        std::uint32_t value=0;for(unsigned i=0;i<width;++i)value|=std::uint32_t(stack_[address-stack_base+i])<<(i*8);return value;
    }return memory_.read(address,width);
}
void RecoveredBattle::write(Address address,std::uint32_t value,unsigned width){
    if(address>=stack_base&&std::uint64_t(address)+width<=stack_base+stack_.size()){
        for(unsigned i=0;i<width;++i)stack_[address-stack_base+i]=std::uint8_t(value>>(i*8));return;
    }memory_.write(address,value,width);
}
std::string RecoveredBattle::format_text(Address format,Address arguments)const{
    const auto string=[&](Address at){std::string value;while(read(at,1)){value.push_back(char(read(at++,1)));if(value.size()>4096)throw Fault(at,"original text buffer exceeded");}return value;};
    const auto pattern=string(format);std::string out;unsigned index=0;
    for(std::size_t i=0;i<pattern.size();++i){if(pattern[i]!='%'){out.push_back(pattern[i]);continue;}if(++i==pattern.size())throw Fault(format,"unterminated text format");
        if(pattern[i]=='%'){out.push_back('%');continue;}bool zero=false,left=false;unsigned width=0;
        while(i<pattern.size()&&(pattern[i]=='0'||pattern[i]=='-')){zero|=pattern[i]=='0';left|=pattern[i]=='-';++i;}
        while(i<pattern.size()&&pattern[i]>='0'&&pattern[i]<='9')width=width*10+unsigned(pattern[i++]-'0');
        if(i==pattern.size()||width>256)throw Fault(format,"text format width outside buffer");const auto arg=read(arguments+index++*4);std::string value;
        if(pattern[i]=='s')value=string(arg);
        else if(pattern[i]=='d'||pattern[i]=='i')value=std::to_string(signed32(arg));
        else if(pattern[i]=='u')value=std::to_string(arg);
        else if(pattern[i]=='c')value.assign(1,char(arg&255));
        else if(pattern[i]=='x'||pattern[i]=='X'){
            char digits[8];const auto converted=std::to_chars(digits,digits+sizeof(digits),arg,16);value.assign(digits,converted.ptr);
            if(pattern[i]=='X')for(auto& c:value)if(c>='a'&&c<='f')c=char(c-'a'+'A');
        }else throw Fault(format,"unsupported original text format conversion");
        const auto padding=value.size()<width?width-value.size():0;
        if(left){out+=value;out.append(padding,' ');}
        else if(zero&&padding&&!value.empty()&&value[0]=='-'){out+='-';out.append(padding,'0');out+=value.substr(1);}
        else{out.append(padding,zero?'0':' ');out+=value;}
    }return out;
}
std::uint32_t RecoveredBattle::get(unsigned reg,unsigned width,unsigned offset)const{return(r[reg]>>offset)&mask(width);}
void RecoveredBattle::put(unsigned reg,std::uint32_t value,unsigned width,unsigned offset){const auto bits=mask(width);r[reg]=(r[reg]&~(bits<<offset))|((value&bits)<<offset);}
void RecoveredBattle::push(std::uint32_t value){r[4]-=4;write(r[4],value);}
std::uint32_t RecoveredBattle::pop(){const auto value=read(r[4]);r[4]+=4;return value;}
void RecoveredBattle::flags(std::uint32_t value,unsigned width){
    value&=mask(width);zero_=value==0;sign_=bool(value&(1u<<(width*8-1)));
    auto p=value&255;p^=p>>4;p^=p>>2;p^=p>>1;parity_=(p&1)==0;
}
std::uint32_t RecoveredBattle::add(std::uint32_t a,std::uint32_t b,unsigned width,bool use_carry){
    const auto bits=mask(width),sign=1u<<(width*8-1);a&=bits;b&=bits;
    const std::uint64_t wide=std::uint64_t(a)+b+unsigned(use_carry&&carry_);const auto value=std::uint32_t(wide)&bits;
    carry_=wide>bits;overflow_=bool((~(a^b)&(a^value))&sign);flags(value,width);return value;
}
std::uint32_t RecoveredBattle::sub(std::uint32_t a,std::uint32_t b,unsigned width,bool use_borrow){
    const auto bits=mask(width),sign=1u<<(width*8-1);a&=bits;b&=bits;
    const auto borrow=unsigned(use_borrow&&carry_);const auto value=(a-b-borrow)&bits;
    carry_=std::uint64_t(a)<std::uint64_t(b)+borrow;overflow_=bool(((a^b)&(a^value))&sign);flags(value,width);return value;
}
std::uint32_t RecoveredBattle::logic(std::uint32_t value,unsigned width){carry_=overflow_=false;value&=mask(width);flags(value,width);return value;}
std::uint32_t RecoveredBattle::inc(std::uint32_t value,unsigned width,int delta){const bool saved=carry_;value=delta>0?add(value,1,width):sub(value,1,width);carry_=saved;return value;}
std::uint32_t RecoveredBattle::shift(std::uint32_t value,std::uint32_t count,unsigned width,unsigned kind){
    count&=31;value&=mask(width);if(!count)return value;const auto bits=width*8,old=value;
    if(kind==0){carry_=count<=bits&&bool((std::uint64_t(value)<<(count-1))&(std::uint64_t(1)<<(bits-1)));value=std::uint32_t(std::uint64_t(value)<<count)&mask(width);if(count==1)overflow_=bool(value&(1u<<(bits-1)))!=carry_;}
    else if(kind==1){carry_=count<=bits&&bool((value>>(count-1))&1);value=count>=bits?0:value>>count;if(count==1)overflow_=bool(old&(1u<<(bits-1)));}
    else {carry_=count>=bits?bool(value&(1u<<(bits-1))):bool((value>>(count-1))&1);const auto signed_value=sign_extend(value,width);value=signed_value<0?std::uint32_t(~(std::uint64_t(~signed_value)>>count)):std::uint32_t(std::uint64_t(signed_value)>>count);value&=mask(width);if(count==1)overflow_=false;}
    flags(value,width);return value;
}
std::uint32_t RecoveredBattle::multiply(std::uint32_t a,std::uint32_t b,unsigned width){const auto product=sign_extend(a,width)*sign_extend(b,width);const auto value=std::uint32_t(product)&mask(width);carry_=overflow_=product!=sign_extend(value,width);return value;}
void RecoveredBattle::divide(std::uint32_t divisor,bool is_signed){
    if(!divisor)throw Fault(0,"recovered battle integer division by zero");
    const auto wide=(std::uint64_t(r[2])<<32)|r[0];
    if(is_signed){
        const auto numerator=wide>>63?-1-std::int64_t(~wide):std::int64_t(wide),denominator=sign_extend(divisor,4);
        if(numerator==std::numeric_limits<std::int64_t>::min()&&denominator==-1)throw Fault(0,"recovered battle division overflow");
        const auto quotient=numerator/denominator;if(quotient<std::numeric_limits<std::int32_t>::min()||quotient>std::numeric_limits<std::int32_t>::max())throw Fault(0,"recovered battle quotient overflow");r[0]=std::uint32_t(quotient);r[2]=std::uint32_t(numerator%denominator);
    }else{const auto quotient=wide/divisor;if(quotient>0xffffffffu)throw Fault(0,"recovered battle quotient overflow");r[0]=std::uint32_t(quotient);r[2]=std::uint32_t(wide%divisor);}
}
bool RecoveredBattle::condition(unsigned id)const{
    switch(id){case 0:return zero_;case 1:return !zero_;case 2:return sign_!=overflow_;case 3:return sign_==overflow_;case 4:return zero_||sign_!=overflow_;case 5:return !zero_&&sign_==overflow_;case 6:return carry_;case 7:return !carry_;case 8:return carry_||zero_;case 9:return !carry_&&!zero_;case 10:return sign_;case 11:return !sign_;case 12:return overflow_;case 13:return !overflow_;case 14:return parity_;case 15:return !parity_;}throw Fault(id,"unknown recovered condition");
}
void RecoveredBattle::result(std::uint32_t value,unsigned popped_bytes){r[0]=value;pop();r[4]+=popped_bytes;}
std::uint32_t RecoveredBattle::invoke(Address entry,const std::vector<std::uint32_t>& args){
    if(depth_)throw Fault(entry,"recursive public recovered-battle invocation");
    r.fill(0);r[4]=stack_top;fp_.clear();carry_=zero_=sign_=overflow_=parity_=false;
    write(r[4],stack_base);unsigned index=0;for(auto arg:args)write(r[4]+4+index++*4,arg);
    dispatch(entry);if(r[4]!=stack_top+4&&r[4]!=stack_top+4+args.size()*4)throw Fault(entry,"recovered battle unbalanced return stack");return r[0];
}
std::uint32_t RecoveredBattle::callback(Address entry,const std::vector<std::uint32_t>& args){
    if(!depth_)return invoke(entry,args);
    const auto saved=r;const std::array<bool,5> flags{carry_,zero_,sign_,overflow_,parity_};const auto fp=fp_;
    const auto restore=[&]{r=saved;carry_=flags[0];zero_=flags[1];sign_=flags[2];overflow_=flags[3];parity_=flags[4];fp_=fp;};
    try{
        for(auto it=args.rbegin();it!=args.rend();++it)push(*it);push(stack_base);fp_.clear();dispatch(entry);
        if(r[4]!=saved[4]&&r[4]!=saved[4]-args.size()*4)throw Fault(entry,"original callback stack imbalance");
        const auto value=r[0];restore();return value;
    }catch(...){restore();throw;}
}
} // namespace fsb::core
