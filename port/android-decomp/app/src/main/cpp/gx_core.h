// The inside of GxRenderer (gx_gl.h): one class per graphics API behind the same calls (port/docs/vulkan-bauplan.md,
// E1). GxRenderer forwards every call to its GxCore: GlRenderer (gx_gl.cpp, GLES 3) or, later, the Vulkan renderer.
// Step by step the GX emulation itself (registers, shader keys, in-between pictures, merged draws, the effects'
// logic) moves into this class, once for both APIs, and the API classes keep only what talks to the GPU.
#pragma once

#include "gx_gl.h"
#include "gx_gl_shader.h"
#include "re4_gx_frame.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define GXLOG(...) re4gx::LogTo("RE4GX", __VA_ARGS__)

namespace re4gx {

constexpr int kEfbWidth = 640;
constexpr int kEfbHeight = 528;
constexpr unsigned kTlutBytes = 0x80000;

inline float AsFloat(std::uint32_t v) {
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

inline std::uint64_t Fnv(const void *data, std::size_t n, std::uint64_t h = 1469598103934665603ull) {
  const auto *p = static_cast<const std::uint8_t *>(data);
  for (std::size_t i = 0; i < n; i++) {
    h ^= p[i];
    h *= 1099511628211ull;
  }
  return h;
}

inline int Signed11(std::uint32_t v) {
  return (v & 0x400) ? static_cast<int>(v & 0x7FF) - 2048 : static_cast<int>(v & 0x7FF);
}

struct KeyHash {
  std::size_t operator()(const ShaderKey &k) const { return static_cast<std::size_t>(Fnv(&k, sizeof(k))); }
};
struct KeyEqual {
  bool operator()(const ShaderKey &a, const ShaderKey &b) const { return std::memcmp(&a, &b, sizeof(a)) == 0; }
};

// A file written under this name first and renamed when complete: one per thread (the render
// thread and the one of WarmProgramCache may store the same program at once).
std::string TempName(const std::string &path);
// A key list file: "RE4L", the size of a key, the count, the keys (WriteProgramKeyList, the app's
// asset gx-programs.keys; port/tools/gx_program_list.py). A list of another key size is ignored.
struct KeyListHead {
  char magic[4];
  std::uint32_t keyBytes, count;
};
bool ReadKeyList(const std::string &path, std::vector<ShaderKey> *keys);
bool WriteKeyList(const std::string &path, const std::vector<ShaderKey> &keys);

// A texture as the renderer keeps it: decoded RGBA8 with its levels (id: the API's handle; GL: the texture name).
// An HD replacement of the player's texture pack (tex_pack.h) instead: ETC2 texels, `lodShift` levels finer than the
// game's texture (log2 of the sizes), `hdBytes` of the pack's budget.
struct Texture {
  unsigned id = 0;
  unsigned width = 0, height = 0, levels = 1;
  float lodShift = 0.0f;
  std::uint32_t hdBytes = 0;
};

class TexPack;

// An EFB copy to a texture: the texels as the matching texture format reads
// them (8-bit copies as I8: value in all four channels; two-channel copies as
// IA8; colour copies as their colour), and for paletted reads the raw value.
struct Copy {
  unsigned id = 0;
  unsigned width = 0, height = 0;
  unsigned serial = 0;
  unsigned kind = 0; // 0 colour, 1 one channel (value in .r), 2 two channels (.a high, .r low)
  bool read = false;  // read as a texture since it was made (GxStats::copyRead, a measurement)
};

// The draw state the GX registers ask for (GxCore::ApplyState), in terms both APIs have: the scissor, depth test,
// colour and alpha writes, blending, culling and the width of lines.
struct DrawState {
  int scissor[4] = {};  // x, y, width, height in EFB pixels at the scale (row 0 = the EFB's row 0)
  bool depthTest = false, depthWrite = false;
  unsigned depthFunc = 7;  // GX's order: never, less, equal, lequal, greater, notequal, gequal, always
  bool colorWrite = true, alphaWrite = true;
  bool blend = false;
  enum Eq : std::uint8_t { kAdd, kReverseSubtract };
  enum Factor : std::uint8_t {
    kZero, kOne, kSrcColor, kOneMinusSrcColor, kDstColor, kOneMinusDstColor, kSrcAlpha, kOneMinusSrcAlpha, kDstAlpha,
    kOneMinusDstAlpha
  };
  Eq eqColor = kAdd, eqAlpha = kAdd;
  Factor srcColor = kOne, dstColor = kZero, srcAlpha = kOne, dstAlpha = kZero;
  unsigned cull = 0;  // triangles only: 0 none, 1 back, 2 front, 3 all
  float lineWidth = 1.0f;  // lines and points, in pixels at the scale (the APIs take at least 1)
};

// An EFB copy as GxCore::EfbCopy sets it up for the API's copy pass (CopyPass): the target texture, the EFB region
// (GameCube pixels), the target's size, the copy shader's mode (CopyMode; 0 for a display copy), the copy filter's
// gain, the TV filter's taps (at < 0: off) one line (`step` pixels) apart, the rows the copy repeats at its edges
// (`limited`: limit[0], limit[1] in pixels at the scale, else -1e9 / 1e9)
struct CopyParams {
  unsigned target = 0;
  bool toXfb = false, depth = false, half = false;
  int mode = 0;
  int x = 0, y = 0, w = 0, h = 0, tw = 0, th = 0;
  float gain = 1.0f;
  float taps[3] = {0.0f, -1.0f, 0.0f};
  float step = 0.0f;
  float box = 0.0f;  // a half copy's box: EFB texels from its centre (0: the 2x2 texels; kCopyFs)
  bool limited = false;
  float limit[2] = {-1e9f, 1e9f};
};

// A texture sampler as the GX texture mode registers ask for it (GxCore::SamplerFor), in terms both APIs have.
struct GxSamplerDesc {
  std::uint8_t wrapS = 0, wrapT = 0;  // 0 clamp to edge, 1 repeat, 2 mirrored repeat
  bool magLinear = false, minLinear = false;
  std::uint8_t mip = 0;  // 0 no mipmaps (the base level), 1 nearest level, 2 linear between levels
  float minLod = 0.0f, maxLod = 0.0f;
  float anisotropy = 1.0f;  // 1: off
};

// The clear after an EFB copy (GxCore::ClearAfterCopy): the region at the scale, what is cleared, the values.
struct GxClearRect {
  int rect[4] = {};  // x, y, width, height
  bool color = false, alpha = false, depth = false;
  float rgba[4] = {};
  float z = 1.0f;
};

struct GxCore {
  explicit GxCore(GxApi a) : api(a) {}
  virtual ~GxCore() = default;
  GxCore(const GxCore &) = delete;
  GxCore &operator=(const GxCore &) = delete;

  const GxApi api;

  // ---- the part moved from GlRenderer (gx_gl.cpp): the GX emulation both APIs share ----
  using DepthSink = GxRenderer::DepthSink;
  static constexpr int kDepthWidth = GxRenderer::kDepthWidth, kDepthHeight = GxRenderer::kDepthHeight;
  // what identifies the program of the shader key in use (DrawSig: draws of the same program match from one
  // picture to the next); GL: the address of its Program
  virtual std::uintptr_t ProgramTag() const = 0;
  // the EFB scale in halves of the GameCube's resolution (2: 1x, 3: 1.5x ... 8: 4x; the app menu's
  // in-between levels, wish of the project owner, 27.10.2026). Lo / Hi: EFB pixels (the
  // GameCube's) to pixels here, rounded down / up. A region runs from Lo(x) to Lo(x + w), so
  // scissors, viewports and clears of neighbouring regions meet without a gap; a copy has Hi(w)
  // pixels (its texture), and the copy shader reads its source rectangle exactly (Sf).
  int halves = 2;
  int Lo(int v) const { return v * halves / 2; }
  int Hi(int v) const { return (v * halves + 1) / 2; }
  float Sf(int v) const { return static_cast<float>(v) * static_cast<float>(halves) * 0.5f; }
  GxStats stats;
  // register state
  std::uint32_t bp[256] = {};
  std::uint32_t xfMem[0x800] = {};
  std::uint32_t xfReg[0x58] = {};
  std::vector<std::uint8_t> tlut = std::vector<std::uint8_t>(kTlutBytes);
  std::int32_t tevColor[4][4] = {};
  std::int32_t tevKonst[4][4] = {};
  // XF block rows (vec4) that changed since the last upload, per part (kXfParts)
  int dirtyLo[kXfParts] = {}, dirtyHi[kXfParts] = {};
  GxOptions options;
  int samples = 1;
  float anisotropy = 1.0f;
  unsigned xfbWidth = 0, xfbHeight = 0;
  bool haveXfb = false;
  // FXAA (GxRenderer::SetFxaa): the smoothed copy of the XFB, shown instead of it while fxaaShown
  bool fxaaOn = false, fxaaShown = false;
  bool tvFilter = false;  // SetTvFilter
  bool tvLogged = false;
  // Sharpen (GxRenderer::SetSharpen): 0 off, 1 light, 2 strong; the display copy (after FXAA) sharpened into
  // sharpTex, shown instead of it while sharpShown. A movie's pictures stay as they are (RE4_GX_CHUNK_MOVIE)
  int sharpLevel = 0;
  bool sharpShown = false, chunkMovie = false;
  // PSone demake (GxRenderer::SetPsx): the 3D programs' key bit and point-sampled textures, and the last pass
  // over the display copy (a 320-wide grid, 15-bit colour with the PlayStation's 4 x 4 dither) into psxTex,
  // shown instead of it while psxShown
  bool psxOn = false, psxShown = false;
  // Colour (GxRenderer::SetColour, 0.99.9): 0 off, 1 warm film, 2 survival horror, mixed in by colourStrength
  // (0.5 light, 1 strong); the display copy as shown so far (after the PSone pass) graded into a picture of its
  // own, shown while colourShown
  int colourMode = 0;
  float colourStrength = 0.5f;
  bool colourShown = false, colourFailed = false;
  // CRT (GxRenderer::SetCrt, 0.99.9): 0 off, 1 light, 2 strong; after each display copy its game lines (crtLines,
  // with the colour bleed) and their glow in pictures of their own (crtReady), from which Present draws the screen
  int crtLevel = 0, crtLines = 0;
  bool crtReady = false, crtFailed = false;
  std::uint32_t curOpAt = 0;  // the op Run executes (FlareNote)
  // "Save bandwidth (Vulkan)" (GxRenderer::SetBandwidthSave, 0.99.8): the EFB passes of a tiling GPU load and store
  // less (gx_vk.cpp: a clear after a copy folded into the next pass, which then loads nothing there)
  bool bwSave = false;
  // the depth map for the game's GXPeekZ (BP 0x5B marker), see GxRenderer::SetDepthSink
  DepthSink depthSink;
  bool peekWanted = false, peekFailed = false;
  // the number of the picture that peeked (the marker's value) and of the picture in each read-back slot
  std::uint32_t peekPicture = 0, peekSlotPicture[2] = {0, 0};
  bool skipDraws = false;  // Skip: this chunk's draws, EFB copies and markers are left out
  bool holdXfb = false;  // this chunk's display copy is not shown (RE4_GX_CHUNK_HOLD)
  // "Native" (0.99.0): this chunk's 3D was drawn for the screen's wider aspect (RE4_GX_CHUNK_WIDE), and the
  // display copy shown now came from such a chunk (XfbWide)
  bool chunkWide = false, xfbWide = false;
  bool viBlack = false;  // the VI shows black (RE4_GX_CHUNK_BLACK, VISetBlack): Present draws black
  // the chunk whose vertices and indices the in-between pass uploaded: its Run right after draws
  // from them (vbo is no longer changed by the pass) instead of uploading them again
  const Re4GxChunk *uploadedBy = nullptr;
  std::uint32_t uploadedSequence = 0;
  // lowest EFB row (+1) a draw under a full-width scissor reached since the last display copy: the
  // rows below it were never drawn (the game's letterbox)
  int sceneBottom = 0;
  // the EFB rows the app shows (GxRenderer::SetShownRows): all, or 56..391 in the 16:9 formats
  std::atomic<int> shownTop{0}, shownBottom{kEfbHeight};
  // GxRenderer::SetWiden: how much wider the flat draws of the whole screen are drawn (1: as sent)
  std::atomic<float> widen2d{1.0f};
  std::unordered_map<std::uint64_t, Texture> textures;
  // the player's HD texture pack (GxRenderer::SetTexturePack, port/docs/textur-lader-plan.md): Upload finds a game
  // texture's replacement by Dolphin's key and loads its ETC2 levels from texPackLevel on (1: "Sharpness: Half"),
  // while the replacements take at most texPackBudget bytes (hdBytes now); never in the PSone demake.
  // texturesDropped: the pack changed, every texture went, the game core must send them again (TakeTexturesDropped)
  std::shared_ptr<const TexPack> texPack;
  unsigned texPackLevel = 0;
  std::size_t texPackBudget = 0, hdBytes = 0;
  bool texPackActive = false, texturesDropped = false;
  std::vector<std::uint8_t> hdData;  // a replacement's texels on their way to the GPU
  void SetTexturePack(std::shared_ptr<const TexPack> pack, unsigned firstLevel, std::size_t budgetBytes);
  // the replacement of the texture Upload is at (into tex); false: none, keep the game's
  bool UploadHd(const Re4GxTexture &t, const std::uint8_t *data, std::size_t bytes, Texture &tex);
  std::unordered_map<std::uint32_t, Copy> copies;  // by destination address
  std::unordered_map<std::uint64_t, Texture> paletted; // copies read through a palette
  bool dofStrong = false;
  bool autofocusFailed = false;
  // the last perspective projection (XF 0x1024/0x1025, type 0 at 0x1026):
  // the scene's, for the distances behind the depths
  float perspA = 0.0f, perspB = 0.0f;
  bool havePerspective = false;
  // --- what GL already has (per-draw work is skipped while it holds) --------
  bool keyDirty = true;              // a register that shapes the shader changed
  std::uint32_t keyLayout = ~0u;     // vertex layout of the cached key
  ShaderKey key = {};
  bool pxDirty = true;               // a register of the PX block changed
  bool xfRegsDirty = true;           // colours, viewport or projection changed
  std::string programCache;          // folder of linked programs ("" = none)
  // Prewarm: the cache's key files (read at its first call), the next one, its time so far
  struct AheadKey {
    ShaderKey key;
    bool listed = false; // from the app's list (SetProgramList): for the MSAA setting in use
  };
  std::string programList;  // the app's key list (SetProgramList)
  bool aheadRead = false;
  std::vector<AheadKey> ahead;
  std::size_t aheadNext = 0;
  double aheadMs = 0.0;
  unsigned aheadLoaded = 0, aheadLinked = 0, aheadListed = 0;
  // Programs per room (01.11.2026, report of a Mi 11 crashing in r307): Prewarm took every program
  // the cache had (10242 on that phone, each held by the driver with mappings of its own) until the
  // process ran out of mappings (Android allows 65530) and the system's shader cache aborted. Now the
  // keys a room drew with go into <cache>/room-XXX.keys, Prewarm takes the room's own list, and the
  // programs beyond kMaxPrograms that went longest unused are deleted (a later draw loads its binary).
  static constexpr std::size_t kMaxPrograms = 3000, kMaxMerged = 1000;
  std::size_t maxPrograms = kMaxPrograms, maxMerged = kMaxMerged;  // RE4_GX_MAX_PROGRAMS (tests)
  unsigned room = ~0u;
  std::unordered_set<ShaderKey, KeyHash, KeyEqual> roomUsed;
  bool roomDirty = false;
  unsigned roomSaved = 0;  // stats.chunks at the last write of the room's list
  unsigned evicted = 0;
  // --- merged draw calls (GxRenderer::SetMerge, RE4_GX_MERGE=1) ----------------------
  // Before a chunk's draws (MergePlan) its chains: draws in a row of the same layout, stride and
  // primitive, each one's vertices after the one before's, with only XF, BP, owner, palette and
  // texture ops without data between them. Their indices go after the chunk's into ibo, each moved
  // by its draw's first vertex minus the chain's (u16, a chain ends where they no longer fit), so one
  // draw call with the chain's base vertex draws any run of them. While drawing (DrawMerged) a run
  // takes the next draws of its chain as long as the ops between them change nothing but what a
  // slot of MergeBlock holds (MergeQuiet).
  bool mergeOn = false;
  struct MergeMark {
    std::uint32_t op;     // the draw op's place in chunk.ops
    std::uint32_t chain;  // the op of the chain's first draw
    std::uint32_t index;  // the draw's moved indices in ibo (elements)
    std::uint32_t first;  // its first vertex (gl_VertexID)
    std::uint32_t base;   // the chain's first vertex (the base vertex of its draw calls)
  };
  std::vector<MergeMark> mergeMarks;  // this chunk's, by op
  std::size_t mergeNext = 0;          // the next mark not yet passed
  std::vector<std::uint16_t> mergeIndices;
  MergeBlock mergeBlock{};
  // the programs merged runs wanted and did not have: Prewarm makes them on a refresh without a chunk
  // (a link took about 16 ms on the Retroid); till then the runs draw one by one
  std::vector<ShaderKey> mergeWanted;
  // the merged draw call Draw makes instead of the single one
  struct MergeCall {
    unsigned draws = 0;
    std::uint32_t index = 0, count = 0, base = 0;
  };
  // GxStats::splitNs: the time since the last mark to `kind` (Run marks at its start)
  std::chrono::steady_clock::time_point splitAt;
  void Split(int kind);
  // the texture bound to each map
  struct Bound {
    GLuint id = 0;
    unsigned width = 1, height = 1, levels = 1;
    float lodShift = 0.0f;  // an HD replacement's (Texture::lodShift): its sampler's levels move by it
  } bound[8];
  XfBlock xf = {};
  // the XF parts as last sent (UploadXf): a part the game wrote again with the same values is not
  // sent again (about 5 % of the uploads of a village picture, Retroid series r226, 28.10.2026)
  XfBlock xfSent = {};
  bool xfSentValid[kXfParts] = {};
  PixelBlock px = {};
  PixelBlock pxSent = {};
  bool pxValid = false;
  std::uint32_t copySerial = 0;
  // --- in-between pictures (60 FPS, experimental; GxRenderer::Interpolate) ----------------
  // A real Run notes each draw: what identifies it (DrawSig), the view-space translation of its
  // position matrix, the position and normal rows it used and the projection. The in-between pass
  // runs the next chunk once more; a draw that matches a noted one is drawn with its rows (and,
  // when they changed, its vertices: the figures the game skins on the CPU) blended between the
  // two pictures. Everything the pass changes is put back afterwards (Interpolate).
  std::uint64_t boundKey[8] = {};  // what each map holds: texture key, or copy address | 1 << 63
  struct DrawRec {
    std::uint64_t sig = 0;
    float t[3] = {};          // view-space translation of the draw's (first) position matrix
    std::uint32_t posRow = 0; // first position row noted (pi)
    std::uint32_t rows = 0;   // 3, 30 with matrix indices per vertex, 0 with the identity
    std::uint32_t data = 0;   // into recFloats: rows x 4 position, rows x 3 normal, 6 projection floats
    std::uint32_t vertexOffset = 0, vertexCount = 0, stride = 0, layout = 0;
    bool perspective = false;
    std::uint64_t look = 0;  // a particle's signature without its textures (0: not a particle)
    // the object's draw (RE4_GX_OP_OWNER, patch 0137: object, function, table and the n-th draw of
    // the object; 0: none), its shape (the signature without the textures), a particle's id << 16 |
    // life frames (~0: none), and for an owned draw where its texture matrices (8 x 12 floats), the
    // TEV colours (32 words) and the light colours (8 x 4 floats) lie in recFloats (~0: not kept)
    std::uint64_t owner = 0, shape = 0;
    std::uint32_t life = ~0u, extra = ~0u;
    std::uint64_t base = 0;  // the object (ownerBase) of an owned draw
  };
  bool recordDraws = false;
  bool recValid = false;            // rec describes the chunk run last
  std::uint32_t recSequence = 0;
  std::vector<DrawRec> rec, recNew;
  std::vector<float> recFloats, recFloatsNew;
  bool interpPass = false;
  float interpT = 0.5f;
  const Re4GxChunk *interpPrev = nullptr;  // the chunk rec describes (its vertices)
  std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> recBySig, recByLook;
  // owner tags (60fps-plan.md S1/S2): the object of the draws that follow (0: none), its tag's life
  // word, how many of its draws came; the draw's own key (object and n-th draw) while it is drawn
  std::uint64_t ownerBase = 0, curOwner = 0;
  std::uint32_t ownerLife = ~0u, ownerDraws = 0, curLife = ~0u;
  std::uint32_t ownerWords[4] = {0, 0, 0xFF, ~0u};  // the owner tag as sent (the lens flares' log and test)
  std::unordered_map<std::uint64_t, std::uint32_t> recByOwner;
  // draws per object (ownerBase): of the chunk noted (recOwnerDraws, filled while recording) and of the
  // next one (nextOwnerDraws, counted ahead by Interpolate). An object that draws more or fewer quads
  // than before (a flame's trail of sprites growing) is not matched by "n-th draw": its sprites would
  // take each other's places (Retroid 28.10.2026, big torches: now and then a wrong in-between picture)
  std::unordered_map<std::uint64_t, std::uint32_t> recOwnerDraws, recOwnerDrawsNew, nextOwnerDraws;
  // what a blended draw changed beyond its position rows (BlendRestore puts it back): texture
  // matrix rows, 0x100 for the TEV colours (tevSave: before the blend), 0x200 + row for a post-transform
  // matrix, 0x300 for the indirect matrices (indSave: the BP words before the blend)
  std::vector<std::uint32_t> texRowsBlended;
  std::int32_t tevSave[2][4][4] = {};
  std::uint32_t indSave[9] = {};
  std::vector<std::uint8_t> recUsed;
  // noted draws of an object that draws as often in the next picture: they belong to its draws there
  // (by the owner tag), the guessing (MatchDraw) leaves them alone. Taken by a draw it guessed, the
  // object's own draw found its draw gone and guessed in turn, and so on down the line (r307 puzzle,
  // 30.10.2026: a turn starts new sparkles; the pieces' triangles were drawn between two fields,
  // squashed, in the in-between picture: ghosts)
  std::vector<std::uint8_t> recReserved;
  std::unordered_map<std::uint32_t, Copy> interpCopies;  // the pass's own EFB copies
  std::unordered_set<std::uint32_t> interpCopied;         // addresses it copied to
  std::unordered_set<std::uint64_t> interpUploads;        // textures it uploaded (Run(next) skips them)
  bool skipInterpUploads = false;
  std::vector<std::uint8_t> blendVertices;
  std::vector<std::uint32_t> oneFrameCands;  // BlendDraw: the one-frame draws of an id in the picture before
  // the pass's counts: perspective draws, matched, turned more than 20 degrees
  unsigned ipDraws = 0, ipMatched = 0, ipTurned = 0;
  // The camera's move from the picture before to the next one in the pass (view space: next =
  // cam * before), read off the large draws of a look of their own: each proposes its move, and
  // another object (another matrix) that moved the same way votes for it; the move with the most
  // votes, at least two, counts (the room, of many pieces that stand still). MatchDraw takes it out
  // of a particle's distances, so that it is matched by its own move, not the camera's.
  struct CamGuess {
    float c[3][4];
    float t[3];  // the proposing draw's translation (its parts with the same matrix do not vote)
    unsigned votes;
  };
  CamGuess camGuess[4] = {};
  unsigned camGuesses = 0, camVotes = 0;
  float cam[3][4] = {};
  bool camValid = false;
  // What identifies a draw from one picture to the next: its shader (the program of the same
  // registers), primitive, vertex layout and count, matrix indices and the textures it samples
  // (without them: a particle's look, see BlendDraw).
  std::uint64_t DrawSig(const Re4GxDraw &d, const ShaderKey &k, bool textures = true) const;
  // The position rows a draw transforms with: 3 from its matrix index, all 30 when its vertices
  // carry indices, none with the identity (60) or a texture row.
  static void DrawRows(const Re4GxDraw &d, std::uint32_t *first, std::uint32_t *count);
  // a real Run: the draw as it is drawn now
  void RecordDraw(const Re4GxDraw &d, const ShaderKey &k);
  // The XF row of texture coordinate n's matrix from the draw's defaults (60: the identity; with
  // indices in the vertices: 60, not looked at).
  static std::uint32_t TexRow(const Re4GxDraw &d, int n);
  // the owner tag (RE4_GX_OP_OWNER): object, function, table (0xFF: none), life word
  static std::uint64_t OwnerKey(const std::uint32_t *p);
  void SetOwner(const std::uint32_t *p);
  // The noted draw of the picture before that this one is: the same signature; the first one not
  // yet taken when it lies near enough (the game draws in the same order from one picture to the
  // next), else of the first eight the nearest that does; none when no one moved less than a
  // quarter of its distance plus 400 units (another object of the same look).
  // A particle (a small draw with a matrix of its own, at most 16 vertices) instead: the first one
  // not taken unless another of the first 32 lies clearly nearer (by more than 2 % of its distance
  // plus 50 units). The particles of smoke and fire look alike, lie a few thousand units apart,
  // and one that dies or starts shifts the order (Retroid report 26.10.2026, r224: neighbours
  // swapped places in the in-between pictures, the smoke flickered). With the camera's move known
  // (cam), their positions before are moved by it first, and a particle may move only 5 % of its
  // distance plus 300 units: one that has just started is drawn as in the next picture instead of
  // taking another's place. Only particles: the camera's move is the room's, and the parts of Leon
  // turn with the camera; matched by it, two alike parts of him swapped places and their blended
  // vertices drew a long bar at the bottom of the picture (Retroid report 26.10.2026, fast turns).
  // (flat: a 2D draw, in screen units; by the game's order)
  const DrawRec *MatchDraw(const std::vector<std::uint32_t> &candidates, const float *t, std::uint32_t rows,
                           std::uint32_t vertexCount, bool flat = false);
  // q moved by the camera's move c (a point in view space before -> after).
  static void CameraApply(const float (&c)[3][4], const float *q, float *out);
  // A particle found by its object moved from `before` to `t` (view space) farther than it can in one game picture:
  // it jumped (wrapped around the camera, started its path again), BlendDraw draws it as a new one.
  bool Jumped(const float *before, const float *t) const;
  // A 3D draw without a counterpart in the picture before (a new flame of a fire, a slot used anew, a
  // part of the ground that drew otherwise) while the camera's move is known (NoteCamera): its rows as if it
  // stood still in the world, between where that move puts it in the picture before and where it is now.
  // Drawn as in the next picture, it stood apart from the matched draws around it, which are between the
  // two: a fire's glow on the ground went missing in every in-between picture while the camera tilted,
  // the fire flickered at 60 FPS (the project owner's Retroid series 03.11.2026).
  std::uint32_t BlendByCamera(std::uint32_t first, std::uint32_t rows);
  // c = n * p^-1 for two 3 x 4 modelviews of the same object (p: row-major, 12 floats); false when
  // p has no inverse.
  static bool CameraMove(const float *p, const float (&n)[3][4], float (&c)[3][4]);
  // A large draw of a look of its own (one of it in the picture before), matched: its move is the
  // camera's when it stands still. It votes for a move proposed before that takes its position
  // before to where it is now (when it has a matrix of its own: two parts of one object that moved
  // agreed with each other, Retroid series 26.10.2026 at the merchant), else proposes its own.
  void NoteCamera(std::uint64_t sig, const float *before, const float (&now)[3][4]);
  // The in-between pass, before a draw: its rows (and vertices) between the picture before (a)
  // and now; how many rows to put back afterwards (BlendRestore).
  // 2D (the HUD, full-screen passes, a glow laid over a flame in screen space) as in the next picture,
  // unless it has a matrix of its own that only moved, by at most a quarter of the picture
  // (kFlatMove, screen units): then between the two too, without lights and projection. Retroid
  // series 26.10.2026: the glow of the merchant's brazier, a 2D square 418 units wide, moved 23
  // units a game picture while the camera turned and jumped in the in-between pictures. Not when it
  // turned or changed its size: r224 lays a copy of the picture over itself through a square that
  // turns 10 degrees a game picture (its texture coordinates come from the square's corners), and
  // half turned it showed the room twice.
  static constexpr float kFlatMove = 128.0f;
  // The lights of the next picture into the in-between one: the modelview `mid` of a draw (blended)
  // against `now` (the next picture's) gives the camera's move between the two, C = mid * now^-1;
  // positions take all of it, directions its turn. False when `now` has no inverse.
  bool BlendLights(const float (&now)[3][4], const float *mid);
  // After the draw: the rows from XF memory again (the projection from the registers).
  void BlendRestore(std::uint32_t blended);
  // The copy filter (BP 0x53/0x54, GXSetCopyFilter): seven vertical taps in
  // 1/64 that the GameCube applies to EFB copies. Their sum scales the colour:
  // the game's brightness option (main_sub.cpp Bg_brightness_set) raises or
  // lowers it around 64. The vertical blur of the taps (the deflicker filter
  // for the TV picture) is left out: the picture stays sharp.
  float CopyFilterGain() const;
  // The copy filter's taps as the line above (taps 0, 1), the line (2, 3, 4) and the line below (5, 6), in 1/64.
  void CopyFilterTaps(float *above, float *at, float *below) const;
  void Dirty(int lo, int hi);
  // The XF block rows an XF address touches.
  void DirtyAddress(std::uint32_t a);
  void XfWrite(std::uint32_t address, const std::uint32_t *words, std::uint32_t n);
  // One word of XF memory into the XF block (FillXf for all of it).
  void XfMemToBlock(std::uint32_t a);
  // The XF registers of the block: material and ambient colours, viewport, projection.
  void FillXfRegs();
  ShaderKey MakeKey(const Re4GxDraw &d);
  // The room's list (room-XXX.keys: the keys it drew with before, SetRoom), at most half of
  // kMaxPrograms (Prewarm).
  std::string RoomList(unsigned r) const;
  void ReadAhead();
  // The keys the room drew with so far into its list.
  void SaveRoom();
  // A key a draw of this room uses (its program exists).
  void NoteKey(const ShaderKey &k);
  void FillPixel(const ShaderKey &k);
  // The chains of `chunk` (mergeMarks) and their moved indices (mergeIndices); see MergeMark.
  void MergePlan(const Re4GxChunk &chunk);
  // The ops of the chunk in [from, to) change nothing a merged draw call depends on but what its
  // slots hold (the XFM part, the material colours, the matrix indices): BP registers that end with
  // the values in place (the game sets a blend mode and back between two objects; no copy,
  // autofocus or depth marker; the TEV colours never changed), XF writes to the matrices, the
  // material colours (0x100A-0x100D) or of the values in place (and the matrix index registers
  // 0x1018/0x1019, which only CP's copy in the draws uses), textures already bound, palette loads
  // (a paletted texture's key has its palette), owner tags.
  bool MergeQuiet(const Re4GxChunk &chunk, std::uint32_t from, std::uint32_t to, int *why) const;
  // "4:3 (exact)": a flat draw over the whole screen (its viewport starts at the left edge and is
  // at least 500 of the game's 512 pixels wide: HUD, menus, text, fades, the letterbox) gets the
  // factor; not one that reads an EFB copy (heat haze, water, the pause filter, blurs: they lay the
  // picture under them back over it and must stay on it), nor one into a part of the EFB (a
  // picture for a texture)
  float WidenFor(const ShaderKey &k) const;
  // Copy format and conversion of BP 0x52 (and the EFB pixel format): mode for
  // the copy shader (0 RGBA8, 1 RGB565, 2 RGB5A3, 3 intensity I, 4 intensity IA,
  // 5 R, 6 G, 7 B, 8 A, 9 RG, 10 GB, 11 RA, 12 depth high byte, 13 depth middle,
  // 14 depth low, 15 depth 16 bit, 16 depth 24 bit) and the kind of the result.
  static void CopyMode(std::uint32_t control, bool depth, int *mode, unsigned *kind);
  // --- lens flares (GxRenderer::SetFlares) ---------------------------------------------------
  //
  // A light source is what the game draws as light: a 3D draw of the picture's main view (viewport at least
  // 500 of 512 wide) blended additively (src * x + dst * 1: flames, candles, the glow of lamps and torches,
  // fire on the ground, muzzle flashes) with a depth test. Its vertices go through the draw's own position
  // matrix and projection on the CPU, as the vertex shader does: the screen box and depth of the draw. Draws
  // close together (a torch is many particles) make one source. After the scene's last 3D draw (FlareScan:
  // the HUD, fades and menus come after it and lie over the flares) FlarePass:
  //  1. visibility, for each tracked source a texel of flareVis[cur]: the share of nine depth taps around
  //     its centre at which the EFB depth is not nearer than the source (a wall or a figure in front: 0),
  //     times how bright the picture is there (only a bright flame flares, not smoke or dust); followed
  //     smoothly from the picture before (flareVis[prev], a quarter of the way a picture), so a flare fades
  //     in and out and never pops;
  //  2. the flares, additively into the EFB's colour, as a camera lens makes them (rework after the project
  //     owner's test, 04.11.2026: "a random flicker near lights" before): at the light a soft glow with a faint
  //     six-ray starburst, and for the largest few lights (up to three, their share followed smoothly) a row of
  //     faint coloured ghosts, rings and hexagons in green, violet and blue, on the line from the light through
  //     the picture's centre: they move against the light as the camera turns.
  // Fourth report of the project owner (05.11.2026, GX series 00 in r104's house: "Funkeln in Leons Hals" with the lens
  // flares on): a faint glow sprite on the dark wall beside Leon took its brightness from his lit neck at the edge of
  // its box. A light is bright at the middle of its own box (a flame, a lamp's glare): where that middle is dark
  // (below 0.3 .. 0.5), it does not flare.
  // Steady, no flicker: a source counts only after it was seen in 4 pictures in a row (a muzzle flash or a spark
  // never flares), its place follows the flame's box smoothly, and its visibility (depth taps times brightness)
  // rises over about ten pictures and falls over about seven.
  // Only real lights (second test of the project owner, 04.11.2026: "stars" beside flames while moving): draws
  // join only when they nearly touch and stay a flame's size (a group of candles is several sources, no star in the
  // empty middle of their box); not a flare: dust and embers (a box under 4 pixels, or thinner than 3 of one draw),
  // a light shaft (long and thin), a cloud of fire or smoke (over 64 pixels of several draws, 96 of one: a lamp's
  // halo is one round sprite); and at the light the picture must be
  // clearly brighter than around it (a mote in a shaft of light or in front of Ashley's white trousers is not).
  // The star sits on the brightest part of its box (the visibility pass finds it), and lights close together share
  // one glow's brightness.
  // Third test of the project owner (04.11.2026, 27 GX series: "Funkeln an Leons Körper", pick-up items, monitors,
  // "große Lichtstrahler extrem grell", the star "wandert beim Drehen mit"): a light must be near white (0.7..0.92;
  // a monitor, a figure's shine or an item's sparkle is about half as bright), a box whose middle lies near a larger
  // one's (a third of its size) is the same light (a lamp's glare is about 20 sprites on top of each other: one star,
  // not 20), the star follows the light in every picture, has 60 % of its former brightness, at most a torch's size,
  // and a big lamp (box over 60 pixels) down to 40 % of that; the ghosts stay as they were.
  // The PSone demake, FXAA and the TV filter come later at the display copy and take the flares with the
  // picture; with MSAA they go into the multisampled EFB; the in-between pictures of 60 FPS run the same
  // (their matrices are the blended ones).
  static constexpr int kFlareSlots = 32;
  bool flaresOn = false, flareFailed = false;
  // the glow and star at each light ("Halos + lens flares", as before 0.99.6); off: only the ghosts ("Lens flares",
  // wish of the project owner, 09.11.2026): the flare shader's size.z = 1 leaves the slot's first quad out
  bool flareHalos = true;
  std::uint32_t flareAfter = 0;  // the op offset after the scene's last 3D draw (0: none in this chunk)
  bool flareDone = false;
  struct FlareCand {
    float x0, y0, x1, y1, z;  // screen box (GameCube EFB pixels) and depth (0..1 as the EFB holds it)
    int draws;
    int normals = 0;          // draws with normals among them (lit meshes: a figure's or an object's shine)
    std::uint32_t verts = 0;  // the most vertices of one draw
    unsigned tev = 0;         // (the log) the TEV stages of the last draw
    std::uint32_t zmode = 0, blend = 0, tevc = 0;  // (the log) BP 0x40, 0x41, the TEV colour of the last draw
    std::uint32_t layout = 0;
    std::uint32_t owner[4] = {0, 0, 0xFF, ~0u};  // (the log) the owner tag of the last draw
  };
  std::vector<FlareCand> flareCands;
  struct FlareSlot {
    float x = 0, y = 0, z = 0, r = 0;
    int lost = -1;     // -1 free, 0 seen in this picture, > 0 pictures since it was seen
    bool fresh = false;
    int age = 0;          // pictures in a row it was seen (4 and more: it flares)
    float ghost = 0.0f;   // its share of the ghosts (0..1, followed smoothly)
    float crowd = 1.0f;   // its glow's share among lights close together (1 / sqrt(their number), followed)
    float size = 0.0f;    // its box's larger side (GameCube pixels, followed)
  };
  FlareSlot flareSlot[kFlareSlots];
  bool flareLog = false, flareLog2 = false;  // RE4_GX_FLARE_LOG=1: sources and slots, 2: seen and brightness
  unsigned flarePictures = 0;
  // The ops of `chunk`: the offset after its last draw in perspective into the main view (the XF projection
  // type, register 0x26, as it changes on the way), 0 without one.
  std::uint32_t FlareScan(const Re4GxChunk &chunk) const;
  // In Draw (after FillXfRegs): an additive 3D draw of the main view before the scene's end is a source.
  void FlareNote(const Re4GxDraw &d, const Re4GxChunk &chunk, std::uint32_t opAt);
  // The picture's sources onto the tracked slots: near ones merged, each to the slot it was in (nearest within
  // its size), new ones into free slots; a slot not seen fades out where it was and is free after 16 pictures.
  void FlareTrack();
  // One op of the chunk at `at` (Run); the next op's place (the end when the op is cut off).
  std::uint32_t Op(const Re4GxChunk &chunk, std::uint32_t at);
  // the GPU side of the ops (Op): the API renderer's
  void Draw(const Re4GxDraw &d, const Re4GxChunk &chunk, const MergeCall *merge = nullptr);
  // Run's GPU side, in this order: before the chunk; its vertices and indices (not when skipped or uploaded by the
  // in-between pass); the state before the first op; after the last op; leaving the API for the app's other code
  virtual void RunBegin() = 0;
  virtual void UploadChunk(const Re4GxChunk &chunk) = 0;
  virtual void RunState() = 0;
  virtual void RunEnd() = 0;
  virtual void RunLeave() = 0;
  // a run of a merged chain as one draw call (MergePlan); the lens flares' pass; every texture forgotten (a reset)
  virtual void FlarePass() = 0;
  // the counters after the draw before (GxStats::mergeSame / mergeUniform), valid within a chunk
  struct MergeSnap {
    unsigned uniforms, programs, binds, states, setups, copies, displays;
    unsigned mode;  // the primitive (GL: GL_TRIANGLES ...)
  } mergeLast{};
  bool mergeValid = false, mergeMatrix = false, mergePixel = false;
  // the draw state of the GX registers for the next draw (when they changed since the last one)
  void ApplyState(unsigned primitive);
  // Draw's GPU side: the program of `key` for the draws from now on (false: none, the draws are left out); the program
  // of this draw (the merged one's for a merged draw call; null: none); the EFB ready to be drawn into; the draw
  // state; a uniform block to its binding (kXfPartBinding ..., `copyBytes`: only the block's first bytes are
  // new); the draw itself with its program, constants (uMtx, uWiden), textures, vertices and indices
  virtual bool KeyProgram() = 0;
  virtual const void *DrawProgram(bool merged) = 0;
  virtual void UseEfb() = 0;
  // the same for a draw that stays inside x0,y0..x1,y1 (the renderer's EFB pixels, DrawArea): a renderer that can
  // load and store only that part of the EFB around it does (Vulkan's render area), the others take the whole EFB
  virtual void UseEfbArea(int x0, int y0, int x1, int y1) {
    (void) x0, (void) y0, (void) x1, (void) y1;
    UseEfb();
  }
  // where a small 2D draw (orthographic, at most 64 vertices) lands on the EFB, in the renderer's pixels with a margin,
  // within the scissor; false: unknown or not small (3D, many vertices, widened, an in-between picture's blend)
  bool DrawArea(const Re4GxDraw &d, const Re4GxChunk &chunk, int area[4]) const;
  virtual void ApplyDrawState(const DrawState &s) = 0;
  virtual void UploadUniform(int binding, const void *data, std::size_t offset, std::size_t bytes,
                             std::size_t blockBytes, std::size_t copyBytes = 0) = 0;
  virtual void SubmitDraw(const Re4GxDraw &d, const void *program, const MergeCall *merge) = 0;
  std::uint32_t stateSig[10] = {};   // ApplyState's registers as last applied
  bool stateValid = false;
  bool streamMoved = false;          // StreamNextPart since the last draw's uploads (UploadXf)
  // The parts of the XF block that changed since the last draw, to the GPU. In the stream a part
  // goes whole into a new place; after the stream moved to its next quarter every part goes again
  // (a part bound in an old quarter could be written over before the GPU reads it).
  // `merged`: not the XFM part (a merged draw call has its own, UploadMerged); it stays dirty for
  // the next single draw.
  void UploadXf(bool merged = false);
  // The merged block's first `draws` slots (and the slot table) to its binding.
  void UploadMerged(unsigned draws);
  // the blended vertices of the draw BlendDraw is on to a place of their own (false: no room left in this pass)
  virtual bool PutBlendedVertices(const std::uint8_t *data, std::size_t bytes, std::uint32_t stride) = 0;
  std::uint32_t BlendDraw(const Re4GxDraw &d, const ShaderKey &k, const Re4GxChunk &chunk);
  // the GPU side of BpWrite, BindTexture, Forget: an EFB copy (BP 0x52); the gameplay autofocus (BP 0x5A); the clear
  // after a copy with the plain state; waiting for the GPU (RE4_GX_COPY_TIME); a copy read through a palette (its
  // texture); a texture's texels to the GPU (into textures); the API's own caches of bound objects forgotten; a
  // texture deleted (GL at once; an API that draws later when the GPU is done with it)
  virtual void Autofocus(std::uint32_t player) = 0;
  virtual void ClearAfterCopyPlain(std::uint32_t control, int x, int y, int w, int h) = 0;
  virtual void GpuFinish() = 0;
  unsigned PalettedCopy(const Copy &copy, const Re4GxTexture &t);
  // the GPU side: the copy through the palette (rgba, pw x ph colours) into a new texture of the copy's size
  virtual unsigned DrawPaletted(const Copy &copy, const Re4GxTexture &t, const std::uint8_t *rgba, unsigned pw,
                                unsigned ph) = 0;
  void Upload(const Re4GxTexture &t, const Re4GxChunk &chunk);
  virtual void Touched() = 0;
  virtual void DeleteTexture(unsigned id) = 0;
  unsigned whiteTex = 0;  // the white stand-in texture (the API's handle)
  void BpWrite(std::uint32_t word, bool action);
  void BindTexture(const Re4GxTexture &t, const Re4GxChunk &chunk);
  void Forget(std::uint64_t key);
  void DropTextures();
  // an EFB copy (BP 0x52) and its GPU side: the display copy's texture; a copy's texture (Copy::id, made or kept);
  // the copy pass; the effects after a display copy (FXAA, Sharpen, PSone); the depth map for GXPeekZ; the clear
  // after a copy; the EFB with the plain state
  void EfbCopy(std::uint32_t control);
  virtual unsigned DisplayTarget(int w, int h) = 0;
  virtual void CopyTarget(Copy &c, int w, int h) = 0;
  virtual void CopyPass(const CopyParams &p) = 0;
  virtual void DisplayEffects(int pixelWidth, int pixelHeight, int y, int h, int w) = 0;
  virtual void SnapshotDepth() = 0;
  void ClearAfterCopy(std::uint32_t control, int x, int y, int w, int h);
  // the clear after a copy kept for the EFB's next pass (true: the API took it, ClearAfterCopy does no more); GL never
  virtual bool DeferClear(const GxClearRect &) { return false; }
  virtual void PlainEfb() = 0;
  // the in-between pass's GPU side: before it (the store of its blended vertices), after it; whether the key's
  // program exists, whether its merged draw calls' program does (made by Prewarm when not: a run draws one by one)
  virtual void InterpBegin(const Re4GxChunk &next) = 0;
  virtual void InterpEnd() = 0;
  virtual bool KeyProgramReady() const = 0;
  virtual bool MergedProgramReady() = 0;
  int focusCur = 0;
  unsigned focusChunk = ~0u;  // the chunk of the last autofocus (stats.chunks)
  // A draw op that starts a chain's run (mergeMarks[mergeNext]): the run's draws as one draw call;
  // the next op to run.
  std::uint32_t DrawMerged(const Re4GxChunk &chunk, std::uint32_t at);
  // a texture's texels to the GPU (Upload): a new texture of `levels` levels (its handle), each level's RGBA8
  // texels, the end (the levels that came); the clear after a copy; the sampler of a texture map's mode registers
  virtual unsigned BeginTexture(unsigned width, unsigned height, unsigned levels) = 0;
  virtual void TextureLevel(unsigned id, unsigned level, unsigned width, unsigned height, const std::uint8_t *rgba) = 0;
  virtual void EndTexture(unsigned id, unsigned levels) = 0;
  // an HD replacement's texture (UploadHd): `levels` levels in a compressed format (c4tex::Encoding), each level's
  // blocks, then EndTexture; 0 when the API or the device cannot (the game's texture stays)
  virtual unsigned BeginCompressed(unsigned /*encoding*/, unsigned /*width*/, unsigned /*height*/, unsigned /*levels*/) {
    return 0;
  }
  virtual void CompressedLevel(unsigned /*id*/, unsigned /*level*/, unsigned /*width*/, unsigned /*height*/,
                               const std::uint8_t * /*data*/, std::size_t /*bytes*/) {}
  virtual void ClearRect(const GxClearRect &r) = 0;
  // lodShift: an HD replacement's (Bound::lodShift)
  GxSamplerDesc SamplerFor(std::uint32_t mode0, std::uint32_t mode1, unsigned levels, bool point,
                           float lodShift = 0.0f) const;
  std::vector<std::uint8_t> decoded;  // Upload's texels of a level
  // ---- end of the part moved from GlRenderer ----

  // GxRenderer's calls, with the same meaning (see gx_gl.h)
  virtual bool InitHalves(int halves, std::string *error) = 0;
  virtual void RescaleHalves(int halves) = 0;
  // `to`: a renderer of the same API (copies stay on their API's GPU objects); else nothing is handed over
  virtual void HandOverCopies(GxCore &to) = 0;
  void SetOptions(const GxOptions &options);
  int Samples() const;
  float Anisotropy() const;
  bool Invalidates() const;
  virtual bool TiledMsaa() const = 0;
  bool HalfDof() const;
  void Run(const Re4GxChunk &chunk);
  void SetDepthSink(GxRenderer::DepthSink sink);
  void SetFxaa(bool on);
  void SetSharpen(int level);
  void SetTvFilter(bool on);
  void SetBandwidthSave(bool on);
  void SetPsx(bool on);
  void SetCrt(int level);
  void SetColour(int mode, int level);
  void SetFlares(bool on);
  void SetFlareHalos(bool on);
  void SetDofStrong(bool on);
  void SetShownRows(int top, int bottom);
  void SetWiden(float factor);
  bool CanSkip(const Re4GxChunk &a, const Re4GxChunk &b) const;
  void Skip(const Re4GxChunk &a);
  void SetRecording(bool on);
  void SetMerge(bool on);
  bool Merging() const;
  bool Interpolate(const Re4GxChunk &prev, const Re4GxChunk &next, float t);
  virtual unsigned Xfb(unsigned *width, unsigned *height) const = 0;
  bool XfbWide() const;
  virtual bool Present(int x, int y, int w, int h, unsigned top, unsigned rows) = 0;
  virtual unsigned EfbColor() const = 0;
  virtual bool ReadXfb(std::vector<std::uint8_t> *rgba, unsigned *width, unsigned *height) = 0;
  virtual bool ReadPresented(int screenW, int screenH, int x, int y, int w, int h, unsigned top, unsigned rows,
                             std::vector<std::uint8_t> *rgba) = 0;
  int Scale() const;
  int Halves() const;
  const GxStats &Stats() const;
  virtual bool BaseVertex() const = 0;
  virtual bool SetProgramCache(const std::string &folder) = 0;
  virtual bool Prewarm(double budgetMs) = 0;
  void SetProgramList(const std::string &path);
  void SetRoom(unsigned room);
  virtual void Release() = 0;
  // the start-up switches of the tests (RE4_GX_TV_FILTER, RE4_GX_PSX, RE4_GX_FLARES, RE4_GX_MAX_PROGRAMS), applied
  // by GxRenderer's constructor
  virtual void ApplyTestEnvironment() = 0;
  // the screen to present to (Vulkan: an ANativeWindow; GL draws into the current context's) and its size in pixels
  virtual void SetWindow(void *, int, int) {}
  virtual void SetOverlay(const GxOverlay &) {}
  // the API, the GPU and its driver, for the log and the error report
  virtual std::string ApiInfo() const = 0;
  virtual std::string TimingLine() { return std::string(); }
};

// A renderer's inside for `api` (GlRenderer in gx_gl.cpp ...); null when this build has none for it.
std::unique_ptr<GxCore> MakeGlCore();
std::unique_ptr<GxCore> MakeVkCore();  // gx_vk.cpp (builds with RE4_GX_VULKAN)
std::unique_ptr<GxCore> MakeCore(GxApi api);

} // namespace re4gx
