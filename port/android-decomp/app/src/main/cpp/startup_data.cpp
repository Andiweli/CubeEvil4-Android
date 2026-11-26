#include "startup_data.h"
#include "asset_reader.h"
#include "language_pack_files.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
namespace re4 {
namespace {
std::vector<std::uint8_t> read(const std::string &path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream || stream.tellg() <= 0 || stream.tellg() > 16*1024*1024)
    throw std::runtime_error("Title: original archive missing or invalid");
  std::vector<std::uint8_t> bytes(std::size_t(stream.tellg())); stream.seekg(0);
  if (!stream.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))
    throw std::runtime_error("Title: original archive truncated");
  return bytes;
}
void curve(std::ostringstream &out,const AssetReader &r,std::size_t p) {
  out << '[';
  if(p) {
    const auto count=r.u32(p);
    if(count!=0xffffffffu) {
      if(count>64)throw std::runtime_error("Title: curve budget");
      r.range(p+4,count*16);
      float previous=-std::numeric_limits<float>::infinity();
      for(unsigned k=0;k<count;++k) {
        const float time=r.f32(p+4+k*16);
        if(time<previous)throw std::runtime_error("Title: curve time order");
        previous=time;
        if(k) {out << ',';} out << '[';
        for(unsigned j=0;j<4;++j){if(j)out << ',';out << r.f32(p+4+k*16+j*4);}
        out << ']';
      }
    }
  }
  out << ']';
}
}
TitleData TitleData::VillageTitle(const std::string &discFiles) {
  // st1/r101.cpp::r101_Event30_TitleCall, the original English event owner.
  return TitleData(read(discFiles+"/etc/Eng/id101.eff"),read(discFiles+"/etc/Eng/event001.uwf"));
}
TitleData::TitleData(const std::string &discFiles,unsigned language) {
  const char *names[]={"Jpn","Eng","Ger","Fra","Esp","Ita"};
  if(language>5)throw std::runtime_error("Title: language unsupported");
  auto bytes=read(discFiles+"/ss/"+names[language]+"/title.dat");
  // This supplied debug disc contains Eng title data. Do not silently substitute
  // another language: absent localization must be reported to the caller.
  textures_=DecodeSubscreenIdTextures(CopySubscreenEntry(bytes,4));
  auto options=DecodeSubscreenIdTextures(CopySubscreenEntry(bytes,12));
  textures_.insert(textures_.end(),std::make_move_iterator(options.begin()),std::make_move_iterator(options.end()));
  for(unsigned group:{6u,7u,8u,9u,10u,11u,13u}) {
    auto table=CopySubscreenEntry(bytes,group);
    // Title timelines legitimately start before frame zero (authored -15
    // colour key); the static subscreen LastCurve validator is unsuitable.
    layouts_.emplace(group,std::move(table));
  }
  const auto optionArchive=read(discFiles+"/ss/"+names[language]+"/option.dat");
  auto optionTextures=DecodeSubscreenIdTextures(CopySubscreenEntry(optionArchive,8));
  // Original persistent CORE glyphs, digits and solid textures supplement the
  // option archive. Give each simultaneously retained archive its own IDs.
  auto shared=LoadSubscreenArt(discFiles,SubscreenArtKind::Common,"eng");
  for(auto &t:shared.textures) {
    bool found=false;for(const auto &o:optionTextures)if(o.id==t.id&&o.frame==t.frame){found=true;break;}
    if(!found)optionTextures.push_back(t);
  }
  for(auto &t:optionTextures){t.id+=256;textures_.push_back(std::move(t));}
  for(unsigned group:{9u,10u,11u,12u,13u,14u}) {
    auto table=CopySubscreenEntry(optionArchive,group);
    layouts_.emplace(1000+group,std::move(table));
  }
  const char saveLanguage[]={ 'j','e','g','f','s','i' };
  const auto saveArchive=read(discFiles+"/ss/Cmn/save_"+saveLanguage[language]+".dat");
  auto saveTextures=DecodeSubscreenIdTextures(CopySubscreenEntry(saveArchive,4));
  // CardID::init clears IdTex before registering this self-contained pack.
  // An ID belongs to one complete texture set, never frames from several
  // owners. Save ID39 is a one-frame header; CORE ID39 is a 15-frame effect.
  // Mixing CORE's frames 1..14 into CARD also overwrote its numTex and made
  // LOAD/SAVE animate those indirect maps as visible artwork.
  for(auto &t:saveTextures){t.id+=512;textures_.push_back(std::move(t));}
  for(unsigned group:{5u,6u,7u,8u,9u}) {
    auto table=CopySubscreenEntry(saveArchive,group);
    layouts_.emplace(2000+group,std::move(table));
  }
  const auto font=LoadOriginalMenuFont(discFiles);
  std::ostringstream f;f << "{\"cellWidth\":" << font.cellWidth << ",\"cellHeight\":" << font.cellHeight << ",\"bearings\":[";
  for(unsigned i=0;i<font.bearings.size();++i){if(i)f << ',';f << '[' << int(font.bearings[i][0]) << ',' << int(font.bearings[i][1]) << ']';}
  f << "],\"sheets\":[";
  for(unsigned i=0;i<font.sheets.size();++i){if(i)f << ',';f << 2000+i;textures_.push_back({2000+i,0,font.sheets[i]});}
  messages_.emplace(3000,f.str()+"]}");
  const auto das=read(re4lang::DiscFile(discFiles,"etc/core.das"));AssetReader d{das};
  const auto coreOffset=d.u32(0x2c),coreLength=d.u32(0x24);d.range(coreOffset,coreLength);
  std::vector<std::uint8_t> core(das.begin()+coreOffset,das.begin()+coreOffset+coreLength);
  // Cockpit and death/continue use their own authored archives. Keep each
  // texture owner separate just as with title/options/save (no ID collisions).
  auto cockpitTextures=DecodeSubscreenIdTextures(CopySubscreenEntry(core,0x1d));
  for(auto t:cockpitTextures)if(t.image.width && t.image.height){t.id+=1024;textures_.push_back(std::move(t));}
  for(unsigned group:{0x1fu,0x20u,0x26u})
    layouts_.emplace(6000+group,CopySubscreenEntry(core,group));
  auto deathTextures=DecodeSubscreenIdTextures(CopySubscreenEntry(optionArchive,4));
  deathTextures.erase(std::remove_if(deathTextures.begin(),deathTextures.end(),[](const auto &t){return !t.image.width || !t.image.height;}),deathTextures.end());
  for(const auto &t:cockpitTextures)if(t.image.width && t.image.height) {
    const bool present=std::any_of(deathTextures.begin(),deathTextures.end(),[&](const auto &v){return v.id==t.id&&v.frame==t.frame;});
    if(!present)deathTextures.push_back(t);
  }
  for(auto &t:deathTextures){t.id+=768;textures_.push_back(std::move(t));}
  for(unsigned group:{5u,6u,7u})
    layouts_.emplace(7000+group,CopySubscreenEntry(optionArchive,group));
  const auto coreMessages=CopySubscreenEntry(core,0x0a);
  std::vector<unsigned> messageIds={0x81u,0x82u,0x83u,0x84u,0x85u,0x86u,0x8bu,0x8cu,0x92u,0x8du,0x8eu};
  for(unsigned kind=0;kind<=47;++kind)messageIds.push_back(kind+0x16);
  for(unsigned message:messageIds) {
    const auto glyphs=DecodeSubscreenGlyphs(ReadSubscreenMessage(coreMessages,message,language));
    std::ostringstream text;text << "{\"glyphs\":[";
    for(unsigned i=0;i<glyphs.size();++i){if(i)text << ',';text << '[' << glyphs[i].glyph << ',' << glyphs[i].line << ',' << glyphs[i].color << ']';}
    messages_.emplace(4000+message,text.str()+"]}");
  }
  const auto cardDas=read(re4lang::DiscFile(discFiles,"etc/memcard.das"));AssetReader cd{cardDas};const auto off=cd.u32(0x2c),len=cd.u32(0x24);cd.range(off,len);
  const std::vector<std::uint8_t> cardBody(cardDas.begin()+off,cardDas.begin()+off+len);
  const auto cardMessages=CopySubscreenEntry(cardBody,5);
  for(unsigned number:{1u,11u,12u,13u,14u,15u,16u,17u,18u,19u,20u,22u,30u,31u,32u,36u,38u}) {
    const auto glyphs=DecodeSubscreenGlyphs(ReadSubscreenMessage(cardMessages,number,language),true);
    std::ostringstream message;message << "{\"glyphs\":[";
    for(unsigned i=0;i<glyphs.size();++i){if(i)message << ',';message << '[' << glyphs[i].glyph << ',' << glyphs[i].line << ',' << glyphs[i].color << ']';}
    messages_.emplace(5000+number,message.str()+"]}");
  }
  for(const auto &texture:textures_) {
    frames_[texture.id]=std::max(frames_[texture.id],texture.frame+1);
    animationFrames_[texture.id]=texture.animationFrames;
  }
  for(const auto &layout:layouts_)layoutJson(layout.first); // Validate all ranges/curves at load.
}
const Texture *TitleData::texture(unsigned id,unsigned frame) const {
  for(const auto &t:textures_)if(t.id==id && t.frame==frame)return &t.image;
  return nullptr;
}
TitleData::TitleData(const std::vector<std::uint8_t> &textures,const std::vector<std::uint8_t> &layout)
    :TitleData(DecodeSubscreenIdTextures(textures),layout) {}
TitleData::TitleData(std::vector<SubscreenTexture> textures,const std::vector<std::uint8_t> &layout)
    :textures_(std::move(textures)) {
  layouts_.emplace(8000,layout);
  for(const auto &t:textures_) {frames_[t.id]=std::max(frames_[t.id],t.frame+1);animationFrames_[t.id]=t.animationFrames;}
  layoutJson(8000);
}
std::string TitleData::layoutJson(unsigned group) const {
  const auto message=messages_.find(group);if(message!=messages_.end())return message->second;
  const auto &b=layouts_.at(group);const AssetReader r{b};
  const unsigned namespaceOffset=group==8000?0:group>=7000?768:group>=6000?1024:group>=2000?512:group>=1000?256:0;
  r.range(0,8);const bool v2=b[0]=='2'&&b[1]=='.';
  if(!v2&&!(b[0]=='1'&&b[1]=='.'))throw std::runtime_error("Title: layout version");
  const unsigned stride=v2?0x8c:0x88;r.range(8,std::size_t(b[5])*stride);
  std::ostringstream out;out << std::setprecision(9) << "{\"width\":640,\"height\":480,\"units\":[";
  for(unsigned i=0;i<b[5];++i) {
    if(i) {out << ',';} const auto p=8+i*stride,c=p+(v2?0x74:0x70);
    out << "{\"mark\":" << unsigned(b[p+4]) << ",\"unit\":" << unsigned(b[p+5])
        << ",\"parent\":" << unsigned(b[p+7]) << ",\"kind\":" << unsigned(b[p+9])
        << ",\"flags\":" << unsigned(b[p+3]) << ",\"texture\":" << unsigned(b[p+11]==255?255:b[p+11]+namespaceOffset)
        << ",\"anchor\":" << unsigned(b[p+12]&15) << ",\"loop\":" << unsigned(b[p+13])
        << ",\"scaleType\":" << unsigned(b[p+14]) << ",\"rotAxis\":" << unsigned(b[p+15])
        << ",\"blend\":" << unsigned(b[p+(v2?0x6c:0x68)])
        << ",\"transType\":" << unsigned(b[p+(v2?0x6d:0x69)])
        << ",\"transSub\":" << unsigned(b[p+(v2?0x70:0x6c)])
        << ",\"textureFlags\":" << unsigned(b[p+(v2?0x6f:0x6b)])
        << ",\"textureFrames\":" << (b[p+11]==255?0:frames_.at(b[p+11]+namespaceOffset))
        << ",\"textureAnimationFrames\":" << (b[p+11]==255?0:animationFrames_.at(b[p+11]+namespaceOffset))
        << ",\"mask\":" << unsigned((b[p+(v2?0x6f:0x6b)]&1)?b[p+(v2?0x6e:0x6a)]+namespaceOffset:255)
        << ",\"x\":" << r.f32(p+0x14) << ",\"y\":" << r.f32(p+0x18)
        << ",\"rotationX\":" << r.f32(p+(v2?0x60:0x5c))
        << ",\"rotationY\":" << r.f32(p+(v2?0x60:0x5c)+4)
        << ",\"rotation\":" << r.f32(p+(v2?0x60:0x5c)+8)
        << ",\"width\":" << r.f32(p+0x50) << ",\"height\":" << r.f32(p+0x54);
    out << ",\"color\":[";for(unsigned j=0;j<4;++j){if(j)out << ',';out << unsigned(b[p+0x58+j]);}out << ']';
    out << ",\"color1\":[";for(unsigned j=0;j<4;++j){if(j)out << ',';out << (v2?unsigned(b[p+0x5c+j]):0);}out << ']';
    out << ",\"curves\":[";for(unsigned j=0;j<4;++j){if(j)out << ',';curve(out,r,r.u32(c+8+j*4));}out << ']';
    const auto path=r.u32(c);out << ",\"path\":[";
    if(path){r.range(path,8);auto count=b[path+7];if(count>64)throw std::runtime_error("Title: path budget");
      r.range(path+8,count*12);for(unsigned j=0;j<count;++j){if(j)out << ',';out << '[' << r.f32(path+8+j*12) << ',' << r.f32(path+12+j*12) << ',' << r.f32(path+16+j*12) << ']';}}
    out << "],\"order\":" << (path?unsigned(b[path]):0) << '}';
  }
  return out.str()+"]}";
}
}
