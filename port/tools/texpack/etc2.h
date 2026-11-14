// ETC2 / EAC: an encoder for the PC tool and a decoder for its check and the tests (our own code, CC0).
// Formats as in OpenGL ES 3.0 (appendix C): RGB8_ETC2, RGB8_PUNCHTHROUGH_ALPHA1_ETC2, RGBA8_ETC2_EAC.
// The encoder writes the individual, differential and planar modes (never T and H); the decoder reads all.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace etc2 {

enum Kind { kRgb = 1, kRgbA1 = 2, kRgbaEac = 3 };  // = c4tex::Encoding

inline std::size_t BlockBytes(Kind k) { return k == kRgbaEac ? 16 : 8; }

// rgba: w*h*4 bytes; the level's blocks are appended to out (row by row, the GL layout).
// effort 0 is quick, 1 the default, 2 searches further.
void EncodeLevel(Kind kind, const std::uint8_t *rgba, unsigned w, unsigned h, int effort, std::vector<std::uint8_t> &out);

// rgba: w*h*4 bytes written
void DecodeLevel(Kind kind, const std::uint8_t *data, unsigned w, unsigned h, std::uint8_t *rgba);

// one block, 16 pixels RGBA row-major (for the tests)
void DecodeBlock(Kind kind, const std::uint8_t *block, std::uint8_t out[64]);

// alpha threshold of the punch-through format: alpha >= 128 is opaque
constexpr int kAlphaThreshold = 128;

}  // namespace etc2
