#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../security/zenrepo_crypto_material.hpp"

namespace {

struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
using Bytes = std::vector<std::uint8_t>;
using Digest = std::array<std::uint8_t, 32>;

class Sha256 {
public:
    Sha256() { reset(); }
    void update(const std::uint8_t* data, std::size_t size) {
        if (!data && size) throw Error("sha256 null input");
        total_ += static_cast<std::uint64_t>(size);
        while (size) {
            const std::size_t chunk = std::min(size, block_.size() - used_);
            std::memcpy(block_.data() + used_, data, chunk);
            data += chunk; size -= chunk; used_ += chunk;
            if (used_ == block_.size()) { transform(block_.data()); used_ = 0; }
        }
    }
    Digest final() {
        const std::uint64_t bits = total_ * 8U;
        block_[used_++] = 0x80U;
        if (used_ > 56U) { while (used_ < 64U) block_[used_++] = 0; transform(block_.data()); used_ = 0; }
        while (used_ < 56U) block_[used_++] = 0;
        for (int shift = 56; shift >= 0; shift -= 8) block_[used_++] = static_cast<std::uint8_t>(bits >> shift);
        transform(block_.data());
        Digest out{};
        for (std::size_t i = 0; i < state_.size(); ++i) {
            out[i*4] = static_cast<std::uint8_t>(state_[i] >> 24U);
            out[i*4+1] = static_cast<std::uint8_t>(state_[i] >> 16U);
            out[i*4+2] = static_cast<std::uint8_t>(state_[i] >> 8U);
            out[i*4+3] = static_cast<std::uint8_t>(state_[i]);
        }
        return out;
    }
    static Digest hash(const std::uint8_t* data, std::size_t size) { Sha256 s; s.update(data,size); return s.final(); }
    static Digest hash(const Bytes& b) { return hash(b.data(), b.size()); }
private:
    static constexpr std::array<std::uint32_t,64> k = {
        0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
        0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
        0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
        0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
        0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
        0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
        0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
        0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
    };
    static std::uint32_t rr(std::uint32_t v,unsigned n){return(v>>n)|(v<<(32U-n));}
    void reset(){state_={0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U}; block_.fill(0); total_=used_=0;}
    void transform(const std::uint8_t* b){
        std::array<std::uint32_t,64>w{};
        for(std::size_t i=0;i<16;++i)w[i]=(static_cast<std::uint32_t>(b[i*4])<<24U)|(static_cast<std::uint32_t>(b[i*4+1])<<16U)|(static_cast<std::uint32_t>(b[i*4+2])<<8U)|b[i*4+3];
        for(std::size_t i=16;i<64;++i){const auto s0=rr(w[i-15],7)^rr(w[i-15],18)^(w[i-15]>>3U);const auto s1=rr(w[i-2],17)^rr(w[i-2],19)^(w[i-2]>>10U);w[i]=w[i-16]+s0+w[i-7]+s1;}
        auto a=state_[0],b0=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
        for(std::size_t i=0;i<64;++i){const auto s1=rr(e,6)^rr(e,11)^rr(e,25);const auto ch=(e&f)^((~e)&g);const auto t1=h+s1+ch+k[i]+w[i];const auto s0=rr(a,2)^rr(a,13)^rr(a,22);const auto maj=(a&b0)^(a&c)^(b0&c);const auto t2=s0+maj;h=g;g=f;f=e;e=d+t1;d=c;c=b0;b0=a;a=t1+t2;}
        state_[0]+=a;state_[1]+=b0;state_[2]+=c;state_[3]+=d;state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
    }
    std::array<std::uint32_t,8> state_{}; std::array<std::uint8_t,64> block_{}; std::uint64_t total_=0; std::size_t used_=0;
};

bool equal_digest(const std::uint8_t* a,const std::uint8_t* b){std::uint8_t d=0;for(std::size_t i=0;i<32;++i)d|=static_cast<std::uint8_t>(a[i]^b[i]);return d==0;}

struct Big { std::uint32_t limb[64]; };
void zero(Big&v){std::memset(v.limb,0,sizeof(v.limb));}
void from_be(Big&v,const std::uint8_t in[256]){zero(v);for(std::uint32_t i=0;i<256;++i){const auto r=255U-i;v.limb[r/4U]|=static_cast<std::uint32_t>(in[i])<<((r%4U)*8U);}}
void to_be(const Big&v,std::uint8_t out[256]){for(std::uint32_t i=0;i<256;++i){const auto r=255U-i;out[i]=static_cast<std::uint8_t>(v.limb[r/4U]>>((r%4U)*8U));}}
int compare(const Big&a,const Big&b){for(std::uint32_t i=64;i>0;--i){const auto n=i-1;if(a.limb[n]<b.limb[n])return-1;if(a.limb[n]>b.limb[n])return 1;}return 0;}
void subtract(Big&a,const Big&b){unsigned long long borrow=0;for(std::uint32_t i=0;i<64;++i){const unsigned long long x=a.limb[i],y=static_cast<unsigned long long>(b.limb[i])+borrow;a.limb[i]=static_cast<std::uint32_t>(x-y);borrow=x<y;}}
void add_plain(Big&o,const Big&a,const Big&b){unsigned long long carry=0;for(std::uint32_t i=0;i<64;++i){const unsigned long long s=static_cast<unsigned long long>(a.limb[i])+b.limb[i]+carry;o.limb[i]=static_cast<std::uint32_t>(s);carry=s>>32U;}}
void add_mod(Big&o,const Big&a,const Big&b,const Big&m){Big t=m;subtract(t,b);if(compare(a,t)>=0){o=a;subtract(o,t);}else add_plain(o,a,b);}
bool bit(const Big&v,std::uint32_t n){return((v.limb[n/32U]>>(n%32U))&1U)!=0;}
void mul_mod(Big&o,const Big&a,const Big&b,const Big&m){Big r{},c=a,t{};for(std::uint32_t n=0;n<2048;++n){if(bit(b,n)){add_mod(t,r,c,m);r=t;}add_mod(t,c,c,m);c=t;}o=r;}
void exp65537(Big&o,const Big&s,const Big&m){Big r{},t{};r.limb[0]=1U;for(int n=16;n>=0;--n){mul_mod(t,r,r,m);r=t;if((65537U>>static_cast<unsigned>(n))&1U){mul_mod(t,r,s,m);r=t;}}o=r;}
void mgf1(const std::uint8_t seed[32],std::uint8_t*out,std::size_t size){std::size_t off=0;std::uint32_t ctr=0;while(off<size){std::uint8_t input[36];std::memcpy(input,seed,32);input[32]=static_cast<std::uint8_t>(ctr>>24U);input[33]=static_cast<std::uint8_t>(ctr>>16U);input[34]=static_cast<std::uint8_t>(ctr>>8U);input[35]=static_cast<std::uint8_t>(ctr);const auto d=Sha256::hash(input,sizeof(input));const auto n=std::min<std::size_t>(32,size-off);std::memcpy(out+off,d.data(),n);off+=n;++ctr;}}
bool verify_pss(const std::uint8_t sig[256],const std::uint8_t digest[32],const std::uint8_t modulus_bytes[256]){
    Big s{},m{},d{};from_be(s,sig);from_be(m,modulus_bytes);if((modulus_bytes[0]&0x80U)==0||compare(s,m)>=0)return false;exp65537(d,s,m);
    std::uint8_t encoded[256];to_be(d,encoded);constexpr std::size_t dbn=223,pad=190;if(encoded[255]!=0xbcU||(encoded[0]&0x80U))return false;
    const auto*eh=encoded+dbn;std::uint8_t mask[dbn],db[dbn];mgf1(eh,mask,dbn);for(std::size_t i=0;i<dbn;++i)db[i]=static_cast<std::uint8_t>(encoded[i]^mask[i]);db[0]&=0x7fU;
    std::uint8_t malformed=0;for(std::size_t i=0;i<pad;++i)malformed|=db[i];malformed|=static_cast<std::uint8_t>(db[pad]^1U);
    std::uint8_t msg[72]{};std::memcpy(msg+8,digest,32);std::memcpy(msg+40,db+pad+1,32);const auto expected=Sha256::hash(msg,sizeof(msg));
    return malformed==0&&equal_digest(eh,expected.data());
}

#pragma pack(push,1)
