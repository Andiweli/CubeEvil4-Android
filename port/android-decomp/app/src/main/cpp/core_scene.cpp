// Core mode renderer: the game core's state drawn with the app's GLES3 parts
// (see core_scene.h).
#include "core_scene.h"

#include "actor_gl.h"
#include "archive.h"
#include "environment.h"
#include "event_playback.h"
#include "language_pack_files.h"
#include "generic_event_actor.h"
#include "room_blocks.h"
#include "room_lighting.h"
#include "room_objects.h"
#include "room_shadows.h"
#include "scene.h"
#include "scene_gl.h"
#include "smd.h"
#include "smx.h"
#include "startup_data.h"

#include <GLES3/gl3.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>

#ifdef __ANDROID__
#include <android/log.h>
#define CORE_LOG(...) __android_log_print(ANDROID_LOG_INFO, "RE4Core", __VA_ARGS__)
#else
#define CORE_LOG(...) (std::fprintf(stderr, "[RE4Core] " __VA_ARGS__), std::fputc('\n', stderr))
#endif

namespace re4 {

void CopyCoreFrame(const Re4CoreSnapshot &s, CoreFrame &out) {
  out.valid = s.apiVersion == RE4_CORE_API_VERSION;
  out.serial = s.serial;
  out.frame = s.frame;
  for (unsigned k = 0; k < 4; ++k) out.step[k] = s.rno[k];
  out.room = s.room;
  out.disc = s.disc;
  out.cameraValid = s.cameraValid != 0;
  for (unsigned k = 0; k < 3; ++k) {
    out.eye[k] = s.cameraEye[k];
    out.target[k] = s.cameraTarget[k];
  }
  out.roll = s.cameraRoll;
  out.fovy = s.cameraFovy;
  out.screen = s.screen;
  out.droppedModels = s.droppedModels;
  const auto models = std::min<unsigned>(s.nModels, RE4_CORE_MAX_MODELS);
  const auto infos = std::min<unsigned>(s.nInfos, RE4_CORE_MAX_INFOS);
  const auto parts = std::min<unsigned>(s.nParts, RE4_CORE_MAX_PARTS);
  out.models.assign(s.models, s.models + models);
  out.infos.assign(s.infos, s.infos + infos);
  out.parts.assign(s.parts, s.parts + parts);
  const auto morph = std::min<unsigned>(s.nMorph, RE4_CORE_MAX_MORPH);
  out.morph.resize(morph);
  if (morph) std::memcpy(out.morph.data(), s.morph, morph * sizeof(s.morph[0]));
}

namespace {

bool exists(const std::string &path) {
  struct stat st {};
  return stat(path.c_str(), &st) == 0;
}

bool sameName(const std::string &a, const char *b) {
  std::size_t i = 0;
  for (; i < a.size() && b[i]; ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  return i == a.size() && b[i] == 0;
}

std::string sourceKey(const Re4CoreSource &s) {
  char text[112];
  std::snprintf(text, sizeof(text), "%.52s@%x#%d+%x", s.file,
                static_cast<unsigned>(s.fileOffset), static_cast<int>(s.entry),
                static_cast<unsigned>(s.innerOffset));
  return text;
}

// entry -2: data of a file without entries (an EVD), innerOffset bytes into it
bool known(const Re4CoreSource &s) { return s.file[0] && (s.entry >= 0 || s.entry == -2); }

const char *kTitleVertex = R"(#version 300 es
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
uniform vec2 scale;
out vec2 texcoord;
void main() {
  texcoord = uv;
  gl_Position = vec4(position * scale, 0.0, 1.0);
})";
const char *kTitleFragment = R"(#version 300 es
precision mediump float;
in vec2 texcoord;
uniform sampler2D image;
out vec4 color;
void main() { color = vec4(texture(image, texcoord).rgb, 1.0); })";

unsigned compile(GLenum type, const char *source) {
  const auto shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok{};
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    glDeleteShader(shader);
    throw std::runtime_error("Core title: shader");
  }
  return shader;
}

}  // namespace

std::string ResolveCoreDiscPath(const std::string &gameRoot, int disc,
                                const std::string &file) {
  if (disc < 0 || file.empty()) return {};
  std::string path = gameRoot + "/disc" + std::to_string(disc + 1) + "/files";
  // the language pack in use replaces some of the disc's files (port/docs/sprachpakete.md)
  const std::string packed = re4lang::DiscFile(path, file);
  if (packed != path + "/" + file) return packed;
  std::stringstream parts(file);
  std::string part;
  while (std::getline(parts, part, '/')) {
    if (part.empty() || part == "." || part == "..") return {};
    if (exists(path + "/" + part)) {
      path += "/" + part;
      continue;
    }
    DIR *dir = opendir(path.c_str());
    if (!dir) return {};
    std::string found;
    while (const dirent *e = readdir(dir))
      if (sameName(part, e->d_name)) {
        found = e->d_name;
        break;
      }
    closedir(dir);
    if (found.empty()) return {};
    path += "/" + found;
  }
  return exists(path) ? path : std::string{};
}

namespace {

std::vector<std::uint8_t> entryFrom(const RoomArchive &archive, const Re4CoreSource &source) {
  if (source.entry < 0 || std::size_t(source.entry) >= archive.entries().size())
    throw std::runtime_error("Core: archive entry");
  auto bytes = archive.copy(std::size_t(source.entry));
  if (source.innerOffset >= bytes.size()) throw std::runtime_error("Core: offset outside the entry");
  bytes.erase(bytes.begin(), bytes.begin() + source.innerOffset);
  return bytes;
}

}  // namespace

std::vector<std::uint8_t> ReadCoreSource(const std::string &gameRoot, int disc,
                                         const Re4CoreSource &source, const RoomArchive *packed) {
  if (!known(source)) throw std::runtime_error("Core: unknown model source");
  if (source.fileOffset == 0xFFFFFFFFu) {
    if (!packed) throw std::runtime_error("Core: model in a Yz2-packed archive of another room");
    return entryFrom(*packed, source);
  }
  const auto path = ResolveCoreDiscPath(gameRoot, disc, source.file);
  if (path.empty()) throw std::runtime_error(std::string("Core: file missing: ") + source.file);
  if (source.archiveSize < 0x20 || source.archiveSize > 64u * 1024 * 1024)
    throw std::runtime_error("Core: archive size");
  std::ifstream in(path, std::ios::binary);
  if (source.entry == -2) {  // file data at an offset (event file)
    if (source.innerOffset >= source.archiveSize) throw std::runtime_error("Core: offset outside the file part");
    std::vector<std::uint8_t> bytes(source.archiveSize - source.innerOffset);
    in.seekg(std::streamoff(source.fileOffset) + source.innerOffset);
    if (!in.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size())))
      throw std::runtime_error(std::string("Core: data outside the file: ") + source.file);
    return bytes;
  }
  std::vector<std::uint8_t> bytes(source.archiveSize);
  in.seekg(source.fileOffset);
  if (!in.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size())))
    throw std::runtime_error(std::string("Core: archive outside the file: ") + source.file);
  return entryFrom(RoomArchive(std::move(bytes)), source);
}

bool CoreModelIsScenery(const Re4CoreModel &model) {
  return model.manager == 1 && model.id == 2 && model.scrollIndex != RE4_CORE_NO_SCROLL;
}

bool CoreModelDrawn(const Re4CoreModel &model) {
  return (model.beFlag & 6) == 6 && !(model.flags & RE4_CORE_MODEL_HIDDEN);
}

GenericActorStyle CoreModelStyle(const Re4CoreModel &model, const Re4CoreModelInfo &info) {
  GenericActorStyle style;
  // trans.cpp ModelRender / commonModelTrans: GXSetBlendMode row of the info,
  // GXSetZMode of the model; a fade (opacity below 1) scales the info's
  // material alpha
  style.blendMode = info.blendMode <= 4 ? info.blendMode : 0;
  style.zMode = model.zMode <= 2 ? model.zMode : 0;
  style.cullMode = model.cullMode <= 2 ? model.cullMode : 0;
  const float fade = model.opacity * info.opacity;
  if (fade < 1.f) style.color[3] = std::clamp(info.color[3] / 255.f * fade, 0.f, 1.f);
  return style;
}

bool CoreSceneLogBounds = false;

namespace {

// row-major 3x4 matrices as the game keeps them (Mtx)
Matrix34 toMatrix(const float *m) {
  Matrix34 r{};
  std::copy(m, m + 12, r.begin());
  return r;
}

Matrix34 multiply(const Matrix34 &a, const Matrix34 &b) {
  Matrix34 r{};
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 4; ++col)
      r[row * 4 + col] = a[row * 4] * b[col] + a[row * 4 + 1] * b[4 + col] + a[row * 4 + 2] * b[8 + col] +
                         (col == 3 ? a[row * 4 + 3] : 0.f);
  return r;
}

Matrix34 inverse(const Matrix34 &m) {
  const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
  const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (std::fabs(det) < 1e-12f) return Matrix34{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
  const float k = 1.f / det;
  Matrix34 r{(e * i - f * h) * k, (c * h - b * i) * k, (b * f - c * e) * k, 0,
             (f * g - d * i) * k, (a * i - c * g) * k, (c * d - a * f) * k, 0,
             (d * h - e * g) * k, (b * g - a * h) * k, (a * e - b * d) * k, 0};
  for (int row = 0; row < 3; ++row)
    r[row * 4 + 3] = -(r[row * 4] * m[3] + r[row * 4 + 1] * m[7] + r[row * 4 + 2] * m[11]);
  return r;
}

bool isIdentity(const float *m) {
  static const float id[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
  for (int k = 0; k < 12; ++k)
    if (std::fabs(m[k] - id[k]) > 1e-6f) return false;
  return true;
}

}  // namespace

struct CoreScene::Actor {
  std::unique_ptr<GenericEventActor> model;
  bool gpu{};
  std::uint64_t revision{};  // the mesh revision on the GPU (shape morphs change it)
  unsigned joints{};  // highest skin joint + 1
  std::string error;
};

struct CoreScene::RoomState {
  std::uint16_t id{};
  std::shared_ptr<const RoomLighting> lighting;
};

CoreScene::CoreScene(std::string gameRoot) : gameRoot_(std::move(gameRoot)) {}

CoreScene::~CoreScene() = default;

void CoreScene::contextCreated() {
  ForgetSceneGl();
  ForgetGenericActorGl();
  for (auto &a : actors_) a.second->gpu = false;
  room_.reset();  // rebuilt from the prepared file on the next frame
  titleTexture_ = titleProgram_ = titleVao_ = titleVbo_ = 0;
  titleTried_ = false;
  std::lock_guard<std::mutex> lock(lock_);
  if (!preparedPath_.empty()) preparedPending_ = true;
}

void CoreScene::roomPrepared(std::uint16_t room, const std::string &decodedPath,
                             const std::string &error) {
  std::lock_guard<std::mutex> lock(lock_);
  preparedRoom_ = room;
  preparedPath_ = decodedPath;
  preparedError_ = error;
  preparedPending_ = true;
}

int CoreScene::wantedRoom() const {
  std::lock_guard<std::mutex> lock(lock_);
  return wantedRoom_;
}

std::string CoreScene::status() const {
  std::lock_guard<std::mutex> lock(lock_);
  return status_;
}

CoreScene::Actor *CoreScene::actorFor(const Re4CoreModelInfo &mi, int disc) {
  if (!known(mi.bin) || !known(mi.tpl)) return nullptr;
  const auto key = sourceKey(mi.bin) + "|" + sourceKey(mi.tpl);
  auto &slot = actors_[key];
  if (!slot) {
    slot = std::make_unique<Actor>();
    try {
      auto packedFor = [&](const Re4CoreSource &src) -> const RoomArchive * {
        return roomArchive_ && sameName(roomArchiveName_, src.file) ? roomArchive_.get() : nullptr;
      };
      auto bin = ReadCoreSource(gameRoot_, disc, mi.bin, packedFor(mi.bin));
      auto tpl = ReadCoreSource(gameRoot_, disc, mi.tpl, packedFor(mi.tpl));
      slot->model = std::make_unique<GenericEventActor>(key, key, std::move(bin), std::move(tpl));
      for (const auto &corner : slot->model->skin())
        for (const auto *w : {&corner.position, &corner.normal})
          for (unsigned j = 0; j < w->count; ++j)
            slot->joints = std::max<unsigned>(slot->joints, w->joints[j] + 1u);
      CORE_LOG("model %s: %u joints", key.c_str(), slot->joints);
    } catch (const std::exception &e) {
      slot->model.reset();
      slot->error = e.what();
      CORE_LOG("model %s: %s", key.c_str(), e.what());
    }
  }
  if (!slot->model) return nullptr;
  if (!slot->gpu) {
    CreateGenericActorGl(slot->model.get(), slot->model->mesh(), slot->model->skin());
    slot->gpu = true;
    slot->revision = slot->model->meshRevision();
  }
  return slot.get();
}

void CoreScene::drawActors(const CoreFrame &frame, const float *camera,
                           const std::array<float, 3> &center, float scale,
                           bool normalView) {
  static const Matrix34 identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
  std::vector<Matrix34> pose;
  std::vector<std::array<float, 3>> bind;
  unsigned drawn = 0, missing = 0, scenery = 0;
  // two passes like the game's ordering tables: the plain models first, then
  // what blends onto them (glass, light glows: added on, faded, or without a
  // depth write), so a later plain model cannot cover a glow drawn before it
  for (int pass = 0; pass < 2; ++pass)
  for (const auto &m : frame.models) {
    if (!CoreModelDrawn(m) || m.nParts == 0 ||
        std::size_t(m.firstMatrix) + m.nParts > frame.parts.size())
      continue;
    if (CoreModelIsScenery(m) && roomSceneHolds(m)) {  // part of the room scene
      scenery += pass == 0;
      continue;
    }
    pose.resize(m.nParts);
    bind.resize(m.nParts);
    for (unsigned i = 0; i < m.nParts; ++i) {
      const auto &p = frame.parts[m.firstMatrix + i];
      std::copy(p.matrix, p.matrix + 12, pose[i].begin());
      bind[i] = {p.bind[0], p.bind[1], p.bind[2]};
    }
    const std::array<float, 3> light{pose[0][3], pose[0][7] + 800, pose[0][11]};
    for (unsigned k = 0; k < m.nInfos && std::size_t(m.firstInfo) + k < frame.infos.size(); ++k) {
      // the core cannot always name the source (a sub screen swaps memory
      // out for a while): the last source seen for the same data stays valid
      Re4CoreModelInfo mi = frame.infos[m.firstInfo + k];
      const auto memoryKey = (std::uint64_t(m.address) << 8) | k;
      auto &remembered = knownSources_[memoryKey];
      if (known(mi.bin) && known(mi.tpl)) {
        remembered = mi;
      } else if (remembered.binAddress == mi.binAddress && remembered.tplAddress == mi.tplAddress &&
                 known(remembered.bin) && known(remembered.tpl)) {
        mi = remembered;
      }
      // trans.cpp commonModelTrans: drawn only with be_flag 8; a rigid model
      // hangs on one part (or the model matrix) with its vertices in that
      // space; a skinned one is posed by all parts; the info's own matrix on top
      if (!(mi.flags & 8)) continue;
      const GenericActorStyle style = CoreModelStyle(m, mi);
      const bool blended = style.blendMode == 1 || style.blendMode == 2 || style.blendMode == 3 ||
                           style.zMode != 0 || style.color[3] < 1.f;
      if (int(blended) != pass) continue;
      const std::vector<Matrix34> *usePose = &pose;
      const std::vector<std::array<float, 3>> *useBind = &bind;
      if (mi.rigidPart >= 0 || mi.rigidPart == -2) {
        if (mi.rigidPart >= int(m.nParts)) {
          ++missing;
          continue;
        }
        const auto base = mi.rigidPart == -2 ? toMatrix(m.matrix) : pose[std::size_t(mi.rigidPart)];
        rigidPose_.assign(1, multiply(base, toMatrix(mi.matrix)));
        rigidBind_.assign(1, {0.f, 0.f, 0.f});
        usePose = &rigidPose_;
        useBind = &rigidBind_;
      } else if (!isIdentity(mi.matrix)) {
        // skinned in the root's space (CalcSk1), drawn with root * info matrix
        const auto pre = multiply(multiply(pose[0], toMatrix(mi.matrix)), inverse(pose[0]));
        movedPose_.resize(pose.size());
        for (std::size_t i = 0; i < pose.size(); ++i) movedPose_[i] = multiply(pre, pose[i]);
        usePose = &movedPose_;
      }
      Actor *a = actorFor(mi, frame.disc);
      if (!a || a->joints > usePose->size() || usePose->size() > 128) {
        ++missing;
        continue;
      }
      // a talking / blinking face: the game's morphed positions (shape.cpp)
      if (mi.nMorph && std::size_t(mi.firstMorph) + mi.nMorph <= frame.morph.size())
        a->model->setCorePositions(reinterpret_cast<const std::int16_t(*)[4]>(frame.morph[mi.firstMorph].data()),
                                   mi.nMorph);
      else
        a->model->clearCorePositions();
      if (a->revision != a->model->meshRevision()) {
        UpdateGenericActorGl(a->model.get(), a->model->mesh());
        a->revision = a->model->meshRevision();
      }
      DrawGenericActorGl(a->model.get(), camera, center, scale, *usePose, *useBind, identity,
                         normalView, 1, &light, 1000.f, &style);
      ++drawn;
      if (CoreSceneLogBounds) {
        // the corners skinned on the CPU like the shader: world bounds of this info
        const auto &mesh = a->model->mesh();
        const auto &skin = a->model->skin();
        std::array<float, 3> lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
        unsigned outside = 0;
        for (std::size_t i = 0; i < skin.size() && i * 3 + 2 < mesh.vertices.size(); ++i) {
          const float v[3] = {mesh.vertices[i * 3], mesh.vertices[i * 3 + 1], mesh.vertices[i * 3 + 2]};
          std::array<float, 3> w{};
          const auto &bw = skin[i].position;
          for (unsigned j = 0; j < bw.count; ++j) {
            const auto bone = bw.joints[j];
            if (bone >= usePose->size()) {
              ++outside;
              continue;
            }
            const auto &mtx = (*usePose)[bone];
            const auto &b = (*useBind)[bone];
            for (int r = 0; r < 3; ++r)
              w[r] += bw.rates[j] * (mtx[r * 4] * (v[0] - b[0]) + mtx[r * 4 + 1] * (v[1] - b[1]) +
                                     mtx[r * 4 + 2] * (v[2] - b[2]) + mtx[r * 4 + 3]);
          }
          for (int r = 0; r < 3; ++r) {
            lo[r] = std::min(lo[r], w[r]);
            hi[r] = std::max(hi[r], w[r]);
          }
        }
        unsigned alphaDraws = 0;
        for (const auto &d : mesh.draws) alphaDraws += d.alphaTest ? 1u : 0u;
        CORE_LOG("bounds frame %u model %08x id %02x info %u (%s): %zu corners, joints %u, %zu draws (%u alpha "
                 "test), world (%.0f %.0f %.0f)..(%.0f %.0f %.0f), part 0 at (%.0f %.0f %.0f)%s",
                 unsigned(frame.frame), unsigned(m.address), unsigned(m.id), k,
                 mi.rigidPart >= 0 ? "rigid" : mi.rigidPart == -2 ? "model matrix" : "skinned", skin.size(), a->joints,
                 mesh.draws.size(), alphaDraws, lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], pose[0][3], pose[0][7],
                 pose[0][11], outside ? " (joints beyond the parts)" : "");
        // what keeps triangles of this info off the picture: parts the game
        // scales to 0 (hidden pieces), the alpha test (vertex alpha times the
        // alpha image at or below the reference), the game's draw state
        std::string flat;
        std::vector<bool> zero(usePose->size());
        for (std::size_t i = 0; i < usePose->size(); ++i) {
          const auto &mtx = (*usePose)[i];
          float most = 0;
          for (int c = 0; c < 3; ++c)
            most = std::max(most, std::fabs(mtx[c]) + std::fabs(mtx[4 + c]) + std::fabs(mtx[8 + c]));
          zero[i] = most < 1e-3f;
          if (zero[i]) flat += (flat.empty() ? "" : " ") + std::to_string(i);
        }
        std::size_t onFlat = 0, cutByVertexAlpha = 0, cutByImage = 0, alphaTris = 0;
        for (const auto &d : mesh.draws) {
          const bool alpha = d.alphaTest && d.alphaTexture >= 0 && std::size_t(d.alphaTexture) < mesh.textures.size();
          float imageCut = 0;
          if (alpha) {
            const auto &img = mesh.textures[std::size_t(d.alphaTexture)].rgba;
            std::size_t below = 0;
            for (std::size_t t = 3; t < img.size(); t += 4) below += img[t] <= d.alphaReference;
            imageCut = img.empty() ? 1.f : float(below) / float(img.size() / 4);
          }
          for (std::size_t c = d.first; c + 2 < d.first + d.count; c += 3) {
            bool allFlat = true;
            for (std::size_t j = c; j < c + 3 && j < skin.size(); ++j)
              for (unsigned w = 0; w < skin[j].position.count; ++w)
                allFlat = allFlat && skin[j].position.joints[w] < zero.size() && zero[skin[j].position.joints[w]];
            onFlat += allFlat;
            if (!alpha) continue;
            ++alphaTris;
            bool vertexCut = true;
            for (std::size_t j = c; j < c + 3 && j * 4 + 3 < mesh.colors.size(); ++j)
              vertexCut = vertexCut && (d.vertexColors ? mesh.colors[j * 4 + 3] : 255u) <= d.alphaReference;
            cutByVertexAlpha += vertexCut;
            cutByImage += imageCut > 0.9f;
          }
        }
        const auto style = CoreModelStyle(m, mi);
        CORE_LOG("  info %u: %zu of %zu triangles on parts scaled to 0 (parts: %s); alpha test on %zu, cut by "
                 "vertex alpha %zu, by a mostly clear alpha image %zu; blend %u, depth %u, cull %u, alpha %.2f, "
                 "order class %u",
                 k, onFlat, mesh.triangles, flat.empty() ? "none" : flat.c_str(), alphaTris, cutByVertexAlpha,
                 cutByImage, style.blendMode, style.zMode, style.cullMode, style.color[3], unsigned(m.otType));
      }
    }
  }
  drawnModels_ = drawn;
  missingModels_ = missing;
  sceneryModels_ = scenery;
}

bool CoreScene::roomSceneHolds(const Re4CoreModel &m) const {
  const auto b = std::size_t(m.scrollBlock + 1);
  return m.scrollBlock >= -1 && b < scroll_.size() && m.scrollIndex < scroll_[b].size() &&
         scroll_[b][m.scrollIndex].inScene;
}

void CoreScene::updateScenery(const CoreFrame &frame) {
  ++scrollFrame_;
  scrollMapped_ = false;
  hiddenScenery_ = movedScenery_ = 0;
  static const std::array<float, 12> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
  for (const auto &m : frame.models) {
    if (!CoreModelIsScenery(m) || !roomSceneHolds(m)) continue;
    auto &w = scroll_[std::size_t(m.scrollBlock + 1)][m.scrollIndex];
    scrollMapped_ = true;
    w.seen = scrollFrame_;
    w.shown = CoreModelDrawn(m);
    // the game's matrix against the placement the scene was built with: a
    // script moved it (a door, a falling bridge part ...)
    const auto delta = multiply(toMatrix(m.matrix), inverse(w.placement));
    bool moved = false;
    for (int k = 0; k < 12; ++k)
      moved = moved || std::fabs(delta[k] - identity[k]) > (k % 4 == 3 ? 1.f : 1e-3f);
    w.moved = moved;
    w.delta = moved ? delta : identity;
    hiddenScenery_ += !w.shown;
    movedScenery_ += w.shown && moved;
  }
}

void CoreScene::buildRoom(std::uint16_t id, const std::string &path) {
  if (!coreLoaded_) {
    coreLoaded_ = true;
    try {
      const auto core = ResolveCoreDiscPath(gameRoot_, 0, "etc/core.das");
      auto archive = RoomArchive::LoadDrs(core);
      if (const auto *eff = archive.find("EFF"))
        coreEff_ = archive.copy(std::size_t(eff - archive.entries().data()));
    } catch (const std::exception &e) {
      CORE_LOG("core.das: %s (no sky/water)", e.what());
    }
  }
  auto arc = RoomArchive::Load(path);
  auto copy = [&](const ArchiveEntry *e) { return arc.copy(std::size_t(e - arc.entries().data())); };
  const auto *smdEntry = arc.find("SMD");
  if (!smdEntry) throw std::runtime_error("Core room: no SMD");
  Smd smd(copy(smdEntry));
  std::unique_ptr<Smd> common;
  if (const auto *e = arc.find("SMD", 1)) common = std::make_unique<Smd>(copy(e), smd.commonRequirements());
  std::unique_ptr<Smx> smx;
  if (const auto *e = arc.find("SMX")) smx = std::make_unique<Smx>(copy(e));
  auto state = std::make_unique<RoomState>();
  state->id = id;
  if (arc.find("LIT")) state->lighting = ReadRoomLighting(arc);
  auto scene = BuildStaticScene(smd, -1, common.get());
  scene.lighting = state->lighting;
  scene.shadows = ReadRoomShadows(arc);
  AttachSceneTextures(smd, scene, common.get());
  if (smx) ApplySceneSmx(*smx, scene);
  if (smx && !coreEff_.empty()) {
    try {
      AttachSceneEnvironment(BuildRoomEnvironment(arc, smd, *smx, id, coreEff_), scene);
    } catch (const std::exception &e) {
      CORE_LOG("room %03x environment: %s", unsigned(id), e.what());
    }
  }
  // the SMD works (room, then the scroll blocks) and which the scene holds
  scroll_.assign(1, {});
  for (const auto &w : smd.placements) {
    ScrollWork work;
    work.placement = PlacementMatrix(w);
    scroll_[0].push_back(work);
  }
  if (id == 0x100) {
    try {
      auto blocks = BuildRoomBlocks(arc, smx.get(), path, id);
      AppendStaticSceneLayer(scene, blocks.mesh);
      for (const auto &placements : blocks.placements) {
        scroll_.emplace_back();
        for (const auto &m : placements) {
          ScrollWork work;
          work.placement = m;
          scroll_.back().push_back(work);
        }
      }
    } catch (const std::exception &e) {
      CORE_LOG("room %03x blocks: %s", unsigned(id), e.what());
    }
  }
  for (const auto &d : scene.draws) {
    const auto b = std::size_t(d.roomBlock + 1);
    if (d.smdIndex >= 0 && b < scroll_.size() && std::size_t(d.smdIndex) < scroll_[b].size())
      scroll_[b][std::size_t(d.smdIndex)].inScene = true;
  }
  DestroySceneGl();
  CreateSceneGl(scene);
  room_ = std::move(state);
  knownSources_.clear();
  char name[32];
  std::snprintf(name, sizeof(name), "st%x/r%03x.das", unsigned(id >> 8), unsigned(id));
  roomArchiveName_ = name;
  roomArchive_ = std::make_unique<RoomArchive>(std::move(arc));
  CORE_LOG("room %03x: %zu triangles, %zu textures", unsigned(id), scene.triangles,
           scene.textures.size());
}

void CoreScene::drawTitle(int width, int height) {
  if (!titleTried_) {
    titleTried_ = true;
    try {
      TitleData title(gameRoot_ + "/disc1/files", 1);
      const Texture *best = nullptr;
      for (const auto &t : title.textures())
        if (t.frame == 0 && !t.image.rgba.empty() &&
            (!best || t.image.width * t.image.height > best->width * best->height))
          best = &t.image;
      if (!best) throw std::runtime_error("no title picture");
      glGenTextures(1, &titleTexture_);
      glBindTexture(GL_TEXTURE_2D, titleTexture_);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLsizei(best->width), GLsizei(best->height), 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, best->rgba.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      titleWidth_ = best->width;
      titleHeight_ = best->height;
      const auto v = compile(GL_VERTEX_SHADER, kTitleVertex);
      const auto f = compile(GL_FRAGMENT_SHADER, kTitleFragment);
      titleProgram_ = glCreateProgram();
      glAttachShader(titleProgram_, v);
      glAttachShader(titleProgram_, f);
      glLinkProgram(titleProgram_);
      glDeleteShader(v);
      glDeleteShader(f);
      const float quad[] = {-1, -1, 0, 1, 1, -1, 1, 1, -1, 1, 0, 0, 1, 1, 1, 0};
      glGenVertexArrays(1, &titleVao_);
      glBindVertexArray(titleVao_);
      glGenBuffers(1, &titleVbo_);
      glBindBuffer(GL_ARRAY_BUFFER, titleVbo_);
      glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                            reinterpret_cast<void *>(2 * sizeof(float)));
      glEnableVertexAttribArray(1);
      CORE_LOG("title picture %ux%u", titleWidth_, titleHeight_);
    } catch (const std::exception &e) {
      titleTexture_ = 0;
      CORE_LOG("title picture: %s", e.what());
    }
  }
  glViewport(0, 0, width, height);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  if (!titleTexture_ || !titleProgram_) return;
  // the picture keeps its own aspect ratio, centred on the screen
  const float screen = float(std::max(width, 1)) / float(std::max(height, 1));
  const float picture = float(titleWidth_) / float(std::max(titleHeight_, 1u));
  const float sx = picture < screen ? picture / screen : 1.f;
  const float sy = picture < screen ? 1.f : screen / picture;
  glUseProgram(titleProgram_);
  glUniform2f(glGetUniformLocation(titleProgram_, "scale"), sx, sy);
  glUniform1i(glGetUniformLocation(titleProgram_, "image"), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, titleTexture_);
  glBindVertexArray(titleVao_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
}

void CoreScene::draw(const CoreFrame &frame, int width, int height, double seconds) {
  std::uint16_t preparedRoom{};
  std::string preparedPath, preparedError;
  bool pending = false;
  {
    std::lock_guard<std::mutex> lock(lock_);
    wantedRoom_ = frame.valid && frame.step[0] >= 2 ? int(frame.room) : -1;
    if (preparedPending_) {
      pending = true;
      preparedPending_ = false;
      preparedRoom = preparedRoom_;
      preparedPath = preparedPath_;
      preparedError = preparedError_;
    }
  }
  if (pending) {
    roomError_ = preparedError;
    if (preparedError.empty() && !preparedPath.empty()) {
      try {
        buildRoom(preparedRoom, preparedPath);
        roomError_.clear();
      } catch (const std::exception &e) {
        room_.reset();
        roomArchive_.reset();
        roomError_ = e.what();
        CORE_LOG("room %03x: %s", unsigned(preparedRoom), e.what());
      }
    }
  }
  const bool inRoom = frame.valid && frame.step[0] >= 2 && room_ && room_->id == frame.room;
  drawnModels_ = missingModels_ = sceneryModels_ = 0;
  if (inRoom && frame.cameraValid) {
    EventCamera camera;
    camera.eye = frame.eye;
    camera.target = frame.target;
    camera.roll = frame.roll;
    camera.fovRadians = frame.fovy * 3.14159265358979323846f / 180.f;
    camera.valid = true;
    SceneProbe probe;
    probe.roomCamera = &camera;
    probe.screenAspect = true;
    probe.hidePlayer = true;
    // the old app's collision capsule and ground patch (a yellow wire oval,
    // drawn at the probe's feet = the world origin): a debug aid of its own
    // movement code, which the core mode does not use
    probe.showBody = false;
    probe.extraActors = [&](const float *matrix, const std::array<float, 3> &center, float scale,
                            bool normalView) { drawActors(frame, matrix, center, scale, normalView); };
    // the room scene's SMD works as the core has them: a work the core does
    // not have (a scroll block not loaded) or hides is not drawn, a moved one
    // follows the game; while the core names no work at all (or dropped
    // models from the snapshot) the scene stays as it was built
    updateScenery(frame);
    if (scrollMapped_ && frame.droppedModels == 0)
      probe.coreScenery = [this](const SceneDraw &d, Matrix34 &delta) {
        const auto b = std::size_t(d.roomBlock + 1);
        if (b >= scroll_.size() || std::size_t(d.smdIndex) >= scroll_[b].size()) return true;
        const auto &w = scroll_[b][std::size_t(d.smdIndex)];
        if (w.seen != scrollFrame_ || !w.shown) return false;
        if (w.moved) delta = w.delta;
        return true;
      };
    DrawSceneGl(width, height, 0, 0, 0, false, &probe, nullptr, seconds);
  } else if (!frame.valid || frame.step[0] < 2) {
    drawTitle(width, height);
  } else {
    glViewport(0, 0, width, height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }
  char text[320];
  std::snprintf(text, sizeof(text),
                "frame %u  step %u/%u  room r%03x  disc %d%s%s\nmodels %zu (drawn %u, missing %u, scenery %u, "
                "hidden %u, moved %u)  room %s%s%s",
                unsigned(frame.frame), unsigned(frame.step[0]), unsigned(frame.step[1]),
                unsigned(frame.room), frame.disc + 1,
                (frame.screen & RE4_CORE_SCREEN_EVENT) ? "  event" : "",
                (frame.screen & RE4_CORE_SCREEN_SUB) ? "  MENU OPEN (not drawn yet)" : "",
                frame.models.size(), drawnModels_, missingModels_, sceneryModels_, hiddenScenery_, movedScenery_,
                inRoom ? "loaded" : room_ ? "other" : "none",
                roomError_.empty() ? "" : " - ", roomError_.c_str());
  std::lock_guard<std::mutex> lock(lock_);
  status_ = text;
}

}  // namespace re4
