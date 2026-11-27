#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <span>
#include "phantomx/blockchain/genesis.hpp"

namespace phantomx::poi {

struct PopPayloadFieldsV1 {
  std::uint8_t network_u8{255};
  std::string verifier_id;
  std::array<std::uint8_t,32> pubkey{};
  std::string owner;
  std::array<std::uint8_t,32> challenge_id{};
  std::array<std::uint8_t,32> nonce{};
  std::uint64_t issued_at{0};
  std::uint64_t expires_at{0};
};

inline constexpr char kPopMagicV1[10] = {'P','H','X','V','E','R','P','O','P','\1'};

inline std::uint8_t pop_network_u8(phantomx::Network n) {
  switch(n){ case phantomx::Network::kMainnet: return 0; case phantomx::Network::kTestnet: return 1; case phantomx::Network::kRegtest: return 2; }
  return 255;
}

inline void pop_push_u16_le(std::vector<std::uint8_t>& out, std::uint16_t v){ out.push_back(static_cast<std::uint8_t>(v&0xFF)); out.push_back(static_cast<std::uint8_t>((v>>8)&0xFF)); }
inline void pop_push_u64_le(std::vector<std::uint8_t>& out, std::uint64_t v){ for(int i=0;i<8;++i) out.push_back(static_cast<std::uint8_t>((v>>(8*i))&0xFF)); }
inline bool pop_read_u16_le(const std::vector<std::uint8_t>& in, size_t& off, std::uint16_t& v){ if(off+2>in.size()) return false; v=static_cast<std::uint16_t>(in[off])| (static_cast<std::uint16_t>(in[off+1])<<8); off+=2; return true; }
inline bool pop_read_u64_le(const std::vector<std::uint8_t>& in, size_t& off, std::uint64_t& v){ if(off+8>in.size()) return false; v=0; for(int i=0;i<8;++i) v |= (static_cast<std::uint64_t>(in[off+static_cast<size_t>(i)])<<(8*i)); off+=8; return true; }

inline bool encode_pop_payload_v1(const PopPayloadFieldsV1& f, std::vector<std::uint8_t>& out){
  if (f.verifier_id.empty() || f.verifier_id.size()>64) return false;
  if (f.owner.empty() || f.owner.size()>128) return false;
  if (f.issued_at==0 || f.expires_at==0 || f.expires_at < f.issued_at) return false;
  out.clear();
  out.insert(out.end(), kPopMagicV1, kPopMagicV1+10);
  pop_push_u16_le(out, 1);
  out.push_back(f.network_u8);
  pop_push_u16_le(out, 1);
  out.push_back(1);
  pop_push_u16_le(out, static_cast<std::uint16_t>(f.verifier_id.size()));
  out.insert(out.end(), f.verifier_id.begin(), f.verifier_id.end());
  out.insert(out.end(), f.pubkey.begin(), f.pubkey.end());
  pop_push_u16_le(out, static_cast<std::uint16_t>(f.owner.size()));
  out.insert(out.end(), f.owner.begin(), f.owner.end());
  out.insert(out.end(), f.challenge_id.begin(), f.challenge_id.end());
  out.insert(out.end(), f.nonce.begin(), f.nonce.end());
  pop_push_u64_le(out, f.issued_at);
  pop_push_u64_le(out, f.expires_at);
  return true;
}

inline bool decode_pop_payload_v1(const std::vector<std::uint8_t>& in, PopPayloadFieldsV1& f){
  size_t off=0;
  if (in.size() < 10+2+1+2+1+2+32+2+32+32+8+8) return false;
  for(size_t i=0;i<10;++i){ if(in[off+i] != static_cast<std::uint8_t>(kPopMagicV1[i])) return false; }
  off += 10;
  std::uint16_t ver=0, proto=0, vid_len=0, owner_len=0;
  if(!pop_read_u16_le(in,off,ver) || ver!=1) return false;
  if(off>=in.size()) return false;
  f.network_u8=in[off++];
  if(!pop_read_u16_le(in,off,proto) || proto!=1) return false;
  if(off>=in.size()) return false;
  std::uint8_t op=in[off++];
  if(op!=1) return false;
  if(!pop_read_u16_le(in,off,vid_len) || vid_len==0 || vid_len>64 || off+vid_len>in.size()) return false;
  f.verifier_id.assign(reinterpret_cast<const char*>(&in[off]), reinterpret_cast<const char*>(&in[off+vid_len]));
  off+=vid_len;
  if(off+32>in.size()) return false;
  std::copy(in.begin()+static_cast<std::ptrdiff_t>(off),in.begin()+static_cast<std::ptrdiff_t>(off+32),f.pubkey.begin());
  off+=32;
  if(!pop_read_u16_le(in,off,owner_len) || owner_len==0 || owner_len>128 || off+owner_len>in.size()) return false;
  f.owner.assign(reinterpret_cast<const char*>(&in[off]), reinterpret_cast<const char*>(&in[off+owner_len]));
  off+=owner_len;
  if(off+32>in.size()) return false;
  std::copy(in.begin()+static_cast<std::ptrdiff_t>(off),in.begin()+static_cast<std::ptrdiff_t>(off+32),f.challenge_id.begin());
  off+=32;
  if(off+32>in.size()) return false;
  std::copy(in.begin()+static_cast<std::ptrdiff_t>(off),in.begin()+static_cast<std::ptrdiff_t>(off+32),f.nonce.begin());
  off+=32;
  if(!pop_read_u64_le(in,off,f.issued_at)) return false;
  if(!pop_read_u64_le(in,off,f.expires_at)) return false;
  if(off != in.size()) return false;
  if(f.expires_at < f.issued_at) return false;
  return true;
}

} // namespace phantomx::poi
