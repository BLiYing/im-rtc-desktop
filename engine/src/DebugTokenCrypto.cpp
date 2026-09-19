#include <cstddef>
#include <cstdint>

#include "imrtc/DebugToken.h"

/**
 * 调试签票用的 SHA-256 / HMAC / base64url。engine 零第三方依赖（连 OpenSSL 都不链），
 * 所以这里自带一份小实现；正确性由 tests/DebugTokenTest.cpp 用 FIPS / RFC 4231 标准向量
 * 与一致性向量共同钉住。
 */
namespace imrtc {
namespace {

constexpr std::uint32_t kRound[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

std::uint32_t rotr(std::uint32_t value, unsigned bits) { return (value >> bits) | (value << (32 - bits)); }

/** Sha256 是流式哈希：update 任意次，finish 一次。 */
class Sha256 {
public:
  Sha256() : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {}

  void update(const std::uint8_t* data, std::size_t size) {
    total_ += size;
    for (std::size_t i = 0; i < size; ++i) {
      block_[used_++] = data[i];
      if (used_ == 64) {
        compress();
        used_ = 0;
      }
    }
  }

  std::array<std::uint8_t, 32> finish() {
    const std::uint64_t bits = static_cast<std::uint64_t>(total_) * 8;
    const std::uint8_t pad = 0x80;
    update(&pad, 1);
    const std::uint8_t zero = 0;
    while (used_ != 56) update(&zero, 1);
    std::uint8_t length[8];
    for (int i = 0; i < 8; ++i) length[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
    update(length, 8);
    std::array<std::uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
      for (int j = 0; j < 4; ++j) out[i * 4 + j] = static_cast<std::uint8_t>(state_[i] >> (24 - 8 * j));
    }
    return out;
  }

private:
  void compress() {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(block_[i * 4]) << 24) | (static_cast<std::uint32_t>(block_[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(block_[i * 4 + 2]) << 8) | static_cast<std::uint32_t>(block_[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t v[8];
    for (int i = 0; i < 8; ++i) v[i] = state_[i];
    for (int i = 0; i < 64; ++i) {
      const std::uint32_t s1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
      const std::uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
      const std::uint32_t t1 = v[7] + s1 + ch + kRound[i] + w[i];
      const std::uint32_t s0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
      const std::uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
      const std::uint32_t t2 = s0 + maj;
      v[7] = v[6];
      v[6] = v[5];
      v[5] = v[4];
      v[4] = v[3] + t1;
      v[3] = v[2];
      v[2] = v[1];
      v[1] = v[0];
      v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; ++i) state_[i] += v[i];
  }

  std::uint32_t state_[8];
  std::uint8_t block_[64] = {};
  std::size_t used_ = 0;
  std::uint64_t total_ = 0;
};

const std::uint8_t* bytesOf(const std::string& text) {
  return reinterpret_cast<const std::uint8_t*>(text.data());
}

}  // namespace

std::array<std::uint8_t, 32> hmacSha256(const std::string& key, const std::string& message) {
  std::uint8_t block[64] = {};
  if (key.size() > 64) {
    Sha256 keyHash;
    keyHash.update(bytesOf(key), key.size());
    const std::array<std::uint8_t, 32> digest = keyHash.finish();
    for (std::size_t i = 0; i < digest.size(); ++i) block[i] = digest[i];
  } else {
    for (std::size_t i = 0; i < key.size(); ++i) block[i] = static_cast<std::uint8_t>(key[i]);
  }
  std::uint8_t inner[64];
  std::uint8_t outer[64];
  for (int i = 0; i < 64; ++i) {
    inner[i] = block[i] ^ 0x36;
    outer[i] = block[i] ^ 0x5c;
  }
  Sha256 first;
  first.update(inner, 64);
  first.update(bytesOf(message), message.size());
  const std::array<std::uint8_t, 32> innerDigest = first.finish();
  Sha256 second;
  second.update(outer, 64);
  second.update(innerDigest.data(), innerDigest.size());
  return second.finish();
}

std::string base64UrlEncode(const std::uint8_t* data, std::size_t size) {
  static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  out.reserve((size + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 2 < size; i += 3) {
    const std::uint32_t chunk = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8) | data[i + 2];
    out += kAlphabet[chunk >> 18];
    out += kAlphabet[(chunk >> 12) & 63];
    out += kAlphabet[(chunk >> 6) & 63];
    out += kAlphabet[chunk & 63];
  }
  if (size - i == 1) {
    const std::uint32_t chunk = static_cast<std::uint32_t>(data[i]) << 16;
    out += kAlphabet[chunk >> 18];
    out += kAlphabet[(chunk >> 12) & 63];
  } else if (size - i == 2) {
    const std::uint32_t chunk = (static_cast<std::uint32_t>(data[i]) << 16) | (static_cast<std::uint32_t>(data[i + 1]) << 8);
    out += kAlphabet[chunk >> 18];
    out += kAlphabet[(chunk >> 12) & 63];
    out += kAlphabet[(chunk >> 6) & 63];
  }
  return out;
}

std::string base64UrlEncode(const std::string& data) { return base64UrlEncode(bytesOf(data), data.size()); }

}  // namespace imrtc
