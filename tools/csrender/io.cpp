#include "io.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace csio {

namespace {

void put16(std::vector<std::uint8_t>& b, std::uint32_t v) {
  b.push_back(static_cast<std::uint8_t>(v & 0xff));
  b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
}
void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
  put16(b, v & 0xffff);
  put16(b, v >> 16);
}
void put32be(std::vector<std::uint8_t>& b, std::uint32_t v) {
  for (int s = 24; s >= 0; s -= 8)
    b.push_back(static_cast<std::uint8_t>((v >> s) & 0xff));
}
void putTag(std::vector<std::uint8_t>& b, const char* tag) { b.insert(b.end(), tag, tag + 4); }

bool writeFile(const std::string& path, const std::vector<std::uint8_t>& bytes) {
  std::ofstream f(path, std::ios::binary);
  f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(f);
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t n, std::uint32_t crc = 0) {
  static std::uint32_t table[256];
  static bool ready = false;
  if (!ready) {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k)
        c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    ready = true;
  }
  crc = ~crc;
  for (std::size_t i = 0; i < n; ++i)
    crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
  return ~crc;
}

void pngChunk(std::vector<std::uint8_t>& out, const char* tag, const std::vector<std::uint8_t>& data) {
  put32be(out, static_cast<std::uint32_t>(data.size()));
  std::vector<std::uint8_t> body(tag, tag + 4);
  body.insert(body.end(), data.begin(), data.end());
  out.insert(out.end(), body.begin(), body.end());
  put32be(out, crc32(body.data(), body.size()));
}

} // namespace

bool writeWav(const std::string& path, const Audio& audio) {
  const auto frames = static_cast<std::uint32_t>(audio.x.size());
  const auto rate = static_cast<std::uint32_t>(std::lround(audio.sampleRate));
  std::vector<std::uint8_t> b;
  b.reserve(44 + frames * 4);
  putTag(b, "RIFF");
  put32(b, 36 + frames * 4);
  putTag(b, "WAVE");
  putTag(b, "fmt ");
  put32(b, 16);
  put16(b, 1); // PCM
  put16(b, 2); // stereo
  put32(b, rate);
  put32(b, rate * 4);
  put16(b, 4);
  put16(b, 16);
  putTag(b, "data");
  put32(b, frames * 4);
  auto sample = [](float v) {
    return static_cast<std::uint32_t>(static_cast<std::int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f))) & 0xffff;
  };
  for (std::uint32_t i = 0; i < frames; ++i) {
    put16(b, sample(audio.x[i]));
    put16(b, sample(audio.y[i]));
  }
  return writeFile(path, b);
}

bool readWav(const std::string& path, Audio& audio, std::string& error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    error = "cannot open " + path;
    return false;
  }
  std::vector<std::uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  auto u16 = [&](std::size_t o) { return static_cast<std::uint32_t>(d[o] | (d[o + 1] << 8)); };
  auto u32 = [&](std::size_t o) { return u16(o) | (u16(o + 2) << 16); };
  if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) {
    error = path + " is not a WAV file";
    return false;
  }
  std::uint32_t format = 0, channels = 0, bits = 0;
  std::size_t dataAt = 0, dataLen = 0;
  for (std::size_t o = 12; o + 8 <= d.size();) {
    const std::size_t len = u32(o + 4);
    if (std::memcmp(d.data() + o, "fmt ", 4) == 0 && o + 24 <= d.size()) {
      format = u16(o + 8);
      channels = u16(o + 10);
      audio.sampleRate = u32(o + 12);
      bits = u16(o + 22);
      if (format == 0xfffe && len >= 26 && o + 34 <= d.size())
        format = u16(o + 32); // WAVE_FORMAT_EXTENSIBLE: the real format is in the sub-format
    } else if (std::memcmp(d.data() + o, "data", 4) == 0) {
      dataAt = o + 8;
      dataLen = std::min(len, d.size() - dataAt);
      break;
    }
    o += 8 + len + (len & 1);
  }
  const bool pcm = format == 1 && (bits == 16 || bits == 24 || bits == 32);
  const bool flt = format == 3 && bits == 32;
  if (!dataAt || channels < 1 || !(pcm || flt)) {
    error = path + ": unsupported WAV format";
    return false;
  }
  const std::size_t bytes = bits / 8, frame = bytes * channels, frames = dataLen / frame;
  auto sample = [&](std::size_t o) -> float {
    if (flt) {
      float v;
      std::memcpy(&v, d.data() + o, 4);
      return v;
    }
    std::int32_t v = 0;
    for (std::size_t k = 0; k < bytes; ++k)
      v |= static_cast<std::int32_t>(d[o + k]) << (8 * k);
    const int shift = 32 - static_cast<int>(bits);
    v = static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << shift) >> shift; // sign-extend
    return static_cast<float>(v) / static_cast<float>(1u << (bits - 1));
  };
  audio.x.resize(frames);
  audio.y.resize(frames);
  for (std::size_t i = 0; i < frames; ++i) {
    const std::size_t o = dataAt + i * frame;
    audio.x[i] = sample(o);
    audio.y[i] = channels > 1 ? sample(o + bytes) : audio.x[i];
  }
  return true;
}

bool writePng(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgb) {
  // Raw scanlines, filter type 0, in stored (uncompressed) deflate blocks.
  std::vector<std::uint8_t> raw;
  raw.reserve(static_cast<std::size_t>(height) * (static_cast<std::size_t>(width) * 3 + 1));
  for (int row = 0; row < height; ++row) {
    raw.push_back(0);
    const auto* begin = rgb.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(width) * 3;
    raw.insert(raw.end(), begin, begin + static_cast<std::size_t>(width) * 3);
  }
  std::vector<std::uint8_t> z{0x78, 0x01};
  std::uint32_t a = 1, b = 0;
  for (std::uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  for (std::size_t o = 0; o < raw.size();) {
    const std::size_t n = std::min<std::size_t>(65535, raw.size() - o);
    z.push_back(o + n == raw.size() ? 1 : 0);
    put16(z, static_cast<std::uint32_t>(n));
    put16(z, static_cast<std::uint32_t>(~n & 0xffff));
    z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(o), raw.begin() + static_cast<std::ptrdiff_t>(o + n));
    o += n;
  }
  put32be(z, (b << 16) | a);

  std::vector<std::uint8_t> out{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  std::vector<std::uint8_t> header;
  put32be(header, static_cast<std::uint32_t>(width));
  put32be(header, static_cast<std::uint32_t>(height));
  header.insert(header.end(), {8, 2, 0, 0, 0}); // 8-bit RGB
  pngChunk(out, "IHDR", header);
  pngChunk(out, "IDAT", z);
  pngChunk(out, "IEND", {});
  return writeFile(path, out);
}

} // namespace csio
