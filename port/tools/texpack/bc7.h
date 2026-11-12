// BC7 (BPTC, DXGI 97-99) block decoder for the texture tool: all eight modes (our own code, CC0, after the format's
// description in the Direct3D 11 and OpenGL BPTC specifications).
#pragma once

#include <cstdint>

namespace img {

// one 16-byte block to 16 pixels RGBA, row-major
void DecodeBc7Block(const std::uint8_t *block, std::uint8_t out[64]);

}  // namespace img
