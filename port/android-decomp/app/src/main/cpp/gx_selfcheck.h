// The GPU checks itself once per run: does it compute what the generated TEV shaders say?
#pragma once

#include <string>

namespace re4gx {

// The colours of the game's movies (game/sofdec.cpp setTevPrm: Y, U and V through four TEV stages
// with a negative colour register and a doubling scale) for eight test colours, after 1, 2, 3 and 4
// stages, on a renderer of its own at scale 1 (the GL state is left at the defaults). Returns the
// log line; `wrong` gets the number of wrong pixels (0: the GPU computes them like the GameCube).
// A Galaxy A36 (Adreno 710) showed the movies green and brown (tester, 22.10.2026): with wrong
// pixels it tries the other ways of writing the TEV's lerp (gx_gl_shader.h SetTevMath) and keeps
// the first one without (else the one with the fewest) for every shader generated after it;
// `wrong` then counts that one's.
std::string MovieColourCheck(int *wrong);

// The check once with the way SetTevMath set: the wrong pixels (-1: no renderer, `error` says
// why), each with its value and the right one appended to `detail` (may be null).
int MovieColourPixels(std::string *detail, std::string *error);

} // namespace re4gx
