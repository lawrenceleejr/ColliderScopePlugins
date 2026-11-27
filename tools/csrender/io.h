// Minimal WAV and PNG I/O for csrender. No dependencies.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace csio {

struct Audio {
  double sampleRate = 48000.0;
  std::vector<float> x, y; // left = X, right = Y
};

// 16-bit PCM stereo, clipped to +-1.
bool writeWav(const std::string& path, const Audio& audio);
// Reads PCM 16/24/32-bit or 32-bit float; mono is copied to both channels.
bool readWav(const std::string& path, Audio& audio, std::string& error);

// 8-bit RGB, rows top to bottom.
bool writePng(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgb);

} // namespace csio
