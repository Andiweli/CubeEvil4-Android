// The upscalers (1.1.0, "Upscaler" in the app menu's Display page; the idea from the review of the RE4Wii-Android port,
// the shaders from their original projects): when the game's picture is smaller than the box it is shown in on the
// screen, instead of the plain bilinear stretch
//   - SGSR: Qualcomm Snapdragon Game Super Resolution 1 (BSD-3-Clause, third_party/snapdragon-gsr), one pass, made for
//     phones: an edge-directed Lanczos-like filter on the green channel;
//   - FSR: AMD FidelityFX Super Resolution 1 (MIT, third_party/fidelityfx-fsr): EASU (edge-adaptive upsampling) and
//     RCAS (contrast-adaptive sharpening), two passes.
// Both read the picture's rows shown (the XFB's region, `rows`: texture v of its top and its span) and write the box's
// size; the box is then shown 1:1. The third-party files stay as they were published; the shader text is made here
// around them (GLSL ES 3.10 for GLES, GLSL 4.50 for Vulkan). Changes to the SGSR file's text, made here at run time:
// "#version 310 es" (textureGather), its operation mode as a constant (textureGather's component must be one), and for
// Vulkan its uniforms as a push constant block.
#pragma once

#include <cstdint>
#include <string>

namespace re4gx {

enum UpscalerKind { kUpscaleOff = 0, kUpscaleSgsr = 1, kUpscaleFsr = 2 };

// the programs: the full-screen quad (in_TEXCOORD0 = the source's texture coordinates), SGSR, FSR's EASU and RCAS
struct UpscaleSources {
  std::string vs, sgsr, easu, rcas;
};
// GLES (vulkan false): "rows" a uniform, the samplers "ps0" (SGSR) and "src" (EASU, RCAS), the constants "ViewportInfo",
// "con0".."con3", "con". Vulkan: one push constant block per program (UpscalePushBytes), the sampler at binding 0.
UpscaleSources UpscaleShaders(bool vulkan, int program);  // program: 1 SGSR, 2 EASU, 3 RCAS (vs fits it)

// Vulkan push constants (offsets in bytes): SGSR {vec4 ViewportInfo; vec2 rows} 32; EASU {uvec4 con0..con3; vec2 rows}
// 80; RCAS {uvec4 con; vec2 rows} 32
constexpr std::uint32_t kSgsrPushBytes = 32, kEasuPushBytes = 80, kRcasPushBytes = 32;

// EASU's constants (FsrEasuConOffset of ffx_fsr1.h): the region (inW x inH texels starting at row offY) of a texture of
// texW x texH upscaled to outW x outH
void EasuConstants(std::uint32_t con[16], float inW, float inH, float texW, float texH, float outW, float outH,
                   float offY);
// RCAS's constants (FsrRcasCon): sharpness in stops (0 the strongest; 0.2 as AMD's samples)
void RcasConstants(std::uint32_t con[4], float stops);

}  // namespace re4gx
