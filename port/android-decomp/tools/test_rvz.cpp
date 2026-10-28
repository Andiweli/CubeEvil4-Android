// RVZ reader (disc_rvz.cpp) with RVZ files built here: stored and zstd groups (zstd frames of raw
// blocks), packed groups with data and junk runs, an all-zero group, a short last group, the disc
// header from the RVZ header, and the refusals (Wii disc, bzip2, damaged tables).
#include "disc_rvz.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;
void Check(bool ok, const char *what) {
  std::printf("%s: %s\n", ok ? "ok" : "FAIL", what);
  if (!ok)
    failures++;
}

using Bytes = std::vector<std::uint8_t>;

void Put32(Bytes &b, std::uint32_t v) {
  for (int i = 3; i >= 0; i--)
    b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
void Put64(Bytes &b, std::uint64_t v) {
  Put32(b, static_cast<std::uint32_t>(v >> 32));
  Put32(b, static_cast<std::uint32_t>(v));
}
void Set32(Bytes &b, std::size_t at, std::uint32_t v) {
  for (int i = 0; i < 4; i++)
    b[at + i] = static_cast<std::uint8_t>(v >> (8 * (3 - i)));
}

// Dolphin's LaggedFibonacciGenerator as written there (words byteswapped, bytes copied out of
// them on this little-endian host): a second way to the junk, against the reader's.
struct DolphinLfg {
  std::uint32_t buf[521];
  std::size_t pos = 0;
  void Forward() {
    for (std::size_t i = 0; i < 32; i++)
      buf[i] ^= buf[i + 521 - 32];
    for (std::size_t i = 32; i < 521; i++)
      buf[i] ^= buf[i - 32];
  }
  void SetSeed(const std::uint8_t *seed) {
    pos = 0;
    for (std::size_t i = 0; i < 17; i++) {
      std::uint32_t w;
      std::memcpy(&w, seed + i * 4, 4);
      buf[i] = __builtin_bswap32(w);
    }
    for (std::size_t i = 17; i < 521; i++)
      buf[i] = (buf[i - 17] << 23) ^ (buf[i - 16] >> 9) ^ buf[i - 1];
    for (std::uint32_t &x : buf)
      x = __builtin_bswap32((x & 0xFF00FFFF) | ((x >> 2) & 0x00FF0000));
    for (int i = 0; i < 4; i++)
      Forward();
  }
  void Skip(std::size_t n) {
    pos += n;
    while (pos >= 521 * 4) {
      Forward();
      pos -= 521 * 4;
    }
  }
  void Get(std::size_t n, std::uint8_t *out) {
    while (n) {
      const std::size_t len = std::min(n, 521 * 4 - pos);
      std::memcpy(out, reinterpret_cast<std::uint8_t *>(buf) + pos, len);
      pos += len;
      n -= len;
      out += len;
      if (pos == 521 * 4) {
        Forward();
        pos = 0;
      }
    }
  }
};

// A zstd frame of raw blocks (no compressor needed): content size in the header, blocks of 128 KiB.
Bytes ZstdRaw(const Bytes &data) {
  Bytes f = {0x28, 0xB5, 0x2F, 0xFD, 0xA0};  // magic, single segment, 4-byte content size
  for (int i = 0; i < 4; i++)
    f.push_back(static_cast<std::uint8_t>(data.size() >> (8 * i)));
  std::size_t at = 0;
  do {
    const std::size_t n = std::min<std::size_t>(data.size() - at, 128 * 1024);
    const bool last = at + n == data.size();
    const std::uint32_t h = (last ? 1u : 0u) | 0u << 1 | static_cast<std::uint32_t>(n) << 3;
    f.push_back(static_cast<std::uint8_t>(h));
    f.push_back(static_cast<std::uint8_t>(h >> 8));
    f.push_back(static_cast<std::uint8_t>(h >> 16));
    f.insert(f.end(), data.begin() + at, data.begin() + at + n);
    at += n;
  } while (at < data.size());
  return f;
}

struct Built {
  Bytes file;
  Bytes disc;  // what it must unpack to
  std::size_t tableAt = 0;
};

// A disc of 4 chunks and a bit (chunk 0x20000): group 0 stored, group 1 zstd and packed with a junk
// run in the middle, group 2 all zero, group 3 packed and stored (junk at its end), group 4 short.
Built Build(bool zstd, std::uint32_t discType = 1, std::uint32_t compression = 0) {
  const std::uint32_t chunk = 0x20000;
  const std::uint64_t discSize = 4ull * chunk + 0x1234;
  Built b;
  b.disc.resize(discSize);
  for (std::size_t i = 0; i < b.disc.size(); i++)
    b.disc[i] = static_cast<std::uint8_t>(i * 2654435761u >> 24);
  for (int i = 0; i < 0x80; i++)
    b.disc[i] = static_cast<std::uint8_t>(0xA0 + i);  // the disc header (also in the RVZ header)
  std::memset(b.disc.data() + 2 * chunk, 0, chunk);
  // junk runs: group 1 at 0x9000..0x15000 of it, group 3 its last 0x3456 bytes
  std::uint8_t seed1[68], seed3[68];
  for (int i = 0; i < 68; i++)
    seed1[i] = static_cast<std::uint8_t>(17 * i + 3), seed3[i] = static_cast<std::uint8_t>(91 * i + 7);
  const std::uint64_t j1 = chunk + 0x9000, j1n = 0xC000, j3n = 0x3456, j3 = 4ull * chunk - j3n;
  DolphinLfg g;
  g.SetSeed(seed1);
  g.Skip(j1 % 0x8000);
  g.Get(j1n, b.disc.data() + j1);
  g.SetSeed(seed3);
  g.Skip(j3 % 0x8000);
  g.Get(j3n, b.disc.data() + j3);

  auto slice = [&](std::uint64_t from, std::uint64_t n) {
    return Bytes(b.disc.begin() + static_cast<std::ptrdiff_t>(from), b.disc.begin() + static_cast<std::ptrdiff_t>(from + n));
  };
  auto run = [&](Bytes &p, std::uint64_t from, std::uint64_t n) {
    Put32(p, static_cast<std::uint32_t>(n));
    Bytes s = slice(from, n);
    p.insert(p.end(), s.begin(), s.end());
  };
  auto junk = [&](Bytes &p, const std::uint8_t *seed, std::uint64_t n) {
    Put32(p, static_cast<std::uint32_t>(n) | 0x80000000u);
    p.insert(p.end(), seed, seed + 68);
  };
  struct Group {
    Bytes data;
    bool compressed;
    std::uint32_t packed;
  };
  std::vector<Group> groups(5);
  groups[0] = {slice(0, chunk), false, 0};
  {
    Bytes p;
    run(p, chunk, 0x9000);
    junk(p, seed1, j1n);
    run(p, j1 + j1n, 2ull * chunk - (j1 + j1n));
    groups[1] = {zstd ? ZstdRaw(p) : p, zstd, static_cast<std::uint32_t>(p.size())};
  }
  groups[2] = {Bytes(), false, 0};
  {
    Bytes p;
    run(p, 3ull * chunk, chunk - j3n);
    junk(p, seed3, j3n);
    groups[3] = {p, false, static_cast<std::uint32_t>(p.size())};
  }
  {
    Bytes s = slice(4ull * chunk, 0x1234);
    groups[4] = {zstd ? ZstdRaw(s) : s, zstd, 0};
  }

  Bytes &f = b.file;
  f.assign(0x48 + 0xDC, 0);
  std::memcpy(f.data(), "RVZ\x01", 4);
  Set32(f, 0x04, 0x01000000);
  Set32(f, 0x08, 0x00030000);
  Set32(f, 0x0C, 0xDC);
  const std::size_t h2 = 0x48;
  Set32(f, h2 + 0x00, discType);
  Set32(f, h2 + 0x04, compression ? compression : (zstd ? 5 : 0));
  Set32(f, h2 + 0x0C, chunk);
  std::memcpy(f.data() + h2 + 0x10, b.disc.data(), 0x80);
  // groups at 4-byte aligned places
  std::vector<std::uint32_t> at(groups.size());
  for (std::size_t i = 0; i < groups.size(); i++) {
    while (f.size() % 4)
      f.push_back(0);
    at[i] = static_cast<std::uint32_t>(f.size());
    f.insert(f.end(), groups[i].data.begin(), groups[i].data.end());
  }
  Bytes raw;
  Put64(raw, 0x80);
  Put64(raw, discSize - 0x80);
  Put32(raw, 0);
  Put32(raw, static_cast<std::uint32_t>(groups.size()));
  Bytes table;
  for (std::size_t i = 0; i < groups.size(); i++) {
    Put32(table, at[i] >> 2);
    Put32(table, static_cast<std::uint32_t>(groups[i].data.size()) | (groups[i].compressed ? 0x80000000u : 0));
    Put32(table, groups[i].packed);
  }
  if (zstd) {
    raw = ZstdRaw(raw);
    table = ZstdRaw(table);
  }
  const std::uint64_t rawAt = f.size();
  f.insert(f.end(), raw.begin(), raw.end());
  const std::uint64_t tableAt = f.size();
  b.tableAt = tableAt;
  f.insert(f.end(), table.begin(), table.end());
  Set32(f, h2 + 0xB4, 1);
  for (int i = 0; i < 8; i++)
    f[h2 + 0xB8 + i] = static_cast<std::uint8_t>(rawAt >> (8 * (7 - i)));
  Set32(f, h2 + 0xC0, static_cast<std::uint32_t>(raw.size()));
  Set32(f, h2 + 0xC4, static_cast<std::uint32_t>(groups.size()));
  for (int i = 0; i < 8; i++)
    f[h2 + 0xC8 + i] = static_cast<std::uint8_t>(tableAt >> (8 * (7 - i)));
  Set32(f, h2 + 0xD0, static_cast<std::uint32_t>(table.size()));
  for (int i = 0; i < 8; i++) {
    f[0x24 + i] = static_cast<std::uint8_t>(discSize >> (8 * (7 - i)));
    f[0x2C + i] = static_cast<std::uint8_t>(f.size() >> (8 * (7 - i)));
  }
  return b;
}

std::string Temp(const char *name) {
  const char *dir = std::getenv("TMPDIR");
  return std::string(dir ? dir : "/tmp") + "/" + name;
}

void Write(const std::string &path, const Bytes &b) {
  FILE *f = std::fopen(path.c_str(), "wb");
  std::fwrite(b.data(), 1, b.size(), f);
  std::fclose(f);
}

Bytes Read(const std::string &path) {
  Bytes b;
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return b;
  std::uint8_t buf[65536];
  std::size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
    b.insert(b.end(), buf, buf + n);
  std::fclose(f);
  return b;
}

void Round(bool zstd) {
  const std::string in = Temp(zstd ? "rvz_check_zstd.rvz" : "rvz_check_none.rvz"), out = in + ".iso";
  Built b = Build(zstd);
  Write(in, b.file);
  re4::RvzInfo info;
  std::string error;
  const char *tag = zstd ? "zstd" : "stored";
  Check(re4::IsRvz(in.c_str()) && re4::RvzInfoOf(in.c_str(), &info, &error) && info.discSize == b.disc.size() &&
            info.fileSize == b.file.size(),
        (std::string(tag) + ": header and sizes").c_str());
  std::uint64_t last = 0, total = 0;
  const bool ok = re4::RvzToIso(in.c_str(), out.c_str(),
                                [&](std::uint64_t d, std::uint64_t t) {
                                  last = d, total = t;
                                  return true;
                                },
                                &error);
  if (!ok)
    std::printf("  %s\n", error.c_str());
  const Bytes got = Read(out);
  std::size_t diff = 0;
  while (diff < got.size() && diff < b.disc.size() && got[diff] == b.disc[diff])
    diff++;
  Check(ok && got == b.disc, (std::string(tag) + ": unpacked like the disc (data, junk, zeros, header)").c_str());
  if (got != b.disc)
    std::printf("  first difference at 0x%zx of 0x%zx\n", diff, b.disc.size());
  Check(last == total && total == b.disc.size() - 0x80, (std::string(tag) + ": progress to the end").c_str());
  // cancel
  const bool canceled = !re4::RvzToIso(in.c_str(), out.c_str(), [](std::uint64_t, std::uint64_t) { return false; }, &error);
  Check(canceled && error.find("canceled") != std::string::npos, (std::string(tag) + ": cancel").c_str());
  std::remove(in.c_str());
  std::remove(out.c_str());
}

void Refusals() {
  std::string error;
  re4::RvzInfo info;
  const std::string path = Temp("rvz_check_bad.rvz");
  Write(path, Build(false, 2).file);
  Check(!re4::RvzInfoOf(path.c_str(), &info, &error) && error.find("Wii") != std::string::npos, "refused: a Wii disc");
  Write(path, Build(false, 1, 2).file);
  Check(!re4::RvzInfoOf(path.c_str(), &info, &error) && error.find("bzip2") != std::string::npos,
        "refused: bzip2 compression, with the hint to Zstandard or ISO/GCM");
  Built b = Build(true);
  b.file[b.tableAt] ^= 0xFF;  // the group table's zstd magic
  Write(path, b.file);
  Check(!re4::RvzToIso(path.c_str(), (path + ".iso").c_str(), nullptr, &error) && error.find("damaged") != std::string::npos,
        "refused: a damaged table");
  Write(path, Bytes{'G', '4', 'B', 'E', '0', '8'});
  Check(!re4::IsRvz(path.c_str()) && !re4::RvzInfoOf(path.c_str(), &info, &error), "refused: an ISO is no RVZ");
  std::remove(path.c_str());
  std::remove((path + ".iso").c_str());
}

} // namespace

int main(int argc, char **argv) {
  // rvz_check IN.rvz OUT.iso: unpacks a real RVZ (the local check with a disc of one's own)
  if (argc == 3) {
    std::string error;
    const bool ok = re4::RvzToIso(argv[1], argv[2], nullptr, &error);
    std::printf("%s\n", ok ? "unpacked" : error.c_str());
    return ok ? 0 : 1;
  }
  Round(false);
  Round(true);
  Refusals();
  std::printf("rvz_check: %d failed\n", failures);
  return failures ? 1 : 0;
}
