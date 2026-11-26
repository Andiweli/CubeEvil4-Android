// Language packs: reading a ZIP (stored and deflated entries, ZIP64) without a library (language_pack.h).
// The inflater follows RFC 1951 (canonical Huffman codes, a 32 KB window is the output itself).
#include "language_pack.h"

#include <algorithm>
#include <cstring>

namespace re4lang {

namespace {

uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t le64(const uint8_t* p) { return uint64_t(le32(p)) | uint64_t(le32(p + 4)) << 32; }

// ---- inflate ------------------------------------------------------------------------------------------------------

struct Bits {
  const uint8_t* in;
  size_t size, pos = 0;
  uint32_t buf = 0;
  int have = 0;
  bool over = false;
  int need(int n) {
    while (have < n) {
      if (pos >= size) {
        over = true;
        return 0;
      }
      buf |= uint32_t(in[pos++]) << have;
      have += 8;
    }
    int v = int(buf & ((1u << n) - 1));
    buf >>= n;
    have -= n;
    return v;
  }
};

// Canonical Huffman table: counts per length, symbols in code order.
struct Huff {
  uint16_t count[16];
  uint16_t symbol[320];
  bool build(const uint8_t* lengths, int n) {
    memset(count, 0, sizeof(count));
    for (int i = 0; i < n; i++) count[lengths[i]]++;
    count[0] = 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
      left <<= 1;
      left -= count[len];
      if (left < 0) return false;  // over-subscribed
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = uint16_t(offs[len] + count[len]);
    for (int i = 0; i < n; i++) {
      if (lengths[i]) symbol[offs[lengths[i]]++] = uint16_t(i);
    }
    return true;
  }
  int decode(Bits& b) const {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
      code |= b.need(1);
      if (b.over) return -1;
      int c = count[len];
      if (code - c < first) return symbol[index + (code - first)];
      index += c;
      first += c;
      first <<= 1;
      code <<= 1;
    }
    return -1;
  }
};

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                               35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool codes(Bits& b, Bytes& out, size_t expected, const Huff& lit, const Huff& dist) {
  for (;;) {
    int s = lit.decode(b);
    if (s < 0) return false;
    if (s < 256) {
      if (out.size() >= expected) return false;
      out.push_back(uint8_t(s));
    } else if (s == 256) {
      return true;
    } else {
      s -= 257;
      if (s >= 29) return false;
      size_t len = kLenBase[s] + size_t(b.need(kLenExtra[s]));
      int d = dist.decode(b);
      if (d < 0 || d >= 30) return false;
      size_t back = kDistBase[d] + size_t(b.need(kDistExtra[d]));
      if (b.over || back > out.size() || out.size() + len > expected) return false;
      size_t from = out.size() - back;
      for (size_t i = 0; i < len; i++) out.push_back(out[from + i]);
    }
  }
}

}  // namespace

bool Inflate(const uint8_t* in, size_t inSize, Bytes& out, size_t expected) {
  out.clear();
  out.reserve(expected);
  Bits b{in, inSize};
  int last;
  do {
    last = b.need(1);
    int type = b.need(2);
    if (b.over) return false;
    if (type == 0) {  // stored
      b.buf = 0;
      b.have = 0;
      if (b.pos + 4 > b.size) return false;
      uint16_t len = le16(in + b.pos), nlen = le16(in + b.pos + 2);
      b.pos += 4;
      if (uint16_t(~nlen) != len || b.pos + len > b.size || out.size() + len > expected) return false;
      out.insert(out.end(), in + b.pos, in + b.pos + len);
      b.pos += len;
    } else if (type == 1) {  // fixed codes
      static Huff lit, dist;
      static bool made;
      if (!made) {
        uint8_t l[288];
        for (int i = 0; i < 144; i++) l[i] = 8;
        for (int i = 144; i < 256; i++) l[i] = 9;
        for (int i = 256; i < 280; i++) l[i] = 7;
        for (int i = 280; i < 288; i++) l[i] = 8;
        lit.build(l, 288);
        uint8_t d[30];
        for (int i = 0; i < 30; i++) d[i] = 5;
        dist.build(d, 30);
        made = true;
      }
      if (!codes(b, out, expected, lit, dist)) return false;
    } else if (type == 2) {  // dynamic codes
      int nlit = b.need(5) + 257, ndist = b.need(5) + 1, ncode = b.need(4) + 4;
      if (b.over || nlit > 286 || ndist > 30) return false;
      static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
      uint8_t lens[320] = {};
      for (int i = 0; i < ncode; i++) lens[order[i]] = uint8_t(b.need(3));
      Huff lencode;
      if (b.over || !lencode.build(lens, 19)) return false;
      uint8_t all[320] = {};
      for (int i = 0; i < nlit + ndist;) {
        int s = lencode.decode(b);
        if (s < 0) return false;
        if (s < 16) {
          all[i++] = uint8_t(s);
          continue;
        }
        int rep, val = 0;
        if (s == 16) {
          if (i == 0) return false;
          val = all[i - 1];
          rep = 3 + b.need(2);
        } else if (s == 17) {
          rep = 3 + b.need(3);
        } else {
          rep = 11 + b.need(7);
        }
        if (b.over || i + rep > nlit + ndist) return false;
        while (rep--) all[i++] = uint8_t(val);
      }
      if (all[256] == 0) return false;
      Huff lit, dist;
      if (!lit.build(all, nlit) || !dist.build(all + nlit, ndist)) return false;
      if (!codes(b, out, expected, lit, dist)) return false;
    } else {
      return false;
    }
  } while (!last);
  return out.size() == expected;
}

uint32_t Crc32(const uint8_t* p, size_t n) {
  static uint32_t table[256];
  static bool made;
  if (!made) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    made = true;
  }
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

// The end record (last 64 KB + 22 bytes), ZIP64 when the counts or the directory's place overflow, then every
// entry of the central directory.
bool ZipList(const ZipRead& read, uint64_t zipSize, std::vector<ZipEntry>& out, std::string& why) {
  out.clear();
  if (zipSize < 22) {
    why = "not a ZIP file";
    return false;
  }
  size_t tail = size_t(std::min<uint64_t>(zipSize, 0x10000 + 22));
  Bytes end(tail);
  if (!read(zipSize - tail, end.data(), tail)) {
    why = "cannot read the end of the ZIP";
    return false;
  }
  long at = -1;
  for (long i = long(tail) - 22; i >= 0; i--) {
    if (le32(end.data() + i) == 0x06054b50) {
      at = i;
      break;
    }
  }
  if (at < 0) {
    why = "not a ZIP file (no directory at its end)";
    return false;
  }
  uint64_t count = le16(end.data() + at + 10), cdSize = le32(end.data() + at + 12), cdOff = le32(end.data() + at + 16);
  if (count == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOff == 0xFFFFFFFFu) {
    // ZIP64: the locator right before the end record names the ZIP64 end record
    if (at < 20 || le32(end.data() + at - 20) != 0x07064b50) {
      why = "a ZIP64 file without its locator";
      return false;
    }
    uint64_t rec = le64(end.data() + at - 20 + 8);
    uint8_t z[56];
    if (rec + 56 > zipSize || !read(rec, z, 56) || le32(z) != 0x06064b50) {
      why = "a damaged ZIP64 end record";
      return false;
    }
    count = le64(z + 32);
    cdSize = le64(z + 40);
    cdOff = le64(z + 48);
  }
  if (cdOff > zipSize || cdSize > zipSize - cdOff || cdSize > (256u << 20) || count > 1000000) {
    why = "the ZIP's directory lies outside the file";
    return false;
  }
  Bytes cd(static_cast<size_t>(cdSize));
  if (cdSize && !read(cdOff, cd.data(), cd.size())) {
    why = "cannot read the ZIP's directory";
    return false;
  }
  size_t p = 0;
  for (uint64_t i = 0; i < count; i++) {
    if (p + 46 > cd.size() || le32(cd.data() + p) != 0x02014b50) {
      why = "the ZIP's directory is damaged";
      return false;
    }
    const uint8_t* h = cd.data() + p;
    ZipEntry e;
    e.flags = le16(h + 8);
    e.method = le16(h + 10);
    e.crc = le32(h + 16);
    e.packed = le32(h + 20);
    e.size = le32(h + 24);
    uint16_t nameLen = le16(h + 28), extraLen = le16(h + 30), commentLen = le16(h + 32);
    e.local = le32(h + 42);
    if (p + 46 + nameLen + extraLen + commentLen > cd.size()) {
      why = "the ZIP's directory is damaged";
      return false;
    }
    e.name.assign((const char*) h + 46, nameLen);
    // ZIP64 extra field: the values that overflowed, in this order
    const uint8_t* x = h + 46 + nameLen;
    for (size_t k = 0; k + 4 <= extraLen;) {
      uint16_t id = le16(x + k), len = le16(x + k + 2);
      if (k + 4 + len > extraLen) break;
      if (id == 1) {
        const uint8_t* v = x + k + 4;
        size_t left = len;
        if (e.size == 0xFFFFFFFFu && left >= 8) e.size = le64(v), v += 8, left -= 8;
        if (e.packed == 0xFFFFFFFFu && left >= 8) e.packed = le64(v), v += 8, left -= 8;
        if (e.local == 0xFFFFFFFFu && left >= 8) e.local = le64(v), v += 8, left -= 8;
      }
      k += 4 + len;
    }
    out.push_back(e);
    p += 46 + nameLen + extraLen + commentLen;
  }
  return true;
}

bool ZipExtract(const ZipRead& read, const ZipEntry& e, Bytes& out, std::string& why) {
  if (e.flags & 1) {
    why = e.name + " is encrypted";
    return false;
  }
  if (e.method != 0 && e.method != 8) {
    why = e.name + " is packed with method " + std::to_string(e.method) + " (only store and deflate)";
    return false;
  }
  uint8_t h[30];
  if (!read(e.local, h, 30) || le32(h) != 0x04034b50) {
    why = e.name + ": damaged entry";
    return false;
  }
  uint64_t data = e.local + 30 + le16(h + 26) + le16(h + 28);
  Bytes packed(static_cast<size_t>(e.packed));
  if (e.packed && !read(data, packed.data(), packed.size())) {
    why = e.name + ": cannot read";
    return false;
  }
  if (e.method == 0) {
    if (e.packed != e.size) {
      why = e.name + ": damaged entry";
      return false;
    }
    out.swap(packed);
  } else if (!Inflate(packed.data(), packed.size(), out, size_t(e.size))) {
    why = e.name + ": damaged packed data";
    return false;
  }
  if (Crc32(out.data(), out.size()) != e.crc) {
    why = e.name + ": damaged (checksum)";
    return false;
  }
  return true;
}

}  // namespace re4lang
