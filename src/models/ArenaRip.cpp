// ArenaRip.cpp — the arena-pack reader (phase A6a).
//
// Reads an `rl-arena-pack/1` pack (docs/ARENA_PACK.md) into memory and decodes
// its textures on the CPU.  No GL, no SDL, no windowing: plain C++ + STL, so
// the whole thing is testable headless.
//
// The pack is Psyonix-owned data and is a RUNTIME INPUT (gitignored
// `arena_assets/`); this file never writes anything and never copies pack
// bytes into the repository.
//
// Contents:
//   1. a small strict JSON reader (file-local; no public JSON dependency)
//   2. a file-local SHA-256 (the manifest records the blob's hash)
//   3. CPU DXT1 / DXT5 decoders, byte-identical to tools/arena/rldds.py
//   4. the loader with loud validation
#include "models/ArenaRip.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace rl {
namespace {

// ===========================================================================
// 1. A small strict JSON reader (file-local).
//
// It only has to read the arena manifest, but it must never crash or read out
// of bounds on malformed input: every read is bounds-checked, the nesting
// depth is capped, and a parse failure is reported as a message.
// ===========================================================================

struct JsonValue {
  enum class Type { kNull, kBool, kNumber, kString, kArray, kObject };

  Type type = Type::kNull;
  bool boolean = false;
  double number = 0.0;
  std::string str;
  std::vector<JsonValue> arr;
  // Object members, kept as parallel vectors (a std::map would need a
  // complete JsonValue, which a self-referential value cannot provide).
  std::vector<std::string> keys;
  std::vector<JsonValue> vals;

  const JsonValue* Find(const std::string& key) const {
    if (type != Type::kObject) return nullptr;
    for (size_t i = 0; i < keys.size(); ++i)
      if (keys[i] == key) return &vals[i];
    return nullptr;
  }
  bool IsObject() const { return type == Type::kObject; }
  bool IsArray() const { return type == Type::kArray; }
  bool IsString() const { return type == Type::kString; }
  bool IsNumber() const { return type == Type::kNumber; }
  bool IsNull() const { return type == Type::kNull; }
};

class JsonParser {
 public:
  JsonParser(const char* data, size_t size) : p_(data), begin_(data), end_(data + size) {}

  bool Parse(JsonValue& out, std::string& err) {
    SkipWs();
    if (!ParseValue(out, 0)) {
      err = err_;
      return false;
    }
    SkipWs();
    if (p_ != end_) {
      err = Fail("trailing data after the top-level JSON value");
      return false;
    }
    return true;
  }

 private:
  static constexpr int kMaxDepth = 128;

  std::string Fail(const char* what) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), " at byte %zu", static_cast<size_t>(p_ - begin_));
    err_ = std::string(what) + buf;
    return err_;
  }

  void SkipWs() {
    while (p_ < end_) {
      const char c = *p_;
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++p_;
      } else {
        break;
      }
    }
  }

  bool ParseValue(JsonValue& out, int depth) {
    if (depth > kMaxDepth) {
      Fail("JSON nesting too deep");
      return false;
    }
    if (p_ >= end_) {
      Fail("unexpected end of JSON input");
      return false;
    }
    switch (*p_) {
      case '{': return ParseObject(out, depth);
      case '[': return ParseArray(out, depth);
      case '"':
        out.type = JsonValue::Type::kString;
        return ParseString(out.str);
      case 't':
      case 'f': return ParseBool(out);
      case 'n': return ParseNull(out);
      default: return ParseNumber(out);
    }
  }

  bool ParseObject(JsonValue& out, int depth) {
    out.type = JsonValue::Type::kObject;
    ++p_;  // '{'
    SkipWs();
    if (p_ < end_ && *p_ == '}') {
      ++p_;
      return true;
    }
    for (;;) {
      SkipWs();
      if (p_ >= end_ || *p_ != '"') {
        Fail("expected a member name in a JSON object");
        return false;
      }
      std::string key;
      if (!ParseString(key)) return false;
      SkipWs();
      if (p_ >= end_ || *p_ != ':') {
        Fail("expected ':' after a JSON member name");
        return false;
      }
      ++p_;
      SkipWs();
      JsonValue value;
      if (!ParseValue(value, depth + 1)) return false;
      out.keys.push_back(std::move(key));
      out.vals.push_back(std::move(value));
      SkipWs();
      if (p_ >= end_) {
        Fail("unterminated JSON object");
        return false;
      }
      if (*p_ == ',') {
        ++p_;
        continue;
      }
      if (*p_ == '}') {
        ++p_;
        return true;
      }
      Fail("expected ',' or '}' in a JSON object");
      return false;
    }
  }

  bool ParseArray(JsonValue& out, int depth) {
    out.type = JsonValue::Type::kArray;
    ++p_;  // '['
    SkipWs();
    if (p_ < end_ && *p_ == ']') {
      ++p_;
      return true;
    }
    for (;;) {
      SkipWs();
      JsonValue value;
      if (!ParseValue(value, depth + 1)) return false;
      out.arr.push_back(std::move(value));
      SkipWs();
      if (p_ >= end_) {
        Fail("unterminated JSON array");
        return false;
      }
      if (*p_ == ',') {
        ++p_;
        continue;
      }
      if (*p_ == ']') {
        ++p_;
        return true;
      }
      Fail("expected ',' or ']' in a JSON array");
      return false;
    }
  }

  bool ParseHex4(unsigned& out) {
    if (end_ - p_ < 4) {
      Fail("truncated \\u escape");
      return false;
    }
    out = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = p_[i];
      unsigned d;
      if (c >= '0' && c <= '9') {
        d = static_cast<unsigned>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        d = static_cast<unsigned>(c - 'a') + 10u;
      } else if (c >= 'A' && c <= 'F') {
        d = static_cast<unsigned>(c - 'A') + 10u;
      } else {
        Fail("bad hex digit in a \\u escape");
        return false;
      }
      out = (out << 4) | d;
    }
    p_ += 4;
    return true;
  }

  static void AppendUtf8(std::string& s, unsigned cp) {
    if (cp < 0x80) {
      s.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      s.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      s.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  bool ParseString(std::string& out) {
    out.clear();
    ++p_;  // '"'
    for (;;) {
      if (p_ >= end_) {
        Fail("unterminated JSON string");
        return false;
      }
      const unsigned char c = static_cast<unsigned char>(*p_);
      if (c == '"') {
        ++p_;
        return true;
      }
      if (c < 0x20) {
        Fail("raw control character in a JSON string");
        return false;
      }
      if (c != '\\') {
        out.push_back(*p_);
        ++p_;
        continue;
      }
      ++p_;  // '\'
      if (p_ >= end_) {
        Fail("unterminated escape in a JSON string");
        return false;
      }
      const char e = *p_++;
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          unsigned cp = 0;
          if (!ParseHex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF && end_ - p_ >= 6 && p_[0] == '\\' &&
              p_[1] == 'u') {
            const char* save = p_;
            p_ += 2;
            unsigned lo = 0;
            if (!ParseHex4(lo)) return false;
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
              cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
            } else {
              p_ = save;  // not a pair: keep the high surrogate as-is
            }
          }
          AppendUtf8(out, cp);
          break;
        }
        default:
          Fail("unknown escape in a JSON string");
          return false;
      }
    }
  }

  bool ParseBool(JsonValue& out) {
    if (end_ - p_ >= 4 && std::memcmp(p_, "true", 4) == 0) {
      p_ += 4;
      out.type = JsonValue::Type::kBool;
      out.boolean = true;
      return true;
    }
    if (end_ - p_ >= 5 && std::memcmp(p_, "false", 5) == 0) {
      p_ += 5;
      out.type = JsonValue::Type::kBool;
      out.boolean = false;
      return true;
    }
    Fail("invalid JSON literal");
    return false;
  }

  bool ParseNull(JsonValue& out) {
    if (end_ - p_ >= 4 && std::memcmp(p_, "null", 4) == 0) {
      p_ += 4;
      out.type = JsonValue::Type::kNull;
      return true;
    }
    Fail("invalid JSON literal");
    return false;
  }

  bool ParseNumber(JsonValue& out) {
    const char* start = p_;
    if (p_ < end_ && *p_ == '-') ++p_;
    if (p_ >= end_) {
      Fail("truncated JSON number");
      return false;
    }
    if (*p_ == '0') {
      ++p_;
    } else if (*p_ >= '1' && *p_ <= '9') {
      while (p_ < end_ && *p_ >= '0' && *p_ <= '9') ++p_;
    } else {
      Fail("invalid JSON value");
      return false;
    }
    if (p_ < end_ && *p_ == '.') {
      ++p_;
      if (p_ >= end_ || *p_ < '0' || *p_ > '9') {
        Fail("invalid JSON number (fraction)");
        return false;
      }
      while (p_ < end_ && *p_ >= '0' && *p_ <= '9') ++p_;
    }
    if (p_ < end_ && (*p_ == 'e' || *p_ == 'E')) {
      ++p_;
      if (p_ < end_ && (*p_ == '+' || *p_ == '-')) ++p_;
      if (p_ >= end_ || *p_ < '0' || *p_ > '9') {
        Fail("invalid JSON number (exponent)");
        return false;
      }
      while (p_ < end_ && *p_ >= '0' && *p_ <= '9') ++p_;
    }
    const std::string token(start, static_cast<size_t>(p_ - start));
    const double v = std::strtod(token.c_str(), nullptr);
    if (!std::isfinite(v)) {
      Fail("JSON number is not finite");
      return false;
    }
    out.type = JsonValue::Type::kNumber;
    out.number = v;
    return true;
  }

  const char* p_;
  const char* begin_;
  const char* end_;
  std::string err_;
};

// ===========================================================================
// 2. SHA-256 (file-local) — the manifest records the blob's content hash.
// ===========================================================================

class Sha256 {
 public:
  void Update(const uint8_t* data, size_t len) {
    total_ += len;
    while (len > 0) {
      const size_t n = std::min(len, sizeof(buf_) - fill_);
      std::memcpy(buf_ + fill_, data, n);
      fill_ += n;
      data += n;
      len -= n;
      if (fill_ == sizeof(buf_)) {
        Block(buf_);
        fill_ = 0;
      }
    }
  }

  void Finish(uint8_t out[32]) {
    const uint64_t bits = total_ * 8u;
    buf_[fill_++] = 0x80;
    if (fill_ > 56) {
      while (fill_ < 64) buf_[fill_++] = 0;
      Block(buf_);
      fill_ = 0;
    }
    while (fill_ < 56) buf_[fill_++] = 0;
    for (int i = 7; i >= 0; --i)
      buf_[fill_++] = static_cast<uint8_t>((bits >> (8 * i)) & 0xFFu);
    Block(buf_);
    for (int i = 0; i < 8; ++i) {
      out[i * 4 + 0] = static_cast<uint8_t>((h_[i] >> 24) & 0xFFu);
      out[i * 4 + 1] = static_cast<uint8_t>((h_[i] >> 16) & 0xFFu);
      out[i * 4 + 2] = static_cast<uint8_t>((h_[i] >> 8) & 0xFFu);
      out[i * 4 + 3] = static_cast<uint8_t>(h_[i] & 0xFFu);
    }
  }

  static std::string Hex(const uint8_t digest[32]) {
    static const char* kHex = "0123456789abcdef";
    std::string s;
    s.reserve(64);
    for (int i = 0; i < 32; ++i) {
      s.push_back(kHex[(digest[i] >> 4) & 0xF]);
      s.push_back(kHex[digest[i] & 0xF]);
    }
    return s;
  }

 private:
  static uint32_t Ror(uint32_t v, int n) {
    return (v >> n) | (v << (32 - n));
  }

  void Block(const uint8_t* p) {
    static const uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu,
        0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u,
        0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u,
        0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u,
        0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
        0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u,
        0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u, 0x1e376c08u,
        0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu,
        0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint32_t>(p[i * 4 + 0]) << 24) |
             (static_cast<uint32_t>(p[i * 4 + 1]) << 16) |
             (static_cast<uint32_t>(p[i * 4 + 2]) << 8) |
             static_cast<uint32_t>(p[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = Ror(w[i - 15], 7) ^ Ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = Ror(w[i - 2], 17) ^ Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t s1 = Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25);
      const uint32_t ch = (e & f) ^ (~e & g);
      const uint32_t t1 = hh + s1 + ch + k[i] + w[i];
      const uint32_t s0 = Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t t2 = s0 + maj;
      hh = g; g = f; f = e; e = d + t1;
      d = c; c = b; b = a; a = t1 + t2;
    }
    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
    h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
  }

  uint32_t h_[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  uint8_t buf_[64] = {};
  size_t fill_ = 0;
  uint64_t total_ = 0;
};

std::string Sha256Hex(const uint8_t* data, size_t len) {
  Sha256 h;
  h.Update(data, len);
  uint8_t digest[32];
  h.Finish(digest);
  return Sha256::Hex(digest);
}

// ===========================================================================
// 3. CPU DXT decoders.
//
// These are a byte-for-byte port of tools/arena/rldds.py (the reference
// decoder of the ripper): same RGB565 expansion, same integer interpolation
// (floor division, not rounding), same 1-bit-alpha mode for DXT1 and the same
// interpolated alpha ramp for DXT5.
// ===========================================================================

// 5 bits -> 8 bits: replicate the high bits into the low ones (0x1F -> 255).
inline int Expand5(int v) { return (v << 3) | (v >> 2); }
// 6 bits -> 8 bits: replicate the high bits into the low ones (0x3F -> 255).
inline int Expand6(int v) { return (v << 2) | (v >> 4); }

struct Rgb565 {
  int r, g, b;
};

inline Rgb565 Decode565(uint16_t c) {
  Rgb565 o;
  o.r = Expand5((c >> 11) & 0x1F);
  o.g = Expand6((c >> 5) & 0x3F);
  o.b = Expand5(c & 0x1F);
  return o;
}

inline uint16_t LoadU16(const uint8_t* p) {
  return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                               (static_cast<uint16_t>(p[1]) << 8));
}

inline uint32_t LoadU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t LoadU48(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 5; i >= 0; --i)
    v = (v << 8) | static_cast<uint64_t>(p[i]);
  return v;
}

// Decode one 8-byte S3TC colour block into the RGBA image at block (bx, by).
//
// `allow_1bit_alpha` picks BC1's three-colour + transparent-black mode when
// c0 <= c1; DXT5 passes false (its colour block is always the four-colour
// mode).  `write_alpha` is false for DXT5, whose alpha comes from the
// separate alpha block and must not be overwritten.
void DecodeColourBlock(const uint8_t* blk, uint8_t* out, int w, int h, int bx,
                        int by, bool allow_1bit_alpha, bool write_alpha) {
  const uint16_t c0 = LoadU16(blk);
  const uint16_t c1 = LoadU16(blk + 2);
  const uint32_t bits = LoadU32(blk + 4);
  const Rgb565 p0 = Decode565(c0);
  const Rgb565 p1 = Decode565(c1);
  int pal[4][4];
  if (c0 > c1 || !allow_1bit_alpha) {
    pal[0][0] = p0.r; pal[0][1] = p0.g; pal[0][2] = p0.b; pal[0][3] = 255;
    pal[1][0] = p1.r; pal[1][1] = p1.g; pal[1][2] = p1.b; pal[1][3] = 255;
    pal[2][0] = (2 * p0.r + p1.r) / 3;
    pal[2][1] = (2 * p0.g + p1.g) / 3;
    pal[2][2] = (2 * p0.b + p1.b) / 3;
    pal[2][3] = 255;
    pal[3][0] = (p0.r + 2 * p1.r) / 3;
    pal[3][1] = (p0.g + 2 * p1.g) / 3;
    pal[3][2] = (p0.b + 2 * p1.b) / 3;
    pal[3][3] = 255;
  } else {
    pal[0][0] = p0.r; pal[0][1] = p0.g; pal[0][2] = p0.b; pal[0][3] = 255;
    pal[1][0] = p1.r; pal[1][1] = p1.g; pal[1][2] = p1.b; pal[1][3] = 255;
    pal[2][0] = (p0.r + p1.r) / 2;
    pal[2][1] = (p0.g + p1.g) / 2;
    pal[2][2] = (p0.b + p1.b) / 2;
    pal[2][3] = 255;
    pal[3][0] = 0; pal[3][1] = 0; pal[3][2] = 0; pal[3][3] = 0;
  }
  for (int py = 0; py < 4; ++py) {
    const int y = by * 4 + py;
    if (y >= h) break;
    const uint32_t row = bits >> (8 * py);
    for (int px = 0; px < 4; ++px) {
      const int x = bx * 4 + px;
      if (x >= w) break;
      const int idx = static_cast<int>((row >> (2 * px)) & 3u);
      const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) +
                        static_cast<size_t>(x)) * 4u;
      out[i + 0] = static_cast<uint8_t>(pal[idx][0]);
      out[i + 1] = static_cast<uint8_t>(pal[idx][1]);
      out[i + 2] = static_cast<uint8_t>(pal[idx][2]);
      if (write_alpha) out[i + 3] = static_cast<uint8_t>(pal[idx][3]);
    }
  }
}

// DXT5: a0, a1 endpoints + 16 three-bit indices (an 8-step alpha ramp).
void DecodeDxt5Alpha(const uint8_t* blk, uint8_t* out, int w, int h, int bx,
                      int by) {
  const int a0 = blk[0];
  const int a1 = blk[1];
  const uint64_t bits = LoadU48(blk + 2);
  int ramp[8];
  if (a0 > a1) {
    ramp[0] = a0;
    ramp[1] = a1;
    ramp[2] = (6 * a0 + 1 * a1) / 7;
    ramp[3] = (5 * a0 + 2 * a1) / 7;
    ramp[4] = (4 * a0 + 3 * a1) / 7;
    ramp[5] = (3 * a0 + 4 * a1) / 7;
    ramp[6] = (2 * a0 + 5 * a1) / 7;
    ramp[7] = (1 * a0 + 6 * a1) / 7;
  } else {
    ramp[0] = a0;
    ramp[1] = a1;
    ramp[2] = (4 * a0 + 1 * a1) / 5;
    ramp[3] = (3 * a0 + 2 * a1) / 5;
    ramp[4] = (2 * a0 + 3 * a1) / 5;
    ramp[5] = (1 * a0 + 4 * a1) / 5;
    ramp[6] = 0;
    ramp[7] = 255;
  }
  for (int py = 0; py < 4; ++py) {
    const int y = by * 4 + py;
    if (y >= h) break;
    for (int px = 0; px < 4; ++px) {
      const int x = bx * 4 + px;
      if (x >= w) break;
      const int idx = static_cast<int>((bits >> (3 * (py * 4 + px))) & 7u);
      const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) +
                        static_cast<size_t>(x)) * 4u;
      out[i + 3] = static_cast<uint8_t>(ramp[idx]);
    }
  }
}

inline int BlockCount(int extent) { return std::max(1, (extent + 3) / 4); }

// DXT1 (BC1): 8 bytes per 4x4 block, three or four colours + 1-bit alpha.
bool DecodeDxt1(const uint8_t* data, size_t len, int w, int h,
                 std::vector<uint8_t>& out) {
  const size_t need = static_cast<size_t>(BlockCount(w)) *
                      static_cast<size_t>(BlockCount(h)) * 8u;
  if (len < need) return false;
  out.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0);
  size_t off = 0;
  for (int by = 0; by < BlockCount(h); ++by) {
    for (int bx = 0; bx < BlockCount(w); ++bx) {
      DecodeColourBlock(data + off, out.data(), w, h, bx, by, true, true);
      off += 8;
    }
  }
  return true;
}

// DXT5 (BC3): 16 bytes per 4x4 block — an interpolated alpha block followed
// by a four-colour DXT1-style block.
bool DecodeDxt5(const uint8_t* data, size_t len, int w, int h,
                std::vector<uint8_t>& out) {
  const size_t need = static_cast<size_t>(BlockCount(w)) *
                      static_cast<size_t>(BlockCount(h)) * 16u;
  if (len < need) return false;
  out.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0);
  size_t off = 0;
  for (int by = 0; by < BlockCount(h); ++by) {
    for (int bx = 0; bx < BlockCount(w); ++bx) {
      DecodeDxt5Alpha(data + off, out.data(), w, h, bx, by);
      DecodeColourBlock(data + off + 8, out.data(), w, h, bx, by, false, false);
      off += 16;
    }
  }
  return true;
}

// ===========================================================================
// 4. Small helpers for the manifest.
// ===========================================================================

std::string JoinPath(const std::string& dir, const char* file) {
  if (dir.empty()) return std::string(file);
  if (dir.back() == '/') return dir + file;
  return dir + "/" + file;
}

bool ReadWholeFile(const std::string& path, std::string& out,
                   std::string& err) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    err = "cannot open '" + path + "'";
    return false;
  }
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  if (in.bad()) {
    err = "read error on '" + path + "'";
    return false;
  }
  return true;
}

// A non-negative integer field of a JSON object.
bool GetUInt(const JsonValue& obj, const char* key, uint64_t& out) {
  const JsonValue* v = obj.Find(key);
  if (!v || !v->IsNumber()) return false;
  const double d = v->number;
  if (!(d >= 0.0) || d > 9.0e15) return false;
  out = static_cast<uint64_t>(d);
  return true;
}

bool GetInt(const JsonValue& obj, const char* key, int64_t& out) {
  const JsonValue* v = obj.Find(key);
  if (!v || !v->IsNumber()) return false;
  const double d = v->number;
  if (!(d >= -9.0e15) || d > 9.0e15) return false;
  out = static_cast<int64_t>(d);
  return true;
}

bool GetString(const JsonValue& obj, const char* key, std::string& out) {
  const JsonValue* v = obj.Find(key);
  if (!v || !v->IsString()) return false;
  out = v->str;
  return true;
}

// The {offset, length, count, stride} descriptor of a blob array.
struct ArrayRef {
  uint64_t offset = 0, length = 0, count = 0, stride = 0;
  bool present = false;
};

bool GetArrayRef(const JsonValue& obj, const char* key, ArrayRef& out) {
  const JsonValue* v = obj.Find(key);
  if (!v || !v->IsObject()) return false;
  if (!GetUInt(*v, "offset", out.offset)) return false;
  if (!GetUInt(*v, "length", out.length)) return false;
  if (!GetUInt(*v, "count", out.count)) return false;
  if (!GetUInt(*v, "stride", out.stride)) return false;
  out.present = true;
  return true;
}

}  // namespace

// ===========================================================================
// The loader.
// ===========================================================================

bool LoadArenaPack(const std::string& dir, ArenaPack& out) {
  out = ArenaPack();

  auto fail = [&out](const std::string& msg) {
    out.error = "arena pack: " + msg;
    return false;
  };

  // ---- 1. the manifest ----------------------------------------------------
  const std::string manifestPath = JoinPath(dir, "arena.json");
  std::string jsonText;
  {
    std::string ioErr;
    if (!ReadWholeFile(manifestPath, jsonText, ioErr)) {
      std::error_code ec;
      const bool exists = std::filesystem::exists(manifestPath, ec);
      if (!exists) {
        return fail("the manifest 'arena.json' was not found in directory '" +
                    dir + "' (expected '" + manifestPath + "')");
      }
      return fail("cannot read the manifest '" + manifestPath + "': " + ioErr);
    }
  }

  JsonValue root;
  {
    std::string parseErr;
    JsonParser parser(jsonText.data(), jsonText.size());
    if (!parser.Parse(root, parseErr)) {
      return fail("the manifest '" + manifestPath + "' is not valid JSON: " +
                  parseErr);
    }
  }
  if (!root.IsObject()) {
    return fail("the manifest '" + manifestPath +
                "' is not a JSON object (it is " +
                (root.IsArray() ? "an array" : "a scalar") + ")");
  }

  // ---- 2. format and version ---------------------------------------------
  {
    std::string format;
    if (!GetString(root, "format", format) || format.empty()) {
      return fail("the manifest '" + manifestPath +
                  "' has no 'format' string (expected \"rl-arena-pack/1\")");
    }
    if (format != "rl-arena-pack/1") {
      return fail("unknown arena pack format '" + format +
                  "' (this reader understands \"rl-arena-pack/1\")");
    }
    out.format = format;
    int64_t version = 0;
    if (!GetInt(root, "format_version", version)) {
      return fail("the manifest '" + manifestPath +
                  "' has no 'format_version' integer");
    }
    if (version != 1) {
      return fail("arena pack version " + std::to_string(version) +
                  " is not supported (this reader understands version 1)");
    }
    out.formatVersion = static_cast<int>(version);
  }

  // The source identity (a basename + a GUID; never a path).
  if (const JsonValue* src = root.Find("source"); src && src->IsObject()) {
    GetString(*src, "name", out.sourcePackage);
    GetString(*src, "guid", out.sourceGuid);
  }

  // ---- 3. the blob --------------------------------------------------------
  const std::string blobPath = JoinPath(dir, "arena.bin");
  std::string blobBytes;
  {
    std::string ioErr;
    if (!ReadWholeFile(blobPath, blobBytes, ioErr)) {
      std::error_code ec;
      const bool exists = std::filesystem::exists(blobPath, ec);
      if (!exists) {
        return fail("the blob 'arena.bin' was not found in directory '" + dir +
                    "' (expected '" + blobPath + "')");
      }
      return fail("cannot read the blob '" + blobPath + "': " + ioErr);
    }
  }
  const uint8_t* blob = reinterpret_cast<const uint8_t*>(blobBytes.data());
  const size_t blobSize = blobBytes.size();

  // ---- 4. the blob's size must match the manifest -------------------------
  uint64_t manifestBlobBytes = 0;
  const JsonValue* blobMeta = root.Find("blob");
  if (blobMeta && blobMeta->IsObject()) {
    if (!GetUInt(*blobMeta, "bytes", manifestBlobBytes)) {
      return fail("the manifest's 'blob.bytes' is missing or not an integer");
    }
    if (manifestBlobBytes != blobSize) {
      return fail("the blob 'arena.bin' is " + std::to_string(blobSize) +
                  " bytes but the manifest records " +
                  std::to_string(manifestBlobBytes) + " bytes");
    }
  }

  // ---- 5. the blob's sha256 (when the manifest records one) ---------------
  if (blobMeta && blobMeta->IsObject()) {
    std::string expected;
    if (GetString(*blobMeta, "sha256", expected) && !expected.empty()) {
      out.blobSha256Checked = true;
      out.blobSha256Expected = expected;
      out.blobSha256Actual = Sha256Hex(blob, blobSize);
      if (out.blobSha256Actual != expected) {
        return fail("the blob 'arena.bin' does not match its recorded sha256 "
                    "(manifest " + expected + ", file " +
                    out.blobSha256Actual + ")");
      }
    }
  }

  // ---- 6. the blob header ------------------------------------------------
  if (blobSize < 32) {
    return fail("the blob 'arena.bin' is only " + std::to_string(blobSize) +
                " bytes (the header alone is 32)");
  }
  {
    static const char kMagic[8] = {'R', 'L', 'A', 'R', 'E', 'N', 'A', 'P'};
    if (std::memcmp(blob, kMagic, 8) != 0) {
      return fail("the blob 'arena.bin' has a bad magic (expected 'RLARENAP')");
    }
    const uint32_t headerVersion = LoadU32(blob + 8);
    if (headerVersion != 1) {
      return fail("the blob header carries format version " +
                  std::to_string(headerVersion) +
                  " (this reader understands version 1)");
    }
    const uint32_t headerSize = LoadU32(blob + 12);
    if (headerSize != 32) {
      return fail("the blob header size is " + std::to_string(headerSize) +
                  " (expected 32)");
    }
    uint64_t totalSize = 0;
    for (int i = 0; i < 8; ++i)
      totalSize |= static_cast<uint64_t>(blob[16 + i]) << (8 * i);
    if (totalSize != blobSize) {
      return fail("the blob header says the blob is " +
                  std::to_string(totalSize) + " bytes but the file is " +
                  std::to_string(blobSize) + " bytes");
    }
    const uint32_t flags = LoadU32(blob + 28);
    if (flags != 0) {
      return fail("the blob header carries the reserved flags word " +
                  std::to_string(flags) + " (expected 0)");
    }
    if (blobMeta && blobMeta->IsObject()) {
      uint64_t arrayCount = 0;
      if (GetUInt(*blobMeta, "array_count", arrayCount) &&
          arrayCount != LoadU32(blob + 24)) {
        return fail("the blob header says " + std::to_string(LoadU32(blob + 24)) +
                    " arrays but the manifest records " +
                    std::to_string(arrayCount));
      }
    }
  }

  // The six group arrays (positions/normals/tangents/uvs/indices/matrices) are
  // checked when the manifest declares them: they must be 4-byte aligned and
  // inside the blob.
  struct GroupArray {
    ArrayRef ref;
    bool present = false;
  };
  GroupArray groups[6];
  static const char* kGroupNames[6] = {"positions", "normals", "tangents",
                                        "uvs", "indices", "matrices"};
  if (const JsonValue* arrays = root.Find("arrays"); arrays && arrays->IsObject()) {
    for (int i = 0; i < 6; ++i) {
      if (!GetArrayRef(*arrays, kGroupNames[i], groups[i].ref)) continue;
      groups[i].present = true;
      const ArrayRef& r = groups[i].ref;
      if ((r.offset % 4u) != 0u) {
        return fail(std::string("the '") + kGroupNames[i] +
                    "' array is not 4-byte aligned (offset " +
                    std::to_string(r.offset) + ")");
      }
      if (r.offset > blobSize || r.length > blobSize - r.offset) {
        return fail(std::string("the '") + kGroupNames[i] +
                    "' array lies outside the blob (offset " +
                    std::to_string(r.offset) + ", length " +
                    std::to_string(r.length) + ", blob " +
                    std::to_string(blobSize) + " bytes)");
      }
      if (r.stride != 0 && r.length != r.count * r.stride) {
        return fail(std::string("the '") + kGroupNames[i] +
                    "' array length (" + std::to_string(r.length) +
                    ") does not match count*stride (" +
                    std::to_string(r.count) + "*" + std::to_string(r.stride) +
                    ")");
      }
    }
  }

  // ---- 7. the textures ----------------------------------------------------
  if (const JsonValue* textures = root.Find("textures"); textures && textures->IsArray()) {
    for (size_t ti = 0; ti < textures->arr.size(); ++ti) {
      const JsonValue& row = textures->arr[ti];
      const std::string where =
          "texture #" + std::to_string(ti) +
          (row.IsObject() && row.Find("name") && row.Find("name")->IsString()
               ? " ('" + row.Find("name")->str + "')"
               : "");
      ArenaTexture tex;
      if (row.Find("name") && row.Find("name")->IsString())
        tex.name = row.Find("name")->str;

      int64_t w = 0, h = 0;
      if (!GetInt(row, "width", w) || !GetInt(row, "height", h) || w <= 0 || h <= 0) {
        return fail(where + " has no valid 'width'/'height'");
      }
      tex.width = static_cast<int>(w);
      tex.height = static_cast<int>(h);

      std::string format, storage;
      if (!GetString(row, "format", format)) {
        return fail(where + " has no 'format' string");
      }
      if (!GetString(row, "storage", storage)) storage = "compressed";

      ArrayRef payload;
      if (!GetArrayRef(row, "payload", payload)) {
        // The pack's texture rows carry offset/length/bytes directly.
        if (!GetUInt(row, "offset", payload.offset) ||
            !GetUInt(row, "length", payload.length)) {
          return fail(where + " has no 'offset'/'length' for its payload");
        }
        payload.count = payload.length;
        payload.stride = 1;
      }
      if (payload.offset > blobSize || payload.length > blobSize - payload.offset) {
        return fail(where + " lies outside the blob (offset " +
                    std::to_string(payload.offset) + ", length " +
                    std::to_string(payload.length) + ", blob " +
                    std::to_string(blobSize) + " bytes)");
      }
      uint64_t declaredBytes = 0;
      if (GetUInt(row, "bytes", declaredBytes) && declaredBytes != payload.length) {
        return fail(where + " declares " + std::to_string(declaredBytes) +
                    " bytes but its byte range is " +
                    std::to_string(payload.length) + " bytes");
      }

      const size_t expectedRgba =
          static_cast<size_t>(tex.width) * static_cast<size_t>(tex.height) * 4u;
      uint64_t rgba8Bytes = 0;
      if (GetUInt(row, "rgba8_bytes", rgba8Bytes) && rgba8Bytes != expectedRgba) {
        return fail(where + " declares " + std::to_string(rgba8Bytes) +
                    " RGBA8 bytes but " + std::to_string(tex.width) + "x" +
                    std::to_string(tex.height) + " needs " +
                    std::to_string(expectedRgba));
      }

      // The payload's hash (the manifest records it per texture).
      std::string payloadHash;
      if (GetString(row, "payload_sha256", payloadHash) && !payloadHash.empty()) {
        const std::string actual =
            Sha256Hex(blob + payload.offset, static_cast<size_t>(payload.length));
        if (actual != payloadHash) {
          return fail(where + " does not match its recorded payload sha256 "
                    "(manifest " + payloadHash + ", blob " + actual + ")");
        }
      }

      const uint8_t* data = blob + payload.offset;
      const size_t len = static_cast<size_t>(payload.length);
      if (storage == "rgba8") {
        if (len != expectedRgba) {
          return fail(where + " stores " + std::to_string(len) +
                      " RGBA8 bytes but needs " + std::to_string(expectedRgba));
        }
        tex.format = ArenaTexFormat::kRgba8;
        tex.rgba.assign(data, data + len);
      } else if (storage == "compressed") {
        if (format == "PF_DXT1") {
          tex.format = ArenaTexFormat::kDxt1;
          const size_t need = static_cast<size_t>(BlockCount(tex.width)) *
                              static_cast<size_t>(BlockCount(tex.height)) * 8u;
          if (len != need) {
            return fail(where + " is " + std::to_string(len) +
                        " compressed bytes but a " + std::to_string(tex.width) +
                        "x" + std::to_string(tex.height) +
                        " PF_DXT1 texture needs " + std::to_string(need));
          }
          if (!DecodeDxt1(data, len, tex.width, tex.height, tex.rgba)) {
            return fail(where + " could not be decoded as PF_DXT1");
          }
        } else if (format == "PF_DXT5") {
          tex.format = ArenaTexFormat::kDxt5;
          const size_t need = static_cast<size_t>(BlockCount(tex.width)) *
                              static_cast<size_t>(BlockCount(tex.height)) * 16u;
          if (len != need) {
            return fail(where + " is " + std::to_string(len) +
                        " compressed bytes but a " + std::to_string(tex.width) +
                        "x" + std::to_string(tex.height) +
                        " PF_DXT5 texture needs " + std::to_string(need));
          }
          if (!DecodeDxt5(data, len, tex.width, tex.height, tex.rgba)) {
            return fail(where + " could not be decoded as PF_DXT5");
          }
        } else {
          return fail(where + " has the unsupported compressed pixel format '" +
                      format + "' (this reader decodes PF_DXT1 and PF_DXT5)");
        }
      } else {
        return fail(where + " has the unknown storage mode '" + storage + "'");
      }

      if (tex.rgba.size() != expectedRgba) {
        return fail(where + " decoded to " + std::to_string(tex.rgba.size()) +
                    " bytes but must be " + std::to_string(expectedRgba));
      }
      tex.valid = true;
      out.textures.push_back(std::move(tex));
    }
  }

  // ---- 8. the meshes ------------------------------------------------------
  // `loadMeshTable` reads one mesh table (the package's own exports, or the
  // meshes the package only imports) into `dst`.
  auto loadMeshTable = [&](const JsonValue& table, std::vector<ArenaMesh>& dst,
                            std::map<std::string, uint32_t>& index,
                            const char* what) -> bool {
    for (size_t mi = 0; mi < table.arr.size(); ++mi) {
      const JsonValue& row = table.arr[mi];
      std::string name;
      if (!GetString(row, "name", name) || name.empty()) {
        return fail(std::string("a ") + what + " mesh has no name");
      }
      ArenaMesh mesh;
      mesh.name = name;

      int64_t numVerts = 0, numTris = 0, numIndices = 0;
      if (!GetInt(row, "num_verts", numVerts) || numVerts < 0) {
        return fail("the mesh '" + name + "' has no valid 'num_verts'");
      }
      if (!GetInt(row, "num_tris", numTris) || numTris < 0) {
        return fail("the mesh '" + name + "' has no valid 'num_tris'");
      }
      if (!GetInt(row, "num_indices", numIndices) || numIndices < 0) {
        return fail("the mesh '" + name + "' has no valid 'num_indices'");
      }
      if (numIndices % 3 != 0) {
        return fail("the mesh '" + name + "' has " + std::to_string(numIndices) +
                    " indices, which is not a multiple of 3");
      }
      if (numIndices != numTris * 3) {
        return fail("the mesh '" + name + "' has " + std::to_string(numTris) +
                    " triangles (" + std::to_string(numIndices) +
                    " indices) — the counts disagree");
      }

      const JsonValue* arrays = row.Find("arrays");
      if (!arrays || !arrays->IsObject()) {
        return fail("the mesh '" + name + "' has no 'arrays' object");
      }
      ArrayRef positions, normals, uvs, indices;
      if (!GetArrayRef(*arrays, "positions", positions) ||
          !GetArrayRef(*arrays, "normals", normals) ||
          !GetArrayRef(*arrays, "uvs", uvs) ||
          !GetArrayRef(*arrays, "indices", indices)) {
        return fail("the mesh '" + name +
                    "' does not declare all of its arrays");
      }
      if (positions.count != static_cast<uint64_t>(numVerts) ||
          normals.count != static_cast<uint64_t>(numVerts) ||
          uvs.count != static_cast<uint64_t>(numVerts)) {
        return fail("the mesh '" + name + "' declares " +
                    std::to_string(numVerts) + " vertices but its arrays hold " +
                    std::to_string(positions.count) + " positions / " +
                    std::to_string(normals.count) + " normals / " +
                    std::to_string(uvs.count) + " uvs");
      }
      if (indices.count != static_cast<uint64_t>(numIndices)) {
        return fail("the mesh '" + name + "' declares " +
                    std::to_string(numIndices) + " indices but its index array "
                    "holds " + std::to_string(indices.count));
      }
      if (positions.stride != 12 || normals.stride != 12 || uvs.stride != 8 ||
          indices.stride != 2) {
        return fail("the mesh '" + name + "' has an unexpected array stride");
      }

      struct Slice {
        const ArrayRef* ref;
        const char* name;
        uint64_t group;  // index into `groups`, or 0xFF for none
      };
      const Slice slices[4] = {{&positions, "positions", 0},
                                {&normals, "normals", 1},
                                {&uvs, "uvs", 3},
                                {&indices, "indices", 4}};
      for (const Slice& s : slices) {
        const ArrayRef& r = *s.ref;
        if (r.offset > blobSize || r.length > blobSize - r.offset) {
          return fail("the mesh '" + name + "' has its " + s.name +
                      " array outside the blob (offset " +
                      std::to_string(r.offset) + ", length " +
                      std::to_string(r.length) + ", blob " +
                      std::to_string(blobSize) + " bytes)");
        }
        if (r.length != r.count * r.stride) {
          return fail("the mesh '" + name + "' has a " + s.name +
                      " array whose length does not match count*stride");
        }
        if (s.group < 6 && groups[s.group].present) {
          const ArrayRef& g = groups[s.group].ref;
          if (r.offset < g.offset ||
              r.offset + r.length > g.offset + g.length) {
            return fail("the mesh '" + name + "' has its " + s.name +
                        " array outside the pack's '" + kGroupNames[s.group] +
                        "' array");
          }
        }
      }

      // Read the geometry.
      mesh.positions.resize(static_cast<size_t>(numVerts) * 3u);
      for (int64_t v = 0; v < numVerts; ++v) {
        for (int c = 0; c < 3; ++c) {
          uint32_t bits = LoadU32(blob + positions.offset +
                                  static_cast<size_t>(v) * 12u +
                                  static_cast<size_t>(c) * 4u);
          float f;
          std::memcpy(&f, &bits, 4);
          if (!std::isfinite(f)) {
            return fail("the mesh '" + name + "' has a non-finite position");
          }
          mesh.positions[static_cast<size_t>(v) * 3u +
                         static_cast<size_t>(c)] = f;
        }
      }
      mesh.normals.resize(static_cast<size_t>(numVerts) * 3u);
      for (int64_t v = 0; v < numVerts; ++v) {
        for (int c = 0; c < 3; ++c) {
          uint32_t bits = LoadU32(blob + normals.offset +
                                  static_cast<size_t>(v) * 12u +
                                  static_cast<size_t>(c) * 4u);
          float f;
          std::memcpy(&f, &bits, 4);
          if (!std::isfinite(f)) {
            return fail("the mesh '" + name + "' has a non-finite normal");
          }
          mesh.normals[static_cast<size_t>(v) * 3u +
                       static_cast<size_t>(c)] = f;
        }
      }
      mesh.uvs.resize(static_cast<size_t>(numVerts) * 2u);
      for (int64_t v = 0; v < numVerts; ++v) {
        for (int c = 0; c < 2; ++c) {
          uint32_t bits = LoadU32(blob + uvs.offset +
                                  static_cast<size_t>(v) * 8u +
                                  static_cast<size_t>(c) * 4u);
          float f;
          std::memcpy(&f, &bits, 4);
          if (!std::isfinite(f)) {
            return fail("the mesh '" + name + "' has a non-finite uv");
          }
          mesh.uvs[static_cast<size_t>(v) * 2u + static_cast<size_t>(c)] = f;
        }
      }
      mesh.indices.resize(static_cast<size_t>(numIndices));
      for (int64_t i = 0; i < numIndices; ++i) {
        const uint16_t v = LoadU16(blob + indices.offset +
                                   static_cast<size_t>(i) * 2u);
        if (v >= static_cast<uint16_t>(numVerts)) {
          return fail("the mesh '" + name + "' has the out-of-range index " +
                      std::to_string(v) + " (it has " +
                      std::to_string(numVerts) + " vertices)");
        }
        mesh.indices[static_cast<size_t>(i)] = v;
      }

      // The sections.
      if (const JsonValue* sections = row.Find("sections");
          sections && sections->IsArray()) {
        for (size_t si = 0; si < sections->arr.size(); ++si) {
          const JsonValue& srow = sections->arr[si];
          int64_t firstIndex = 0, numTriangles = 0;
          if (!GetInt(srow, "first_index", firstIndex) || firstIndex < 0) {
            return fail("a section of the mesh '" + name +
                        "' has no valid 'first_index'");
          }
          if (!GetInt(srow, "num_triangles", numTriangles) || numTriangles < 0) {
            return fail("a section of the mesh '" + name +
                        "' has no valid 'num_triangles'");
          }
          const int64_t last = firstIndex + numTriangles * 3;
          if (last > numIndices) {
            return fail("a section of the mesh '" + name + "' covers the index "
                        "range [" + std::to_string(firstIndex) + ", " +
                        std::to_string(last) + ") but the mesh only has " +
                        std::to_string(numIndices) + " indices");
          }
          ArenaSection section;
          section.firstIndex = static_cast<uint32_t>(firstIndex);
          section.numTriangles = static_cast<uint32_t>(numTriangles);
          section.materialSlot = static_cast<int>(si);  // slot i == section i
          mesh.sections.push_back(section);
        }
      }

      const uint32_t meshIndex = static_cast<uint32_t>(dst.size());
      // A mesh NAME is not unique in a UE3 package: the DFH Stadium day pack
      // holds EIGHT exports called "Grass" (the ground tiles).  The pack's
      // instance rows name their mesh, not its export index, so the name join
      // is ambiguous for them.  The reader keeps the FIRST mesh of a name in
      // the index map (deterministic: the manifest's table order is fixed),
      // leaves every mesh in the table (reachable by index) and counts the
      // ambiguity so the caller can see it.
      if (index.count(name) != 0) {
        ++out.duplicateMeshNames;
      } else {
        index.emplace(name, meshIndex);
      }
      dst.push_back(std::move(mesh));
    }
    return true;
  };

  if (const JsonValue* meshes = root.Find("meshes");
      meshes && meshes->IsArray()) {
    if (!loadMeshTable(*meshes, out.meshes, out.meshIndexByName, "package")) {
      return false;
    }
  }
  if (const JsonValue* extMeshes = root.Find("external_meshes");
      extMeshes && extMeshes->IsArray()) {
    if (!loadMeshTable(*extMeshes, out.externalMeshes,
                       out.externalMeshIndexByName, "external")) {
      return false;
    }
  }
  for (const ArenaMesh& m : out.meshes) out.sectionCount += m.sections.size();
  for (const ArenaMesh& m : out.externalMeshes)
    out.sectionCount += m.sections.size();

  // ---- 9. the materials ---------------------------------------------------
  // The material table is keyed by EXPORT (a name is not unique in the
  // package), so the rows are kept in the manifest's order and the name ->
  // index map takes the FIRST row of a name (the pack's own rows are unique
  // in name for the DFH Stadium day pack: 83 distinct names, 83 rows).
  std::map<std::string, int> materialByName;
  if (const JsonValue* materials = root.Find("materials");
      materials && materials->IsArray()) {
    for (size_t mi = 0; mi < materials->arr.size(); ++mi) {
      const JsonValue& row = materials->arr[mi];
      ArenaMaterial mat;
      const JsonValue* name = row.Find("name");
      if (name && name->IsString()) mat.name = name->str;
      mat.texture = -1;
      const JsonValue* ti = row.Find("texture_index");
      if (ti && ti->IsNumber()) {
        const double d = ti->number;
        if (d < 0.0 || d >= static_cast<double>(out.textures.size())) {
          return fail("the material '" +
                      (mat.name.empty() ? std::string("(unnamed)") : mat.name) +
                      "' points at the texture index " +
                      std::to_string(static_cast<long long>(d)) +
                      " but the pack only has " +
                      std::to_string(out.textures.size()) + " textures");
        }
        mat.texture = static_cast<int>(d);
      }
      if (!mat.name.empty()) materialByName.emplace(mat.name,
                                                     static_cast<int>(out.materials.size()));
      out.materials.push_back(std::move(mat));
    }
  }

  // ---- 10. the instances --------------------------------------------------
  auto loadInstanceTable = [&](const JsonValue& table,
                               std::vector<ArenaInstance>& dst,
                               const std::map<std::string, uint32_t>& meshIndex,
                               const std::map<std::string, uint32_t>& extIndex) -> bool {
    for (size_t ii = 0; ii < table.arr.size(); ++ii) {
      const JsonValue& row = table.arr[ii];
      ArenaInstance inst;
      if (!GetString(row, "mesh", inst.mesh) || inst.mesh.empty()) {
        return fail("an instance has no 'mesh' name");
      }
      const bool known = meshIndex.count(inst.mesh) != 0 ||
                         extIndex.count(inst.mesh) != 0;
      if (!known) {
        return fail("the instance " + std::to_string(ii) +
                    " names the unknown mesh '" + inst.mesh + "'");
      }
      const uint32_t sectionCount =
          meshIndex.count(inst.mesh)
              ? out.meshes[meshIndex.at(inst.mesh)].sections.size()
              : out.externalMeshes[extIndex.at(inst.mesh)].sections.size();

      // The world matrix.
      const JsonValue* matrix = row.Find("matrix");
      if (!matrix || !matrix->IsObject()) {
        return fail("the instance of the mesh '" + inst.mesh +
                    "' has no 'matrix' object");
      }
      std::string layout;
      if (GetString(*matrix, "layout", layout) && layout != "column-major") {
        return fail("the instance of the mesh '" + inst.mesh +
                    "' has the matrix layout '" + layout +
                    "' (expected \"column-major\")");
      }
      uint64_t offset = 0, length = 0, count = 1, values = 16;
      if (!GetUInt(*matrix, "offset", offset) ||
          !GetUInt(*matrix, "length", length)) {
        return fail("the instance of the mesh '" + inst.mesh +
                    "' has no matrix offset/length");
      }
      GetUInt(*matrix, "count", count);
      GetUInt(*matrix, "values", values);
      if (count != 1 || values != 16) {
        return fail("the instance of the mesh '" + inst.mesh + "' declares " +
                    std::to_string(count) + " matrices of " +
                    std::to_string(values) + " values (expected 1 x 16)");
      }
      if (offset > blobSize || length > blobSize - offset ||
          length < 16u * 8u) {
        return fail("the matrix of the instance of the mesh '" + inst.mesh +
                    "' lies outside the blob (offset " +
                    std::to_string(offset) + ", length " +
                    std::to_string(length) + ", blob " +
                    std::to_string(blobSize) + " bytes)");
      }
      for (int c = 0; c < 16; ++c) {
        uint64_t bits = 0;
        for (int i = 0; i < 8; ++i)
          bits |= static_cast<uint64_t>(blob[offset + static_cast<size_t>(c) * 8u +
                                            static_cast<size_t>(i)]) << (8 * i);
        double d;
        std::memcpy(&d, &bits, 8);
        if (!std::isfinite(d)) {
          return fail("the matrix of the instance of the mesh '" + inst.mesh +
                      "' holds a non-finite value");
        }
        inst.matrix[c] = static_cast<float>(d);
        if (!std::isfinite(inst.matrix[c])) {
          return fail("the matrix of the instance of the mesh '" + inst.mesh +
                      "' holds a value that is not finite in float");
        }
      }

      // The owning actor (optional; empty when the pack omits it).
      // DecalBlockingActor_TA / FXActor_TA placements are invisible gameplay
      // helpers, never visible geometry.
      GetString(row, "owner", inst.owner);

      // The material slots: one per section of the placed mesh.
      inst.materialSlots.assign(sectionCount, -1);
      if (const JsonValue* slots = row.Find("material_slots");
          slots && slots->IsArray()) {
        for (size_t si = 0; si < slots->arr.size(); ++si) {
          const JsonValue& srow = slots->arr[si];
          int64_t slot = static_cast<int64_t>(si);
          GetInt(srow, "slot", slot);
          if (slot < 0 || slot >= static_cast<int64_t>(sectionCount)) continue;
          const JsonValue* matName = srow.Find("material");
          if (!matName || !matName->IsString() || matName->str.empty()) continue;
          auto it = materialByName.find(matName->str);
          if (it != materialByName.end())
            inst.materialSlots[static_cast<size_t>(slot)] = it->second;
        }
      }

      dst.push_back(std::move(inst));
    }
    return true;
  };

  if (const JsonValue* instances = root.Find("instances");
      instances && instances->IsArray()) {
    if (!loadInstanceTable(*instances, out.instances, out.meshIndexByName,
                           out.externalMeshIndexByName)) {
      return false;
    }
  }
  if (const JsonValue* extInstances = root.Find("external_instances");
      extInstances && extInstances->IsArray()) {
    if (!loadInstanceTable(*extInstances, out.externalInstances,
                           out.meshIndexByName, out.externalMeshIndexByName)) {
      return false;
    }
  }
  for (const ArenaInstance& inst : out.instances) {
    auto it = out.meshIndexByName.find(inst.mesh);
    if (it != out.meshIndexByName.end())
      out.usedSectionSlots += out.meshes[it->second].sections.size();
  }

  // ---- 11. the manifest's own counts must agree with the tables ----------
  if (const JsonValue* counts = root.Find("counts"); counts && counts->IsObject()) {
    struct CountCheck {
      const char* key;
      size_t actual;
    };
    const CountCheck checks[] = {
        {"meshes", out.meshes.size()},
        {"external_meshes", out.externalMeshes.size()},
        {"instances", out.instances.size()},
        {"external_instances", out.externalInstances.size()},
        {"materials", out.materials.size()},
        {"textures", out.textures.size()},
        {"sections", out.sectionCount},
        {"used_section_slots", out.usedSectionSlots},
    };
    for (const CountCheck& c : checks) {
      uint64_t declared = 0;
      if (!GetUInt(*counts, c.key, declared)) continue;
      if (declared != c.actual) {
        return fail(std::string("the manifest counts '") + c.key + "' as " +
                    std::to_string(declared) + " but the tables hold " +
                    std::to_string(c.actual));
      }
    }
  }

  out.error.clear();
  return true;
}

bool ComputeInstanceAabb(const ArenaPack& p, const ArenaInstance& inst,
                         float lo[3], float hi[3]) {
  const ArenaMesh* mesh = FindArenaMesh(p, inst.mesh);
  if (!mesh) return false;
  if (mesh->positions.size() < 3) return false;
  const double m[16] = {inst.matrix[0],  inst.matrix[1],  inst.matrix[2],
                         inst.matrix[3],  inst.matrix[4],  inst.matrix[5],
                         inst.matrix[6],  inst.matrix[7],  inst.matrix[8],
                         inst.matrix[9],  inst.matrix[10], inst.matrix[11],
                         inst.matrix[12], inst.matrix[13], inst.matrix[14],
                         inst.matrix[15]};
  double low[3] = {1e300, 1e300, 1e300};
  double high[3] = {-1e300, -1e300, -1e300};
  const size_t verts = mesh->positions.size() / 3;
  for (size_t v = 0; v < verts; ++v) {
    const double x = mesh->positions[v * 3 + 0];
    const double y = mesh->positions[v * 3 + 1];
    const double z = mesh->positions[v * 3 + 2];
    const double wx = m[0] * x + m[4] * y + m[8] * z + m[12];
    const double wy = m[1] * x + m[5] * y + m[9] * z + m[13];
    const double wz = m[2] * x + m[6] * y + m[10] * z + m[14];
    if (wx < low[0]) low[0] = wx;
    if (wy < low[1]) low[1] = wy;
    if (wz < low[2]) low[2] = wz;
    if (wx > high[0]) high[0] = wx;
    if (wy > high[1]) high[1] = wy;
    if (wz > high[2]) high[2] = wz;
  }
  for (int c = 0; c < 3; ++c) {
    lo[c] = static_cast<float>(low[c]);
    hi[c] = static_cast<float>(high[c]);
  }
  return true;
}

const char* ArenaPackError(const ArenaPack& p) { return p.error.c_str(); }

const ArenaMesh* FindArenaMesh(const ArenaPack& p, const std::string& name) {
  auto it = p.meshIndexByName.find(name);
  if (it != p.meshIndexByName.end() && it->second < p.meshes.size())
    return &p.meshes[it->second];
  auto it2 = p.externalMeshIndexByName.find(name);
  if (it2 != p.externalMeshIndexByName.end() && it2->second < p.externalMeshes.size())
    return &p.externalMeshes[it2->second];
  return nullptr;
}

}  // namespace rl
