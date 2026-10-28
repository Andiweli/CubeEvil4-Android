#include "scene_gl.h"
#include "village_bottles.h"
#include "player_projectiles.h"
#include "room_lighting_gl.h"
#include "cinematic_frame.h"
#include "model_textures_gl.h"
#include "room_filter_gl.h"
#include "room_shadows_gl.h"
#include "room_interactions.h"
#include "room_item_events.h"
#include "room_ladders.h"
#include "room_pickups.h"
#include "item_effects_gl.h"
#include "actor_gl.h"
#include "subscreen_portrait_gl.h"
#include "collision_query.h"
#include "destruction_effects.h"
#include "enemy_encounter.h"
#include "enemy_group.h"
#include "event_actor_gl.h"
#include "event_focus_gl.h"
#include "depth_of_field_gl.h"
#include "forest_animals.h"
#include "village_animals.h"
#include "dog_traps.h"
#include "r100_secondary.h"
#include "environment_gl.h"
#include "world_effects_gl.h"
#include "source_glow_gl.h"
#include "room_area_effects.h"
#include "room_effect_cloth_gl.h"
#include "follow_camera.h"
#include "object_actions.h"
#include "foot_shadow_gl.h"
#include "muzzle_effects.h"
#include <GLES3/gl3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>
namespace re4 {
namespace {
GLuint program{}, vao{}, vbo{}, probeVao{}, probeVbo{};
std::array<float, 3> worldCenter{};
std::shared_ptr<const LaserSightData> laserSight;
GLuint laserPointTexture{};
FollowCameraState followCamera;
FollowCameraPose followPose;
double followHeading{};
float worldScale = 1, followDistance = 6000, resolvedFollowDistance = 6000;
GLsizei count{};
std::vector<GLuint> textures;
ModelTextureLodGl textureLod;
std::vector<SceneDraw> draws;
std::vector<std::vector<std::size_t>> destructionDraws;
EnvironmentData environment;
float yaw = .6f, pitch = .45f, distance = 3.3f;
std::chrono::steady_clock::time_point lastFrame;
double sceneFrames = 0;
unsigned footShadowColor{};
double footShadowFrame{};
GLuint compile(GLenum type, const char *code) {
  auto s = glCreateShader(type);
  glShaderSource(s, 1, &code, nullptr);
  glCompileShader(s);
  GLint ok;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024]{};
    glGetShaderInfoLog(s, sizeof log, nullptr, log);
    glDeleteShader(s);
    throw std::runtime_error(log);
  }
  return s;
}
} // namespace
void ForgetSceneGl() {
  RoomAtmosphere() = {};
  laserPointTexture = 0;
  laserSight.reset();
  ForgetActorGl();
  ForgetEventActorsGl();
  ForgetSubscreenPortraitGl();
  // The scene owns persistent scroll/animal/radio uploads as well as EVDs.
  // Event teardown only forgets its own actors; a lost context forgets all.
  ForgetGenericActorGl();
  ForgetEventFocusGl();
  ForgetDepthOfFieldGl();
  ForgetRoomFilterGl();
  ForgetRoomShadowsGl();
  ForgetEnvironmentGl();
  ForgetWorldEffectsGl();
  ForgetMuzzleEffectsGl();
  ForgetRoomHitEffectsGl();
  ForgetVillageBottleEffectsGl();
  ForgetProjectileEffectsGl();
  ForgetEnemyEffectsGl();
  ForgetTrapEffectsGl();
  ForgetSourceGlowGl();
  ForgetRoomAreaEffectsGl();
  ForgetRoomEffectClothGl();
  ForgetFootShadowGl();
  ForgetItemEffectsGl();
  environment = {};
  followCamera = {};
  followPose = {};
  followHeading = 0;
  program = vao = vbo = probeVao = probeVbo = 0;
  worldCenter = {};
  worldScale = 1;
  followDistance = 6000;
  resolvedFollowDistance = 6000;
  textures.clear();
  textureLod={};
  draws.clear();
  destructionDraws.clear();
  count = 0;
  yaw = .6f;
  pitch = .45f;
  distance = 3.3f;
  lastFrame = std::chrono::steady_clock::now();
  sceneFrames = 0;
  footShadowColor=0; footShadowFrame=0;
}
void DestroySceneGl() {
  if (laserPointTexture) glDeleteTextures(1, &laserPointTexture);
  DestroyActorGl();
  DestroyEventActorsGl();
  DestroySubscreenPortraitGl();
  DestroyGenericActorGl();
  DestroyEventFocusGl();
  DestroyDepthOfFieldGl();
  DestroyRoomFilterGl();
  DestroyRoomShadowsGl();
  DestroyEnvironmentGl();
  DestroyWorldEffectsGl();
  DestroyMuzzleEffectsGl();
  DestroyRoomHitEffectsGl();
  DestroyVillageBottleEffectsGl();
  DestroyProjectileEffectsGl();
  DestroyEnemyEffectsGl();
  DestroyTrapEffectsGl();
  DestroySourceGlowGl();
  DestroyRoomAreaEffectsGl();
  DestroyRoomEffectClothGl();
  DestroyFootShadowGl();
  DestroyItemEffectsGl();
  if (!textures.empty())
    glDeleteTextures(static_cast<GLsizei>(textures.size()), textures.data());
  if (probeVbo)
    glDeleteBuffers(1, &probeVbo);
  if (probeVao)
    glDeleteVertexArrays(1, &probeVao);
  if (vbo)
    glDeleteBuffers(1, &vbo);
  if (vao)
    glDeleteVertexArrays(1, &vao);
  if (program) {
    GLint current{};glGetIntegerv(GL_CURRENT_PROGRAM,&current);
    if(GLuint(current)==program)glUseProgram(0);
    glDeleteProgram(program);
  }
  ForgetSceneGl();
}
void CreateSceneGl(const StaticScene &scene) {
  ForgetSceneGl();
  RoomAtmosphere().data = scene.lighting;
  CreateRoomShadowsGl(scene.shadows);
  if (scene.vertices.empty())
    throw std::runtime_error("No supported rigid geometry");
  const char *vs = R"GLSL(#version 300 es
layout(location=0) in vec3 p;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 rgba;
layout(location=3) in vec3 normal;
uniform mat4 camera, modelDelta;
uniform mat3 normalDelta;
uniform int background, vertexColors;
uniform vec3 roomCenter;
uniform float roomScale;
out vec3 worldNormal, world;
out vec4 rasterColor;
out vec2 texcoord;
void main() {
  world=(modelDelta*vec4(p,1)).xyz;
  texcoord=uv; worldNormal=normalDelta*normal;
  rasterColor=litRoomColor(rgba,world/roomScale+roomCenter,worldNormal,vertexColors);
  gl_Position=camera*vec4(world,1);
  // cObjScr's sky is an ordinary authored model. Retain its projected
  // depth; moving it onto the far plane changes visibility and GX fog.
}
)GLSL";
  const char *fs = R"GLSL(#version 300 es
precision highp float;
in vec3 world, worldNormal;
in vec4 rasterColor;
in vec2 texcoord;
out vec4 color;
uniform int vertexColors, normalView, textured, alphaTest, unlit;
uniform vec3 materialRGB, roomCenter;
uniform float roomScale;
uniform sampler2D image, alphaImage, sceneImage;
uniform float alphaReference, materialAlpha, refractPower, refractRatio;
uniform vec2 uvOffset, viewport;
uniform int liquid, textureAlpha, refractOn;
void main() {
  vec2 st=texcoord+uvOffset;
  vec4 c=unlit!=0?vec4(1):(vertexColors!=0 || roomLightingEnabled!=0)?rasterColor:vec4(1);
  if(unlit!=0 || roomLightingEnabled==0)c.rgb*=materialRGB;
  c.a*=materialAlpha;
  vec3 n=normalize(cross(dFdx(world),dFdy(world)));
  float l=.25+.75*abs(dot(n,normalize(vec3(.3,.8,.5))));
  color=vec4(vec3(.32,.66,.74)*l,1);
  if(vertexColors!=0 || roomLightingEnabled!=0)color=c;
  if(textured!=0) {
    vec4 t=modelTexture(image,liquid!=0?vec2(st.x,1.0-st.y):st);
    color=vec4(t.rgb*c.rgb,c.a);
    if(liquid!=0 && textureAlpha!=0)color.a*=t.a;
    if(alphaTest!=0) {
      float a=modelTexture(alphaImage,st).a*c.a;
      if(floor(a*255.0+.5)<=alphaReference)discard;
      color.a=a;
    }
    if(refractOn!=0) {
      // GLES adapter of RefractShaderSetup type 2. The source matrix columns
      // use indirect texture A/B/G with signed offsets; room power is /512.
      vec3 ind=t.abg-vec3(128.0/255.0);
      vec2 warp=vec2(ind.y-ind.z,ind.x+ind.z*1.35)*refractPower;
      vec3 under=texture(sceneImage,clamp(gl_FragCoord.xy/viewport+warp,vec2(0),vec2(1))).rgb;
      color.rgb=mix(under,color.rgb,refractRatio);
    }
  }
  color.rgb=specularRoomColor(color.rgb,c.rgb,worldNormal);
  if(normalView!=0){float size=length(worldNormal);color.rgb=size>0.0?worldNormal/size*.5+.5:vec3(1,0,1);}
  else {
    // ShaderSetup's final clamped TEV stage precedes GX fog.
    color.rgb=clamp(color.rgb*roomTevScale.x,0.0,1.0);
    color=fogRoomColor(color,world/roomScale+roomCenter);
  }
}
)GLSL";
  const auto litVertex=RoomAtmosphereShader(vs,true), litFragment=RoomSpecularShader(RoomAtmosphereShader(fs,false));
  auto a = compile(GL_VERTEX_SHADER, litVertex.c_str());
  GLuint b = 0;
  try {
    b = compile(GL_FRAGMENT_SHADER, litFragment.c_str());
  } catch (...) {
    glDeleteShader(a);
    throw;
  }
  program = glCreateProgram();
  glAttachShader(program, a);
  glAttachShader(program, b);
  glLinkProgram(program);
  glDeleteShader(a);
  glDeleteShader(b);
  GLint ok;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[2048]{}; glGetProgramInfoLog(program,sizeof(log),nullptr,log);
    throw std::runtime_error(std::string("3D shader linking: ")+log);
  }
  float extent = 0;
  std::array<float, 3> center{};
  for (unsigned k = 0; k < 3; ++k) {
    center[k] = scene.minimum[k] * .5f + scene.maximum[k] * .5f;
    extent = std::max(extent, scene.maximum[k] - scene.minimum[k]);
  }
  if (!std::isfinite(extent) || extent <= 0)
    throw std::runtime_error("Invalid scene bounds");
  worldCenter = center;
  worldScale = 2 / extent;
  auto vertices = scene.vertices;
  for (std::size_t i = 0; i < vertices.size(); ++i)
    vertices[i] = (vertices[i] - center[i % 3]) * 2 / extent;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  if (scene.uv.size() != vertices.size() / 3 * 2)
    throw std::runtime_error("3D: UV count");
  if (scene.colors.size() != vertices.size() / 3 * 4)
    throw std::runtime_error("3D: Color count");
  if (scene.normals.size() != vertices.size())
    throw std::runtime_error("3D: Normal count");
  std::vector<float> interleaved;
  interleaved.reserve(vertices.size() / 3 * 12);
  for (std::size_t i = 0; i < vertices.size() / 3; ++i) {
    interleaved.insert(interleaved.end(), vertices.begin() + i * 3,
                       vertices.begin() + i * 3 + 3);
    interleaved.push_back(scene.uv[i * 2]);
    interleaved.push_back(scene.uv[i * 2 + 1]);
    for (unsigned k = 0; k < 4; ++k)
      interleaved.push_back(scene.colors[i * 4 + k] / 255.f);
    interleaved.insert(interleaved.end(), scene.normals.begin() + i * 3,
                       scene.normals.begin() + i * 3 + 3);
  }
  glBufferData(GL_ARRAY_BUFFER, interleaved.size() * sizeof(float),
               interleaved.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12 * sizeof(float), nullptr);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 12 * sizeof(float),
                        reinterpret_cast<void *>(3 * sizeof(float)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 12 * sizeof(float),
                        reinterpret_cast<void *>(5 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 12 * sizeof(float),
                        reinterpret_cast<void *>(9 * sizeof(float)));
  glEnableVertexAttribArray(3);
  // Merge contiguous primitives sharing the resolved base texture: avoid
  // thousands of tiny draw calls while preserving original submission order.
  for (const auto &draw : scene.draws) {
    if (!draws.empty() && draws.back().texture == draw.texture &&
        draws.back().alphaTest == draw.alphaTest &&
        draws.back().vertexColors == draw.vertexColors &&
        draws.back().materialRGB == draw.materialRGB &&
        draws.back().blendMode == draw.blendMode &&
        draws.back().zMode == draw.zMode &&
        draws.back().cullMode == draw.cullMode &&
        draws.back().roomBlock == draw.roomBlock &&
        draws.back().orderType == draw.orderType &&
        draws.back().object == draw.object &&
        draws.back().roomObjectSlot == draw.roomObjectSlot &&
        draws.back().roomObjectVariant == draw.roomObjectVariant &&
        draws.back().roomLockParent == draw.roomLockParent &&
        draws.back().roomItemIndex == draw.roomItemIndex &&
        draws.back().roomPickupIndex == draw.roomPickupIndex &&
        draws.back().roomItemId == draw.roomItemId &&
        draws.back().lightMask == draw.lightMask &&
        draws.back().lightSize == draw.lightSize &&
        // ModelTrans culls each placed model. Equal materials are not
        // sufficient: retaining only the first tree's bound makes every
        // merged neighbour pop in when that first tree enters the frustum.
        draws.back().sortCenter == draw.sortCenter &&
        draws.back().lightVolume.center == draw.lightVolume.center &&
        draws.back().lightVolume.size == draw.lightVolume.size &&
        draws.back().lightVolume.axes == draw.lightVolume.axes &&
        draws.back().lightVolume.type == draw.lightVolume.type &&
        draws.back().noDiffuseLight == draw.noDiffuseLight &&
        draws.back().scrollLightAttribute == draw.scrollLightAttribute &&
        draws.back().destructionMesh == draw.destructionMesh &&
        draws.back().authored == draw.authored &&
        draws.back().sky == draw.sky && draws.back().water == draw.water &&
        draws.back().uvSpeed == draw.uvSpeed &&
        draws.back().alphaTexture == draw.alphaTexture &&
        draws.back().alphaReference == draw.alphaReference &&
        draws.back().specularTexture == draw.specularTexture &&
        draws.back().specularType == draw.specularType &&
        draws.back().specularScale == draw.specularScale &&
        draws.back().specularNormalScale == draw.specularNormalScale &&
        draws.back().specularRGB == draw.specularRGB &&
        draws.back().specularEnabled == draw.specularEnabled &&
        draws.back().first + draws.back().count == draw.first)
      draws.back().count += draw.count;
    else
      draws.push_back(draw);
  }
  for (std::size_t i = 0; i < draws.size(); ++i) {
    const auto mesh = draws[i].destructionMesh;
    if (mesh < 0)
      continue;
    if (mesh >= 256)
      throw std::runtime_error("3D: Fragment model budget");
    if (destructionDraws.size() <= std::size_t(mesh))
      destructionDraws.resize(std::size_t(mesh) + 1);
    destructionDraws[mesh].push_back(i);
  }
  glActiveTexture(GL_TEXTURE0);
  textures.resize(scene.textures.size());
  if (!textures.empty())
    glGenTextures(static_cast<GLsizei>(textures.size()), textures.data());
  auto wrap = [](unsigned mode) {
    return mode == 1   ? GL_REPEAT
           : mode == 2 ? GL_MIRRORED_REPEAT
                       : GL_CLAMP_TO_EDGE;
  };
  for (std::size_t i = 0; i < textures.size(); ++i) {
    const auto &t = scene.textures[i];
    textureLod.append(t);
    glBindTexture(GL_TEXTURE_2D, textures[i]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    t.mips.empty() ? GL_LINEAR : GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,
                    static_cast<GLint>(t.mips.size()));
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD,
                    t.mips.empty() ? 0.f : float(t.minLOD));
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, float(t.mips.size()));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap(t.wrapS));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap(t.wrapT));
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.width, t.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, t.rgba.data());
    for (std::size_t level = 0; level < t.mips.size(); ++level) {
      const auto &mip = t.mips[level];
      glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level + 1), GL_RGBA8,
                   mip.width, mip.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                   mip.rgba.data());
    }
  }
  laserSight = scene.laserSight;
  if (laserSight) {
    const auto &image = laserSight->pointTexture;
    if (!image.width || !image.height || image.width > 1024 || image.height > 1024 ||
        image.rgba.size() != std::size_t(image.width) * image.height * 4)
      throw std::runtime_error("Laser: GPU texture budget");
    glGenTextures(1, &laserPointTexture);
    glBindTexture(GL_TEXTURE_2D, laserPointTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width, image.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
  }
  environment = scene.environment;
  CreateEnvironmentGl(environment);
  CreateWorldEffectsGl(scene.worldEffects);
  CreateFootShadowGl(scene.footShadowTexture);
  count = static_cast<GLsizei>(vertices.size() / 3);
  if (glGetError() != GL_NO_ERROR) {
    count = 0;
    throw std::runtime_error("3D GPU upload failed");
  }
}
FollowCameraState SceneFollowState() { return followCamera; }
void SceneAdvanceFollowInput(double horizontal, double vertical, double seconds) {
  AdvanceFollowCamera(followCamera, horizontal, vertical, seconds);
}
void SceneSeedFollowView(double relativeYaw, double vertical) {
  if (!std::isfinite(relativeYaw) || !std::isfinite(vertical))
    throw std::runtime_error("Camera: invalid weapon transition");
  followCamera.yawRatioRadians = 0;
  followCamera.releaseYawRadians = std::clamp(
      std::remainder(relativeYaw, 6.28318530717958647692),
      -1.57079632679489661923, 1.57079632679489661923);
  followCamera.verticalRatio = std::clamp(vertical, -1., 1.);
  followCamera.desiredYaw = 0;
  followCamera.desiredReleaseYaw = followCamera.releaseYawRadians;
  followCamera.desiredVertical = followCamera.verticalRatio;
  followCamera.smoothingInitialized = true;
}
FollowCameraPose SceneFollowPose() { return followPose; }
float SceneCameraYaw() { return yaw; }
float SceneCameraDistance() { return resolvedFollowDistance; }
void SceneFollowDistance(float d) {
  followDistance = std::clamp(d, 2500.f, 18000.f);
}
void SceneFollowHeading(float heading) {
  if (std::isfinite(heading)) {
    followHeading = heading;
    followCamera = {};
    pitch = 0;
    yaw = std::remainder(heading + 3.14159265358979323846f,
                         6.28318530717958647692f);
  }
}
void SceneCameraTurn(float angle) {
  if (std::isfinite(angle))
    yaw = std::remainder(yaw + angle, 6.28318530717958647692f);
}
SceneViewState SaveSceneView() {
  return {yaw,         pitch,
          distance,    followDistance,
          sceneFrames, SaveEnvironmentState(), followCamera, SaveWorldEffectsState(),
          followHeading, footShadowColor, footShadowFrame,SaveTrapEffectsPresentationGl(),
          SourceEffectWritesAlphaGl(0x800000),SaveRoomFilterGl(),RoomAtmosphere().transition};
}
void RestoreSceneView(const SceneViewState &state) {
  RestoreRoomFilterGl(state.roomFilter);
  if(state.lightTransition.owner==RoomAtmosphere().data.get())RoomAtmosphere().transition=state.lightTransition;
  for (auto value :
       {double(state.yaw), double(state.pitch), double(state.distance),
        double(state.followDistance), state.animationFrames, state.followHeading, state.footShadowFrame})
    if (!std::isfinite(value))
      throw std::runtime_error("Camera: invalid restore value");
  auto validatedFollow = state.followCamera;
  AdvanceFollowCamera(validatedFollow, 0, 0, 0);
  followCamera = validatedFollow;
  followHeading = state.followHeading;
  yaw = state.yaw;
  pitch = state.pitch;
  distance = state.distance;
  followDistance = state.followDistance;
  sceneFrames = state.animationFrames;
  footShadowColor=std::min(state.footShadowColor,255u);
  footShadowFrame=state.footShadowFrame;
  const auto currentEffects = SaveWorldEffectsState();
  // A diagnostic mesh is a new animation source. Keep its camera, but do
  // not replay the previous full room's potentially hours-old effect clock.
  // Same-source context recovery and failed room commits retain exact time.
  if (state.worldEffects && currentEffects &&
      state.worldEffects->signature != currentEffects->signature)
    sceneFrames = footShadowFrame = 0;
  RestoreEnvironmentState(state.environment);
  RestoreWorldEffectsState(state.worldEffects);
  if(RestoreTrapEffectsPresentationGl(state.trapPresentation))
    RestoreSourceGlowAlphaDraw2Gl(state.sourceAlphaDraw2);
}
void DrawSceneGl(int w, int h, float horizontal, float vertical, float zoom,
                 bool normalView, const SceneProbe *probe,
                 const LeonPreview *actor, double seconds) {
  const auto *event=probe && probe->event?probe->event:nullptr;
  const auto *scriptCamera=event?&event->camera():probe?probe->roomCamera:nullptr;
  const bool eventCamera=scriptCamera && scriptCamera->valid;
  // Inspection orbit and original player-local follow rig have separate state.
  auto now = std::chrono::steady_clock::now();
  float dt = std::clamp(
      float(seconds < 0 ? std::chrono::duration<double>(now - lastFrame).count()
                        : seconds),
      0.f, .05f);
  if (!std::isfinite(dt))
    throw std::runtime_error("Scene: invalid time");
  lastFrame = now;
  sceneFrames += dt * 30;
  if (!probe) {
    yaw += std::clamp(horizontal, -1.f, 1.f) * .9f * dt;
    pitch = std::clamp(pitch + std::clamp(vertical, -1.f, 1.f) * .72f * dt,
                       -1.3f, 1.3f);
    distance = std::clamp(distance + std::clamp(zoom, -1.f, 1.f) * 2.1f * dt,
                         1.8f, 12.f);
  } else if(eventCamera) {
    // The EVD owns the world view and playback clock; controller look input
    // cannot advance the ordinary player-local follow state during a cut.
  } else if (!actor || actor->action() == PlayerAction::Footwork) {
    if (!probe->followInputAdvanced)
      AdvanceFollowCamera(followCamera, horizontal, vertical, dt);
  } else {
    // Weapon controls own the look ratios. Freeze the knife's initial local
    // yaw; the handgun has already handed its yaw to Leon over four ticks.
    const auto action = actor->action();
    if (action == PlayerAction::Draw || action == PlayerAction::Aim ||
        action == PlayerAction::Fire || action == PlayerAction::Reload ||
        action == PlayerAction::Holster)
      followCamera.yawRatioRadians = followCamera.releaseYawRadians =
          followCamera.desiredYaw = followCamera.desiredReleaseYaw = 0;
    if (action == PlayerAction::Draw || action == PlayerAction::Aim ||
        action == PlayerAction::Fire || action == PlayerAction::Reload ||
        action == PlayerAction::Holster || action == PlayerAction::KnifeDraw ||
        action == PlayerAction::KnifeReady || action == PlayerAction::KnifeSlash ||
        action == PlayerAction::KnifeHolster)
      followCamera.verticalRatio = followCamera.desiredVertical = actor->aimRate();
  }
  float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch),
        sp = std::sin(pitch);
  // Row-major view; eye = distance*(sy*cp,sp,cy*cp), target at scene center.
  float v[16] = {cy,      0,  -sy,     0,         -sy * sp, cp, -cy * sp, 0,
                 sy * cp, sp, cy * cp, -distance, 0,        0,  0,        1};
  CollisionVec resolvedEye{};
  if (probe) {
    if(eventCamera)followPose=EventCameraPose(*scriptCamera);
    else {
    double aimYaw = 0, aimRate = 0;
    bool aiming = false;
    if (actor) {
      const auto action = actor->action();
      aiming = action == PlayerAction::Draw || action == PlayerAction::Aim ||
          action == PlayerAction::Fire || action == PlayerAction::Reload ||
          action == PlayerAction::Holster || action == PlayerAction::KnifeDraw ||
          action == PlayerAction::KnifeReady || action == PlayerAction::KnifeSlash ||
          action == PlayerAction::KnifeHolster;
      aimYaw = action == PlayerAction::Draw ? actor->drawCameraYawRemaining()
                                            : actor->waistYaw();
      // The knife does not rotate Leon during its draw, so keep the look
      // offset from before L1. Its authored hand movement never moves the eye.
      if (action == PlayerAction::KnifeDraw || action == PlayerAction::KnifeReady ||
          action == PlayerAction::KnifeSlash || action == PlayerAction::KnifeHolster)
        aimYaw += followCamera.yawRatioRadians + followCamera.releaseYawRadians;
      aimRate = actor->aimRate();
    }
    if (actor)
      followHeading = actor->renderHeading();
    followPose = PoseFollowCamera(followCamera, probe->collision, actor ? actor->renderFeet() : probe->feet,
                                  followHeading,
                                  aiming, aimYaw, aimRate,
                                  double(std::max(w, 1)) / std::max(h, 1));
    {
    struct ActionViewMemory {
      const LeonPreview *owner{};
      CollisionVec eye{},target{};
      unsigned ticks{};
      bool easing{};
    };
    static ActionViewMemory actionView;
    const auto attached=actor?(actor->scopeReady()?std::optional<ActionCameraPose>{actor->scopeCamera()}:actor->actionCamera()):std::optional<ActionCameraPose>{};
    if(attached) {
      auto actionPose=*attached;
      const float blend=actor->actionCameraBlend();
      if(blend<1.f) {
        const unsigned ticks=actor->dogTrapTicks();
        const bool start=actionView.owner!=actor || !actionView.easing || ticks<actionView.ticks;
        if(actionView.owner!=actor) {actionView.eye=followPose.eye;actionView.target=followPose.target;}
        const unsigned elapsed=start?1:ticks-actionView.ticks;
        const double amount=1.-std::pow(1.-double(blend),elapsed);
        for(unsigned k=0;k<3;++k) {
          actionView.eye[k]+=amount*(actionPose.eye[k]-actionView.eye[k]);
          actionView.target[k]+=amount*(actionPose.target[k]-actionView.target[k]);
        }
        actionView.owner=actor;actionView.ticks=ticks;actionView.easing=true;
        actionPose.eye=actionView.eye;actionPose.target=actionView.target;
      } else actionView={actor,actionPose.eye,actionPose.target,0,false};
      // Window motion cameras and obj13 ladder cameras own the action view.
      // Their eye/target/FOV follow the displayed pose instead of the ordinary
      // standing shoulder rig; authored action flags control wall repair.
      followPose.eye=actionPose.eye; followPose.target=actionPose.target;
      double length=0;
      for(unsigned k=0;k<3;++k) {
        followPose.look[k]=actionPose.target[k]-actionPose.eye[k];
        length+=followPose.look[k]*followPose.look[k];
      }
      length=std::sqrt(length);
      if(length<=.00001)throw std::runtime_error("Window: Camera view length is zero");
      for(auto &value:followPose.look)value/=length;
      auto right=CollisionVec{-followPose.look[2],0,followPose.look[0]};
      const auto horizontal=std::hypot(right[0],right[2]);
      if(horizontal>.00001)for(auto &value:right)value/=horizontal;
      else right=followPose.right; // Source retains old right for vertical look.
      const auto &look=followPose.look;
      CollisionVec up{right[1]*look[2]-right[2]*look[1],
          right[2]*look[0]-right[0]*look[2],right[0]*look[1]-right[1]*look[0]};
      const auto c=std::cos(actionPose.roll),s=std::sin(actionPose.roll);
      for(unsigned k=0;k<3;++k) {
        followPose.right[k]=right[k]*c+up[k]*s;
        followPose.up[k]=up[k]*c-right[k]*s;
      }
      followPose.fovDegrees=actionPose.fovDegrees;
      followPose.distance=std::hypot(actionPose.eye[0]-actor->feet()[0],actionPose.eye[2]-actor->feet()[2]);
    } else actionView={actor,followPose.eye,followPose.target,0,false};
    }
    }
    resolvedEye = followPose.eye;
    std::array<float, 3> eye{};
    for (unsigned k = 0; k < 3; ++k) {
      eye[k] = float(resolvedEye[k] - worldCenter[k]) * worldScale;
      v[k] = float(followPose.right[k]);
      v[4 + k] = float(followPose.up[k]);
      v[8 + k] = float(-followPose.look[k]);
    }
    v[3] = -(v[0] * eye[0] + v[1] * eye[1] + v[2] * eye[2]);
    v[7] = -(v[4] * eye[0] + v[5] * eye[1] + v[6] * eye[2]);
    v[11] = -(v[8] * eye[0] + v[9] * eye[1] + v[10] * eye[2]);
    if(!eventCamera) {
      resolvedFollowDistance = float(followPose.distance);
      yaw = float(std::atan2(followPose.look[0], followPose.look[2]) +
                3.14159265358979323846);
      pitch = float(-std::asin(std::clamp(followPose.look[1], -1., 1.)));
    }
  }
  // CameraMotion's projection is authored for4:3. The original cinematic
  // bars hide part of that picture; crop those rows before adapting the
  // visible picture to the display. Ordinary scripted room cameras retain
  // their whole frame, and mirror RTT keeps its own original projection.
  float aspect = eventCamera && !(probe && probe->screenAspect && !event)?1.3333334f:float(std::max(w, 1)) / std::max(h, 1), f = 1.7320508f;
  if (probe)
    f = float(1. / std::tan(followPose.fovDegrees * 3.14159265358979323846 / 360.));
  if (aspect < 1)
    f *= aspect;
  float p[16] = {f / aspect, 0, 0,          0,          0, f, 0,  0,
                 0,          0, -1.002002f, -.2002002f, 0, 0, -1, 0};
  if (probe) {
    float near = worldScale * 100,
          far = worldScale * 1000000;
    p[10] = -(far + near) / (far - near);
    p[11] = -2 * far * near / (far - near);
  }
  float column[16]{};
  for (unsigned i = 0; i < 4; ++i)
    for (unsigned j = 0; j < 4; ++j)
      for (unsigned k = 0; k < 4; ++k)
        column[j * 4 + i] += p[i * 4 + k] * v[k * 4 + j];
  if(event)CropOriginalCinematicFrame(column);
  auto &atmosphere=RoomAtmosphere();
  atmosphere.modelTextureCameraAspect=aspect;
  atmosphere.cinematicFrame=event!=nullptr;
  atmosphere.modelTextureViewport={512,event?OriginalCinematicPictureHeight:448};
  struct EventLightingScope {
    RoomAtmosphereGl &state;std::shared_ptr<const RoomLighting> room;RoomLightTransition transition;bool suspended;
    ~EventLightingScope() {if(state.data!=room)state.transition=transition;state.data=std::move(room);state.suspended=suspended;RoomLightBindings().valid=false;}
  } eventLighting{atmosphere,atmosphere.data,atmosphere.transition,atmosphere.suspended};
  if(event && event->lighting())atmosphere.data=event->lighting();
  atmosphere.suspended=event || (probe && probe->sourceSuspended);
  if(atmosphere.data) {
    const auto cut=event?0:probe && probe->roomLightCut?*probe->roomLightCut:
        actor?atmosphere.data->selectCut(actor->feet(),actor->heading(),atmosphere.transition.cut):0;
    atmosphere.transition.select(*atmosphere.data,cut,dt,event!=nullptr);
  }
  for(unsigned k=0;k<3;++k) {
    atmosphere.eye[k]=probe?float(resolvedEye[k]):worldCenter[k]+v[8+k]*distance/worldScale;
    atmosphere.right[k]=v[k]; atmosphere.up[k]=v[4+k]; atmosphere.look[k]=-v[8+k];
  }
  atmosphere.playerParts.clear();
  atmosphere.transientLights=!event && probe && probe->muzzle && probe->coreLighting
      ? probe->muzzle->lights(*probe->coreLighting) : std::vector<RoomLight>{};
  if(probe && probe->coreLighting) {
    const auto requests=event?WorldEffectLights(event->effectsData(),event->effectsState()):
        probe->trapEffects && probe->trapState?WorldEffectLights(*probe->trapEffects,*probe->trapState):std::vector<WorldEffectLight>{};
    for(const auto &request:requests) {
      const auto &bank=request.kind==1 && atmosphere.data?*atmosphere.data:*probe->coreLighting;
      if(request.cut>=bank.cuts.size() || !bank.present.at(request.cut) ||
          request.number>=bank.cuts[request.cut].lights.size())
        throw std::runtime_error("Esp11: original light source is missing");
      auto light=bank.cuts[request.cut].lights[request.number];
      light.parent=0;light.parentNo=0;light.flags=3|0x40;
      light.position=request.position;light.radius=request.radius;
      for(unsigned k=0;k<3;++k)light.color[k]=std::uint8_t(float(light.color[k])*request.color[k]);
      light.color[3]=std::uint8_t(float(light.color[3])*request.color[3]*.5f);
      atmosphere.transientLights.push_back(light);
    }
  }
  if(actor) {
    // Event LIT's actual ParentType1/no0 resolves gameplay pPL through
    // EmMgr, not the independent SetOm pl0000/PPl event body. Keep the
    // frozen entry pose for those source parent lights while hiding its mesh.
    const auto &parts=actor->renderJointMatrices();
    const auto placement=actor->renderPlacement();
    atmosphere.playerParts.resize(parts.size());
    for(std::size_t i=0;i<parts.size();++i) {
      auto &world=atmosphere.playerParts[i];
      for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c) {
        world[r*4+c]=c==3?placement[r*4+3]:0;
        for(unsigned k=0;k<3;++k)world[r*4+c]+=placement[r*4+k]*parts[i][k*4+c];
      }
    }
  }
  const auto eventHidden=event?event->hiddenRoomObjects():std::vector<unsigned>{};
  auto glowRequests=!event && !normalView && probe && probe->trapEffects && probe->trapState
      ? WorldEffectGlowRequests(*probe->trapEffects,*probe->trapState) : std::vector<WorldEffectGlowRequest>{};
  if(!event && !normalView && probe && probe->villageBottles && !probe->sourceSuspended) {
    const auto &b=*probe->villageBottles;
    for(const auto &q:WorldEffectGlowRequests(b.coreData(),b.coreState()))glowRequests.push_back(q);
    for(const auto &q:WorldEffectGlowRequests(b.trailData(),b.trailState()))glowRequests.push_back(q);
  }
  BeginSourceGlowFrameGl(glowRequests);
  DrawEnvironmentTargets(sceneFrames);
  glViewport(0, 0, w, h);
  glDisable(GL_SCISSOR_TEST);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  if(atmosphere.data) {
    // bio4_AddBgColor uses black when the current LIT disables fog, even
    // if its unused color bytes contain a nonzero value.
    const auto &fog=*CurrentRoomFog();
    const auto &c=fog.color;
    glClearColor(fog.type ? c[0]/255.f : 0.f,
                 fog.type ? c[1]/255.f : 0.f,
                 fog.type ? c[2]/255.f : 0.f,0);
  } else glClearColor(.035f, .055f, .075f, 0);
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  // gameInit/GX source frame alpha starts at0. Normal geometry preserves
  // it; source effect Tool flags publish the Filter03 glow mask explicitly.
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
  if (program && count) {
    glUseProgram(program);
    BindRoomFog(program);
    textureLod.bind(textures);
    glUniform3fv(glGetUniformLocation(program,"roomCenter"),1,worldCenter.data());
    glUniform1f(glGetUniformLocation(program,"roomScale"),worldScale);
    glUniform1i(glGetUniformLocation(program, "normalView"),
                normalView ? 1 : 0);
    glBindVertexArray(vao);
    glUniformMatrix4fv(glGetUniformLocation(program, "camera"), 1, GL_FALSE,
                       column);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(program, "image"), 0);
    // Reproduce the supported original OT model categories. Whole objects keep
    // their display-list order; world/model buckets depend on camera depth.
    std::array<double, 3> eye{}, look{-sy * cp, -sp, -cy * cp};
    for (unsigned k = 0; k < 3; ++k)
      eye[k] = worldCenter[k];
    if (probe) {
      eye = resolvedEye;
      look = followPose.look;
    } else {
      for (unsigned k = 0; k < 3; ++k)
        eye[k] -= look[k] * distance / worldScale;
    }
    GLuint screenImage = 0;
    const auto order = SceneDrawOrder(draws, eye, look);
    const auto *objects = probe ? probe->objects : nullptr;
    std::array<bool, 64> brokenSlots{};
    if (objects)
      for (const auto &state : objects->states())
        if (state.slot < brokenSlots.size())
          brokenSlots[state.slot] = state.broken;
    auto drawOne = [&](std::size_t index,
                       const DestructionInstance *fragment = nullptr) {
      const auto &draw = draws[index];
      if(draw.roomBlock>=0 && probe && !(probe->roomBlocks&(1u<<draw.roomBlock)))return;
      if(event && (!event->drawsRoomBackground() ||
          std::find(eventHidden.begin(),eventHidden.end(),draw.work)!=eventHidden.end()))return;
      if(probe && probe->r100Secondary && !probe->r100Secondary->roomObjectVisible(draw.work))return;
      const auto *interactions = probe ? probe->interactions : nullptr;
      if(event && event->data().name()=="r101s30" && (draw.roomObjectSlot==5 || draw.roomObjectSlot==6 || draw.roomObjectSlot==7))return;
      if (interactions ? !interactions->visible(draw.roomObjectSlot,
                            draw.roomObjectVariant, draw.roomLockParent)
                       : draw.roomObjectVariant == 1)
        return;
      // Local fragment geometry lives in the same immutable VBO, but has no
      // world placement until the source Efm05 simulation supplies one.
      if (draw.destructionMesh >= 0 && !fragment)
        return;
      if (fragment && draw.destructionMesh != int(fragment->mesh))
        return;
      if (draw.roomObjectSlot >= 0 &&
          std::size_t(draw.roomObjectSlot) < brokenSlots.size() &&
          brokenSlots[draw.roomObjectSlot])
        return;
      if (draw.roomItemIndex >= 0) {
        if (!objects ||
            std::size_t(draw.roomItemIndex) >= objects->states().size())
          return;
        const auto &state = objects->states()[draw.roomItemIndex];
        if (!objects->itemModelVisible(std::size_t(draw.roomItemIndex)) ||
            draw.roomItemId != int(state.itemId))
          return;
      }
      if (draw.roomPickupIndex >= 0 &&
          (!probe || !probe->pickups || !probe->pickups->visible(draw.roomPickupIndex)))
        return;
      const WaterSurface *water =
          draw.water >= 0 ? &environment.surfaces.at(draw.water) : nullptr;
      const GLuint generated = water && water->target >= 0
                                   ? EnvironmentTexture(unsigned(water->target))
                                   : 0;
      const bool liquid = generated != 0;
      const bool background = draw.sky >= 0;
      auto delta = fragment ? fragment->matrix
                   : background ? EnvironmentSkyMatrix(unsigned(draw.sky))
                                : Matrix34{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
      if (!fragment && !background) {
        const auto object = interactions ? interactions->delta(draw.roomObjectSlot,draw.roomLockParent) : std::optional<Matrix34>{};
        if(probe && probe->itemEvents && draw.roomObjectSlot<0 && draw.destructionMesh<0 && draw.roomPickupIndex<0 && draw.roomItemIndex<0)
          if(const auto hinge=probe->itemEvents->delta(draw.work))delta=*hinge;
        if(object) delta = *object;
        else if(probe && probe->ladders)
          if(const auto ladder=probe->ladders->delta(draw.roomObjectSlot)) delta=*ladder;
        if(probe && probe->coreScenery && draw.smdIndex>=0 && !probe->coreScenery(draw,delta))return;
      }
      const auto litPosition=TransformPoint(delta,draw.sortCenter);
      auto lightVolume=draw.lightVolume;lightVolume.center=TransformPoint(delta,lightVolume.center);
      for(unsigned axis=0;axis<3;++axis) {
        std::array<float,3> basis{};float scale=0;
        for(unsigned k=0;k<3;++k) {
          for(unsigned j=0;j<3;++j)basis[k]+=delta[k*4+j]*lightVolume.axes[axis*3+j];
          scale+=basis[k]*basis[k];
        }
        scale=std::sqrt(scale);lightVolume.size[axis]*=scale;
        for(unsigned k=0;k<3;++k)lightVolume.axes[axis*3+k]=scale?basis[k]/scale:0;
      }
      if(draw.authored && draw.orderType!=6 && !fragment && atmosphere.data) {
        const auto *cut=CurrentRoomLightCut();
        float radius=0;for(float extent:lightVolume.size)radius+=extent*extent;
        // The radius is the half-diagonal of this transformed BIN bound.
        // Its matching center must be used even for orderType0: a tree's
        // part0 can sit at its foot, far below the center of its crown.
        // A layer without an authored BIN volume retains its source sort
        // center. A zero/default volume is not a bound at the world origin.
        const auto &boundCenter=radius>0?lightVolume.center:litPosition;
        if(!RoomModelVisible(boundCenter,std::sqrt(radius),atmosphere.eye,
            atmosphere.right,atmosphere.up,atmosphere.look,
            float(followPose.fovDegrees*3.14159265358979323846/180),aspect,
            RoomVisibilityFar(*CurrentRoomFog(),cut->farRatio)))return;
      }
      const unsigned lightEnable = draw.roomItemIndex >= 0 || draw.roomPickupIndex >= 0 ? 0x20 : 0x10;
      BindRoomLights(program,lightEnable,draw.lightMask,litPosition,draw.lightSize,draw.noDiffuseLight,8,256,false,
          draw.authored?&lightVolume:nullptr,draw.scrollLightAttribute);
      // SmxSetFlag16 sets be_flag8000: LightSetModel uses zero diffuse
      // lights but retains room/vertex ambient. It is not ModelRender's
      // separate be_flag20000 material-only path.
      glUniform1i(glGetUniformLocation(program,"unlit"),0);
      auto normal = NormalMatrix(delta);
      // Convert a world-space affine delta into this VBO's normalized space.
      float model[16]{};
      model[15] = 1;
      for (unsigned r = 0; r < 3; ++r) {
        for (unsigned c = 0; c < 3; ++c)
          model[c * 4 + r] = delta[r * 4 + c];
        double shift = delta[r * 4 + 3] - worldCenter[r];
        for (unsigned c = 0; c < 3; ++c)
          shift += double(delta[r * 4 + c]) * worldCenter[c];
        model[12 + r] = float(shift * worldScale);
      }
      float normals[9]{};
      for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = 0; c < 3; ++c)
          normals[c * 3 + r] = normal[r * 3 + c];
      glUniformMatrix4fv(glGetUniformLocation(program, "modelDelta"), 1,
                         GL_FALSE, model);
      glUniformMatrix3fv(glGetUniformLocation(program, "normalDelta"), 1,
                         GL_FALSE, normals);
      glUniform1i(glGetUniformLocation(program, "background"),
                  background ? 1 : 0);
      glUniform1i(glGetUniformLocation(program, "liquid"), liquid ? 1 : 0);
      glUniform1i(glGetUniformLocation(program, "textureAlpha"),
                  liquid && water->textureAlpha ? 1 : 0);
      glUniform1f(glGetUniformLocation(program, "materialAlpha"),
                  fragment ? fragment->color[3]
                    : liquid ? water->colorAlpha / 255.f : 1.f);
      glUniform1i(
          glGetUniformLocation(program, "refractOn"),
          liquid && water->shader == 2 && screenImage && !normalView ? 1 : 0);
      glUniform1f(glGetUniformLocation(program, "refractPower"),
                  liquid ? water->refractPower / 512.f : 0.f);
      glUniform1f(glGetUniformLocation(program, "refractRatio"),
                  liquid ? water->refractRatio / 255.f : 1.f);
      glUniform1i(glGetUniformLocation(program, "sceneImage"), 2);
      const bool specular=draw.specularEnabled&&draw.specularTexture>=0&&(!liquid||water->shader!=2);
      glUniform1i(glGetUniformLocation(program,"specularType"),specular?int(draw.specularType):-1);
      glUniform4f(glGetUniformLocation(program,"specularParameters"),draw.specularRGB[0]/255.f,draw.specularRGB[1]/255.f,draw.specularRGB[2]/255.f,draw.specularScale);
      glUniform3fv(glGetUniformLocation(program,"roomSpecularNormalScale"),1,draw.specularNormalScale.data());
      glUniform1i(glGetUniformLocation(program,"specularImage"),3);glActiveTexture(GL_TEXTURE3);glBindSampler(3,0);
      glBindTexture(GL_TEXTURE_2D,specular?textures.at(draw.specularTexture):0);
      glActiveTexture(GL_TEXTURE0);
      glUniform2f(glGetUniformLocation(program, "viewport"), float(w),
                  float(h));
      const auto zMode = fragment ? fragment->zMode : draw.zMode;
      glDepthFunc(zMode == 2 ? GL_ALWAYS : GL_LEQUAL);
      // obj05 selects the world-depth translucent OT when Col_a < 250.
      // This GLES pass keeps its depth test and suppresses writes while
      // fading; explicit source zMode is otherwise retained.
      const bool fading = fragment && fragment->color[3] < 250.f / 255.f;
      glDepthMask(zMode == 0 && !fading ? GL_TRUE : GL_FALSE);
      glUniform2f(glGetUniformLocation(program, "uvOffset"),
                  float(std::remainder(sceneFrames * draw.uvSpeed[0], 1.)),
                  float(std::remainder(sceneFrames * draw.uvSpeed[1], 1.)));
      glUniform3f(glGetUniformLocation(program, "materialRGB"),
                  draw.materialRGB[0] / 255.f * (fragment ? fragment->color[0] : 1.f),
                  draw.materialRGB[1] / 255.f * (fragment ? fragment->color[1] : 1.f),
                  draw.materialRGB[2] / 255.f * (fragment ? fragment->color[2] : 1.f));
      glUniform3f(glGetUniformLocation(program,"roomMaterialRGB"),
                  draw.materialRGB[0]/255.f*(fragment?fragment->color[0]:1.f),
                  draw.materialRGB[1]/255.f*(fragment?fragment->color[1]:1.f),
                  draw.materialRGB[2]/255.f*(fragment?fragment->color[2]:1.f));
      bool ready =
          draw.texture >= 0 && std::size_t(draw.texture) < textures.size();
      glBindTexture(GL_TEXTURE_2D, liquid  ? generated
                                   : ready ? textures[draw.texture]
                                           : 0);
      glUniform1i(glGetUniformLocation(program, "textured"),
                  ready || liquid ? 1 : 0);
      glUniform1i(glGetUniformLocation(program, "vertexColors"),
                  draw.vertexColors ? 1 : 0);
      bool alphaReady = !liquid && ready && draw.alphaTest &&
                        draw.alphaTexture >= 0 &&
                        std::size_t(draw.alphaTexture) < textures.size();
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D,
                    alphaReady ? textures[draw.alphaTexture] : 0);
      glUniform1i(glGetUniformLocation(program, "alphaImage"), 1);
      glUniform1i(glGetUniformLocation(program, "alphaTest"),
                  alphaReady ? 1 : 0);
      glUniform1f(glGetUniformLocation(program, "alphaReference"),
                  float(draw.alphaReference));
      unsigned blendMode = fragment ? fragment->blend : draw.blendMode;
      if (blendMode == 5)
        blendMode = 0;
      if (blendMode < 4) {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        GLenum source[4]{GL_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE, GL_DST_COLOR};
        GLenum destination[4]{GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE};
        glBlendFunc(source[blendMode], destination[blendMode]);
      } else
        glDisable(GL_BLEND);
      glActiveTexture(GL_TEXTURE0);
      glFrontFace(GL_CCW);
      if(draw.cullMode==2)glDisable(GL_CULL_FACE);
      else {glEnable(GL_CULL_FACE);glCullFace(draw.cullMode==0?GL_BACK:GL_FRONT);}
      glDrawArrays(GL_TRIANGLES, static_cast<GLint>(draw.first),
                   static_cast<GLsizei>(draw.count));
    };
    WorldEffectCamera effectCamera;
    for (unsigned k = 0; k < 3; ++k) {
      effectCamera.right[k] = v[k];
      effectCamera.up[k] = v[4 + k];
      effectCamera.forward[k] = -v[8 + k];
      effectCamera.eye[k] = probe ? float(resolvedEye[k])
          : worldCenter[k] + v[8 + k] * distance / worldScale;
    }
    if(event && !normalView)
      DrawEventEffectsGl(column,worldCenter,worldScale,
          WorldEffectQuads(event->effectsData(),event->effectsState(),effectCamera),true);
    const auto roomAreaQuads=!normalView && probe && probe->roomAreaEffects
        ? probe->roomAreaEffects->quads(effectCamera) : std::vector<RoomAreaEffectQuad>{};
    DrawRoomAreaEffectsGl(column,worldCenter,worldScale,roomAreaQuads,true);
    glUseProgram(program);glBindVertexArray(vao);
    for (auto index : order)
      if (draws[index].sky >= 0)
        drawOne(index);
    for (auto index : order)
      if (draws[index].sky < 0 && draws[index].water < 0 && draws[index].orderType != 1)
        drawOne(index);
    if (probe && probe->destruction) {
      auto fragments = probe->destruction->instances();
      const auto depth = [&](const auto &fragment) {
        double value = 0;
        for (unsigned k = 0; k < 3; ++k)
          value += (fragment.matrix[k * 4 + 3] - eye[k]) * look[k];
        return value;
      };
      std::stable_sort(fragments.begin(), fragments.end(),
                       [&](const auto &a, const auto &b) { return depth(a) > depth(b); });
      for (const auto &fragment : fragments) {
        if (fragment.mesh >= destructionDraws.size())
          continue;
        for (const auto index : destructionDraws[fragment.mesh])
          drawOne(index, &fragment);
      }
    }
    // An unmatched water pass retains exactly its authored base material.
    for (auto index : order)
      if (draws[index].water >= 0 &&
          environment.surfaces.at(draws[index].water).target < 0)
        drawOne(index);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    const bool worldAutofocus=!normalView && !event && probe && probe->gameplayDepthOfField && userDepthOfField;
    const auto drawPlayer=[&](){
    // Keep body/weapon at every distance. Only the original head attachments
    // fade when their animated bound meets the resolved close-camera site.
    if (actor && !actor->scopeReady() && !event && (!probe || !probe->hidePlayer)) {
      DrawActorGl(column, worldCenter, worldScale, *actor, normalView,
                   probe ? &resolvedEye : nullptr);
      if(probe && probe->collision && atmosphere.data && !normalView) {
        const auto ground=probe->collision->ground(actor->feet(),600,100000);
        const auto action=actor->action();
        const bool enabled=ground && ground->normal[1]>=.8 &&
            resolvedEye[1]>=actor->feet()[1] && action!=PlayerAction::LadderUp &&
            action!=PlayerAction::LadderDown && action!=PlayerAction::JumpDown;
        // pl_class.cpp::shadowCtrl adds/subtracts 16 each original 30 Hz tick.
        // dt=0 (pause) changes neither fade nor pose. SaveSceneView preserves
        // this owner across EGL context recreation with the effect clock.
        const auto current=std::floor(sceneFrames);
        const auto ticks=unsigned(std::clamp(current-footShadowFrame,0.,16.));
        for(unsigned i=0;i<ticks;++i)
          footShadowColor=enabled?(footShadowColor>15?footShadowColor-16:0):std::min(footShadowColor+16,255u);
        footShadowFrame=current;
        if(ground) DrawFootShadowGl(column,worldCenter,worldScale,
            BuildPlayerFootShadows(*CurrentRoomLightCut(),atmosphere.playerParts,
                actor->feet(),float(ground->point[1]),footShadowColor));
      }
      if (actor->weapon() && actor->weaponVisible())
        DrawWeaponGl(column,worldCenter,worldScale,*actor,normalView);
      if(probe && probe->muzzle && !normalView)
        DrawMuzzleEffectsGl(column,worldCenter,worldScale,probe->muzzle->quads(effectCamera));
      glUseProgram(program);
    }    };
    if(!worldAutofocus)drawPlayer();
    if(event) {
      DrawEventEffectModelsGl(column,worldCenter,worldScale,*event,normalView);
      DrawEventActorsGl(column,worldCenter,worldScale,*event,normalView);
      glUseProgram(program);
    }
    if(probe && probe->extraActors) {
      probe->extraActors(column,worldCenter,worldScale,normalView);
      glUseProgram(program);
    }
    if(probe && probe->r100Secondary)for(const auto &a:probe->r100Secondary->actors()) {
      if(!a->visible() || !a->rig())continue;
      if((event || probe->sourceSuspended || probe->roomCamera || probe->houseEnemy) &&
          !probe->r100Secondary->actorVisibleDuringSuspend(*a))continue;
      GenericActorStyle style;style.color=a->materialColor();style.zMode=a->depthMode();
      style.lightVolume=a->lightVolume();const auto light=a->lightRootPosition();
      DrawGenericActorGl(a.get(),column,worldCenter,worldScale,a->rig()->bones(),a->rig()->bindCenters(),
          a->worldMatrix(),normalView,a->lightMask(),&light,a->lightSize(),&style);
      glUseProgram(program);
    }
    if(probe && probe->villageBottles && !probe->sourceSuspended) {
      const auto &model=probe->villageBottles->model();
      for(const auto &placement:probe->villageBottles->placements())DrawGenericActorGl(&model,column,worldCenter,worldScale,model.rig()->bones(),model.rig()->bindCenters(),placement,normalView,model.lightMask());
    }
    if(probe && probe->projectiles && !probe->sourceSuspended) {
      for(const auto &draw:probe->projectiles->draws()) {
        const auto &model=*draw.model;
        DrawGenericActorGl(&model,column,worldCenter,worldScale,draw.rig->bones(),draw.rig->bindCenters(),draw.placement,normalView,model.lightMask());
      }
      if(!normalView)DrawProjectileEffectsGl(column,worldCenter,worldScale,
          WorldEffectQuads(probe->projectiles->effectsData(),probe->projectiles->effectsState(),effectCamera));
      glUseProgram(program);
    }
    if(probe && probe->radioActor && actor) {
      const auto &radio=*probe->radioActor;
      DrawGenericActorGl(&radio,column,worldCenter,worldScale,radio.rig()->bones(),radio.rig()->bindCenters(),probe->radioMatrix,normalView,radio.lightMask());
    }
    if(probe && probe->radioHand && probe->r100Secondary && actor) {
      const auto &radio=probe->r100Secondary->radioActor();
      DrawGenericActorGl(&radio,column,worldCenter,worldScale,radio.rig()->bones(),radio.rig()->bindCenters(),
          probe->r100Secondary->radioMatrix(*actor),normalView,radio.lightMask());
      glUseProgram(program);
    }
    if(probe && probe->houseEnemy) {
      DrawEnemyGl(column,worldCenter,worldScale,probe->houseEnemy->actor(),probe->houseEnemy->placement(),normalView);glUseProgram(program);
    }
    if (probe && probe->enemy && (!event || event->data().name()=="r100s20") && probe->enemy->visible() &&
        (!probe->sourceSuspended || (probe->r100Secondary && probe->r100Secondary->corpseNoSuspend()))) {
      DrawEnemyGl(column, worldCenter, worldScale, probe->enemy->actor(),
                  probe->enemy->placement(), normalView);
      glUseProgram(program);
    }
    if(probe && probe->enemies)for(const auto &entry:probe->enemies->actors()) {
      if(entry.get()==probe->enemy || !entry->visible())continue;
      // r100_em_set explicitly marks the seven s20 actors NoSuspend.
      // Ordinary room scripts obey ModelTrans's authored NoSuspend gate.
      if(event && event->data().name()!="r100s20")continue;
      if(probe->sourceSuspended && std::find(probe->noSuspendEnemies.begin(),probe->noSuspendEnemies.end(),entry->listIndex())==probe->noSuspendEnemies.end())continue;
      DrawEnemyGl(column,worldCenter,worldScale,entry->actor(),entry->placement(),normalView);glUseProgram(program);
    }
    if (probe && probe->animals && !event && !probe->sourceSuspended)
      DrawAnimalsGl(column, worldCenter, worldScale, *probe->animals, normalView);

    if(probe && probe->forest && !event && !probe->sourceSuspended)for(const auto &animal:probe->forest->animals()) {
      if(!animal.visible())continue;
      const auto wing=animal.wing();
      DrawGenericActorGl(animal.gpuOwner(wing),column,worldCenter,worldScale,
          animal.rig().bones(),animal.rig().bindCenters(),
          animal.placement(),normalView,2);
    }
    if(probe && probe->dogTraps && !event && !probe->sourceSuspended)for(const auto &animal:probe->dogTraps->actors()) {
      if(!animal->visible())continue;
      if(!animal->rig())continue;
      GenericActorStyle style;style.color=animal->materialColor();style.zMode=animal->depthMode();
      DrawGenericActorGl(animal.get(),column,worldCenter,worldScale,
          animal->rig()->bones(),animal->rig()->bindCenters(),
          animal->worldMatrix(),normalView,animal->lightMask(),nullptr,1000,&style);
    }
    if(probe && probe->villageAnimals && !event && !probe->sourceSuspended)for(const auto &animal:probe->villageAnimals->animals()) {
      if(!animal->visible())continue;
      const auto &model=animal->model();GenericActorStyle style;style.color=model.materialColor();
      style.color[0]*=animal->deathShade();style.color[1]*=animal->deathShade();style.color[2]*=animal->deathShade();
      // EM26/EM28 LightInfo.init2 uses part3, with their own authored box.
      const auto part=WeaponAttachmentMatrix(model.rig()->bones().at(2),model.worldMatrix());
      RoomLightVolume volume;volume.type=0;volume.center=TransformPoint(part,{0,0,0});
      volume.size=animal->source().id==0x26?std::array<float,3>{2000,2000,2000}:std::array<float,3>{500,500,0};
      for(unsigned axis=0;axis<3;++axis)for(unsigned k=0;k<3;++k)volume.axes[axis*3+k]=part[k*4+axis];
      style.lightVolume=volume;
      DrawGenericActorGl(&model,column,worldCenter,worldScale,model.rig()->bones(),model.rig()->bindCenters(),
          model.worldMatrix(),normalView,2,&volume.center,animal->source().id==0x26?2000:500,&style);
    }
    if(!normalView && probe && probe->houseEnemy)
      DrawRoomEnemyShadowGl(column,worldCenter,worldScale,*probe->houseEnemy);
    // EspTrans and ModelTrans share OT0x11 for world-position sprites and
    // glass. Draw the distant forest before the pane, rather than replacing
    // the already blended glass with leaves in a later blanket effect pass.
    // Screen effects retain their later OT0x12; fixed effects use OT0x10.
    struct WorldEntry { unsigned bucket; std::size_t index; bool sprite; };
    std::vector<WorldEntry> worldOrder;
    std::vector<RoomAreaEffectQuad> fixedEffects,screenEffects,pendingSprites;
    const auto bucket=[&](const std::array<float,3> &position) {
      double depth=0;
      for(unsigned k=0;k<3;++k)depth+=(position[k]-eye[k])*look[k];
      return unsigned(std::clamp(depth*.05,0.,1023.));
    };
    for(auto index:order)
      if(draws[index].sky<0 && draws[index].water<0 && draws[index].orderType==1)
        worldOrder.push_back({bucket(draws[index].sortCenter),index,false});
    for(std::size_t i=0;i<roomAreaQuads.size();++i) {
      const auto &wrapped=roomAreaQuads[i];const auto &q=wrapped.quad;
      if(q.flags&0x400000)continue; // Already drawn in the far pass.
      if(q.screen || (q.flags&0x1000))screenEffects.push_back(wrapped);
      else if(q.flags&0xc00)fixedEffects.push_back(wrapped);
      // q.depth retains the source sprite origin; ANM center offsets move
      // its quad vertices without changing AddOtWorldPos's sorting point.
      else worldOrder.push_back({unsigned(std::clamp(double(q.depth)*.05,0.,1023.)),i,true});
    }
    DrawRoomAreaEffectsGl(column,worldCenter,worldScale,fixedEffects);
    std::stable_sort(worldOrder.begin(),worldOrder.end(),[](const auto &a,const auto &b){return a.bucket>b.bucket;});
    const auto flushSprites=[&] {
      if(pendingSprites.empty())return;
      DrawRoomAreaEffectsGl(column,worldCenter,worldScale,pendingSprites);
      pendingSprites.clear();
    };
    for(const auto &entry:worldOrder) {
      if(entry.sprite)pendingSprites.push_back(roomAreaQuads[entry.index]);
      else {
        flushSprites();
        glUseProgram(program);glBindVertexArray(vao);BindRoomFog(program);
        glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);glBindSampler(1,0);
        drawOne(entry.index);
      }
    }
    flushSprites();
    if(event)DrawEventEffectModelsGl(column,worldCenter,worldScale,*event,normalView,true);
    screenImage = CaptureEnvironmentScreen(w, h);
    glUseProgram(program);
    glBindVertexArray(vao);
    for (auto index : order)
      if (draws[index].water >= 0 &&
          environment.surfaces.at(draws[index].water).target >= 0)
        drawOne(index);
    std::array<float,3> waterTarget=worldCenter;
    if(probe)for(unsigned k=0;k<3;++k)waterTarget[k]=float(followPose.target[k]);
    DrawEnvironmentGridWater(column,worldCenter,worldScale,waterTarget,w,h);
    DrawWorldEffectsGl(column, worldCenter, worldScale, effectCamera, sceneFrames);
    if(probe && probe->villageBottles && !probe->sourceSuspended)DrawVillageBottleEffectsGl(column,worldCenter,worldScale,
        WorldEffectQuads(probe->villageBottles->coreData(),probe->villageBottles->coreState(),effectCamera),
        WorldEffectQuads(probe->villageBottles->trailData(),probe->villageBottles->trailState(),effectCamera));
    DrawRoomAreaEffectsGl(column,worldCenter,worldScale,screenEffects);
    if(!event && !normalView && probe && !probe->sourceSuspended && probe->roomHitData && probe->roomHitState)
      DrawRoomHitEffectsGl(column,worldCenter,worldScale,
          WorldEffectQuads(*probe->roomHitData,*probe->roomHitState,effectCamera));
    if(!normalView && probe && probe->roomAreaEffects)
      DrawRoomEffectClothGl(column,worldCenter,worldScale,probe->roomAreaEffects->clothMeshes());
    if(!event && probe && probe->itemEffects)
      DrawItemEffectsGl(column,worldCenter,worldScale,effectCamera,*probe->itemEffects,normalView);
    if(event && !normalView)
      DrawEventEffectsGl(column,worldCenter,worldScale,
          WorldEffectQuads(event->effectsData(),event->effectsState(),effectCamera));
    if(!event && probe && probe->enemyEffectsData && probe->enemyEffects)
      DrawEnemyEffectsGl(column,worldCenter,worldScale,
          WorldEffectQuads(*probe->enemyEffectsData,probe->enemyEffects->state(),effectCamera));
    if(!event && !normalView && probe && probe->trapEffects && probe->trapState) {
      DrawTrapEffectsGl(column,worldCenter,worldScale,
          WorldEffectQuads(*probe->trapEffects,*probe->trapState,effectCamera));
      DrawTrapEffectLinesGl(column,worldCenter,worldScale,
          WorldEffectLines(*probe->trapEffects,*probe->trapState));
      DrawTrapEffectLensesGl(column,worldCenter,worldScale,effectCamera,
          WorldEffectLenses(*probe->trapEffects,*probe->trapState));
    }
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LEQUAL);
    // Item effects may draw an original BIN through GenericActorGl. That
    // renderer leaves its skinning program current. Uniform locations below
    // belong to the world program: applying them to the actor's BoneMatrices
    // produced GL_INVALID_OPERATION as soon as an enemy dropped a model item.
    glUseProgram(program);
    glBindVertexArray(vao);
    const float identity4[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float identity3[9]{1, 0, 0, 0, 1, 0, 0, 0, 1};
    glUniformMatrix4fv(glGetUniformLocation(program, "modelDelta"), 1, GL_FALSE,
                       identity4);
    glUniformMatrix3fv(glGetUniformLocation(program, "normalDelta"), 1,
                       GL_FALSE, identity3);
    glUniform1i(glGetUniformLocation(program, "background"), 0);
    glUniform1i(glGetUniformLocation(program, "liquid"), 0);
    glUniform1i(glGetUniformLocation(program, "refractOn"), 0);
    glUniform1i(glGetUniformLocation(program,"unlit"),0);
    glUniform1f(glGetUniformLocation(program, "materialAlpha"), 1);
    if (probe && !event) {
      std::vector<float> data;
      auto vertex = [&](std::array<double, 3> pos, std::array<float, 4> color,
                        std::array<float, 2> uv = {0, 0}) {
        for (int k = 0; k < 3; ++k)
          data.push_back(float(pos[k] - worldCenter[k]) * worldScale);
        data.insert(data.end(), uv.begin(), uv.end());
        data.insert(data.end(), color.begin(), color.end());
        data.insert(data.end(), {0, 1, 0});
      };
      // Small ground-contact patch, projected onto the queried triangle's
      // plane. Keep original floor textures visible outside the footprint.
      auto a = probe->groundTriangle[0], b = probe->groundTriangle[1],
           c = probe->groundTriangle[2];
      double nx = (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]);
      double ny = (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]);
      double nz = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
      auto floorPoint = [&](double angle) {
        auto p = probe->feet;
        p[0] += std::cos(angle) * probe->radius * 1.1;
        p[2] += std::sin(angle) * probe->radius * 1.1;
        if (std::abs(ny) > 1e-9)
          p[1] = a[1] - (nx * (p[0] - a[0]) + nz * (p[2] - a[2])) / ny;
        p[1] += 3;
        return p;
      };
      auto foot = probe->feet;
      foot[1] += 3;
      constexpr int groundCount = 36;
      for (int i = 0; i < 12; ++i)
        for (auto p : {foot, floorPoint(i * 6.283185307 / 12),
                       floorPoint((i + 1) * 6.283185307 / 12)})
          vertex(p, {1, .8f, .1f, .65f});
      std::array<float, 4> color = probe->blocked
                                       ? std::array<float, 4>{1, .3f, .1f, 1}
                                       : std::array<float, 4>{1, .9f, .2f, 1};
      struct Ring {
        double y, r;
      };
      std::vector<Ring> rings;
      constexpr double pi = 3.141592653589793;
      for (int i = 0; i <= 3; ++i) {
        double a = -pi / 2 + i * pi / 6;
        rings.push_back({probe->radius + std::sin(a) * probe->radius,
                         std::cos(a) * probe->radius});
      }
      for (int i = 0; i <= 3; ++i) {
        double a = i * pi / 6;
        rings.push_back(
            {probe->height - probe->radius + std::sin(a) * probe->radius,
             std::cos(a) * probe->radius});
      }
      auto point = [&](int ring, int segment) {
        auto p = probe->feet;
        double angle = segment * 2 * pi / 12;
        p[0] += rings[ring].r * std::cos(angle);
        p[1] += rings[ring].y;
        p[2] += rings[ring].r * std::sin(angle);
        return p;
      };
      for (int ring = 0; ring + 1 < int(rings.size()); ++ring)
        for (int i = 0; i < 12; ++i) {
          auto a = point(ring, i), b = point(ring, i + 1),
               c = point(ring + 1, i);
          for (auto p : {a, b, a, c})
            vertex(p, color);
        }
      const auto rayFirst = data.size() / 12;
      if (probe->showAimRay) {
        const auto color = laserSight ? laserSight->lineColor
                                      : std::array<float, 4>{1, 0, 0, 1};
        auto endColor = color;
        const double length = std::hypot(
            std::hypot(probe->rayEnd[0] - probe->rayStart[0],
                       probe->rayEnd[1] - probe->rayStart[1]),
            probe->rayEnd[2] - probe->rayStart[2]);
        const float fade = laserSight ? float(std::max(0., 1 - length / laserSight->maxLineLength)) : 1;
        for (auto &component : endColor) component *= fade;
        vertex(probe->rayStart, color);
        vertex(probe->rayEnd, endColor);
      }
      const auto pointFirst = data.size() / 12;
      if (probe->showAimPoint && laserSight && laserPointTexture) {
        const auto &point = probe->aimPoint;
        const auto distance = std::hypot(std::hypot(point[0] - resolvedEye[0],
                                                    point[1] - resolvedEye[1]),
                                         point[2] - resolvedEye[2]);
        const auto factor = std::min(distance / 8000 + .8, 3.);
        constexpr unsigned corners[]{0, 1, 2, 0, 2, 3};
        constexpr std::array<float, 2> uv[]{ {0,1}, {1,1}, {1,0}, {0,0} };
        for (auto corner : corners) {
          auto position = point;
          const auto x = (uv[corner][0] - .5) * laserSight->pointSize[0] * factor;
          const auto y = (.5 - uv[corner][1]) * laserSight->pointSize[1] * factor;
          for (unsigned axis = 0; axis < 3; ++axis)
            position[axis] += followPose.right[axis] * x + followPose.up[axis] * y;
          vertex(position, laserSight->pointColor, uv[corner]);
        }
      }
      if (!probeVao) {
        glGenVertexArrays(1, &probeVao);
        glGenBuffers(1, &probeVbo);
      }
      glBindVertexArray(probeVao);
      glBindBuffer(GL_ARRAY_BUFFER, probeVbo);
      glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(),
                   GL_STREAM_DRAW);
      for (unsigned i = 0; i < 4; ++i) {
        unsigned sizes[4]{3, 2, 4, 3}, offsets[4]{0, 3, 5, 9};
        glVertexAttribPointer(
            i, sizes[i], GL_FLOAT, GL_FALSE, 12 * sizeof(float),
            reinterpret_cast<void *>(offsets[i] * sizeof(float)));
        glEnableVertexAttribArray(i);
      }
      glUniform1i(glGetUniformLocation(program, "normalView"), 0);
      glUniform1i(glGetUniformLocation(program, "textured"), 0);
      glUniform1i(glGetUniformLocation(program, "alphaTest"), 0);
      glUniform1i(glGetUniformLocation(program, "vertexColors"), 1);
      // GX laser/marker passes are unlit and unfogged. Room raster state
      // must not tint the original CORE EST red after the room/object draws.
      glUniform1i(glGetUniformLocation(program, "roomLightingEnabled"), 0);
      glUniform1i(glGetUniformLocation(program, "roomFogType"), 0);
      glUniform1i(glGetUniformLocation(program,"specularType"),-1);
      glUniform2f(glGetUniformLocation(program, "uvOffset"), 0, 0);
      glUniform3f(glGetUniformLocation(program, "materialRGB"), 1, 1, 1);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glDepthMask(GL_FALSE);
      if (probe->showBody && (!actor || (!actor->ladderActive() && !actor->jumpActive())))
        glDrawArrays(GL_TRIANGLES, 0, groundCount);
      glDepthMask(GL_TRUE);
      // Debug body remains visible even when the uncollided follow camera is
      // behind a wall.
      glDisable(GL_DEPTH_TEST);
      if (probe->showBody)
        glDrawArrays(GL_LINES, groundCount, GLsizei(rayFirst - groundCount));
      glEnable(GL_DEPTH_TEST);
      glDepthMask(GL_FALSE);
      if (probe->showAimRay)
        glDrawArrays(GL_LINES, GLint(rayFirst), 2);
      if (data.size() / 12 > pointFirst) {
        constexpr GLenum source[]{GL_SRC_ALPHA, GL_SRC_ALPHA, GL_ONE,
                                   GL_DST_COLOR, GL_DST_COLOR, GL_SRC_ALPHA};
        constexpr GLenum destination[]{GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE,
                                        GL_ONE, GL_ZERO, GL_ONE_MINUS_DST_COLOR};
        glBlendFunc(source[laserSight->blend], destination[laserSight->blend]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, laserPointTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, laserPointTexture);
        glUniform1i(glGetUniformLocation(program, "textured"), 1);
        glUniform1i(glGetUniformLocation(program, "alphaTest"), 1);
        glUniform1f(glGetUniformLocation(program, "alphaReference"), 0);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1, -1); // Surface billboard: depth-tested, no writes.
        glDrawArrays(GL_TRIANGLES, GLint(pointFirst), 6);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glActiveTexture(GL_TEXTURE0);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      }
      glDepthMask(GL_TRUE);
    }
    // All world geometry and other actors contribute to autofocus. Leon and
    // his equipment are drawn afterwards, so they cannot become its target.
    if(worldAutofocus)
      CaptureDepthOfFieldFocusGl(w,h,100.f,1000000.f);
    glDepthMask(GL_TRUE);glDepthFunc(GL_LEQUAL);
    if(worldAutofocus)drawPlayer();
  }
  if(!normalView)if(const auto *cut=CurrentRoomLightCut())
    ApplyRoomFilterGl(w,h,*cut,std::uint64_t(sceneFrames+1.e-5),eventLighting.room);
  if(!normalView && !event && probe && probe->gameplayDepthOfField)ApplyDepthOfFieldGl(w,h,100.f,1000000.f);
  if(!normalView)ApplyEventFocusGl(w,h,event?event->focus():EventFocusState{},100.f,1000000.f,CurrentRoomLightCut());
  if(event) {
    DrawEventSubtitlesGl(w,h,*event);DrawEventFadeGl(w,h,event->fadeAlpha());
  }
  if(!event && !normalView)DrawSourceGlowGl(w,h);
  glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
  if(probe && probe->roomMessage && probe->r100Secondary)
    DrawOriginalRoomMessageGl(w,h,probe->r100Secondary->messageFont(),*probe->roomMessage);
}
} // namespace re4
