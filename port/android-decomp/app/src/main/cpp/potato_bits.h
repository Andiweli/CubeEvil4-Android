// The Potato mask the app hands the game core (CoreBridge.setPotato -> core_mode.cpp -> re4_core_set_potato, also
// through the 32-bit core process): bits 0..4 the Potato mode's switches (0 weather effects off, 1 special effects off,
// 2..3 the particle effects, 4 shadows and light effects off; the core's runtime/potato.cpp), bits 5 and 6 the
// real-time shadows (5 on, 6 also the objects; every build since 16.11.2026, until then debug builds only;
// RealtimeShadows.java, runtime/rt_shadow.cpp). Until 16.11.2026 the app kept only bits 0..4: the core never saw the real-time shadows on the device (the PC checks set
// them with RE4_POTATO). Header only: core_mode.cpp and the host check tools/test_gx_gl.cpp (TestPotatoBits).
#pragma once

namespace re4 {

constexpr int kPotatoSwitches = 0x1F;  // bits 0..4
constexpr int kPotatoShadows = 0x60;   // bits 5 and 6

// what the core gets of the app's mask: the switches and the real-time shadows, nothing else
constexpr int PotatoForCore(int mask) { return mask & (kPotatoSwitches | kPotatoShadows); }

// the Potato mode's switches alone (the GPU bench, the log's names): the real-time shadows are no Potato switch
constexpr int PotatoSwitches(int mask) { return mask & kPotatoSwitches; }

}  // namespace re4
