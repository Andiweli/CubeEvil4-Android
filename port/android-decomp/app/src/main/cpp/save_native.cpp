#include "save_native.h"
#include "item_rules.h"
#include "asset_reader.h"
#include "language_pack_files.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>
namespace re4 {
namespace {
std::uint16_t read16(const std::uint8_t *p) {return std::uint16_t((unsigned(p[0])<<8)|p[1]);}
std::uint32_t read32(const std::uint8_t *p) {return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];}
void write16(std::uint8_t *p,std::uint16_t x) {p[0]=x>>8;p[1]=x;}
void write32(std::uint8_t *p,std::uint32_t x) {p[0]=x>>24;p[1]=x>>16;p[2]=x>>8;p[3]=x;}
void writeFloat(std::uint8_t *p,float x) {
  if(!std::isfinite(x))throw std::runtime_error("Original save: Non-finite player position");
  std::uint32_t raw;std::memcpy(&raw,&x,4);write32(p,raw);
}
float readFloat(const std::uint8_t *p) {
  auto raw=read32(p);float x;std::memcpy(&x,&raw,4);
  if(!std::isfinite(x))throw std::runtime_error("Original save: Non-finite player position");
  return x;
}
template<std::size_t N> void writeFlags(std::uint8_t *p,const SourceFlags<N> &flags) {
  for(unsigned i=0;i<N;++i)write32(p+i*4,flags.words()[i]);
}
template<std::size_t N> void readFlags(const std::uint8_t *p,SourceFlags<N> &flags) {
  flags.clear();
  for(unsigned i=0;i<N*32;++i)if(read32(p+(i>>5)*4)&(0x80000000u>>(i&31)))flags.set(i);
}
std::uint8_t *roomRecord(std::vector<std::uint8_t> &rooms,std::uint16_t id) {
  const auto n=read32(rooms.data()+4);
  for(unsigned i=0;i<n;++i)if(read16(rooms.data()+16+i*0xd8)==id)return rooms.data()+16+i*0xd8;
  return nullptr;
}
bool emptyRoom(const RoomSaveState &r) {
  const auto any=[](const auto &a){return std::any_of(a.begin(),a.end(),[](auto x){return x!=0;});};
  return !any(r.room.words())&&!any(r.items.words())&&!any(r.itemsFound.words())&&!any(r.etc);
}
std::int8_t centerByte(int top,unsigned extent) {
  const int value=top*2+int(extent)-1;
  if(value<-128 || value>127)throw std::runtime_error("Original save: Case position");
  return std::int8_t(value);
}
struct WeaponMap {unsigned id,no,type;};
constexpr WeaponMap weaponMap[] = {
  {0x23,0x02,0},
  {0x24,0x02,1},
  {0x25,0x03,0},
  {0x26,0x03,2},
  {0x03,0x11,0},
  {0x21,0x01,0},
  {0x22,0x01,1},
  {0x40,0x01,0},
  {0x27,0x04,0},
  {0x28,0x04,1},
  {0x29,0x05,0},
  {0x2C,0x07,0},
  {0x2D,0x08,0},
  {0x2E,0x09,0},
  {0x6B,0x09,1},
  {0x99,0x09,2},
  {0x2F,0x0A,0},
  {0x6C,0x0A,1},
  {0x51,0x0A,2},
  {0x30,0x0B,0},
  {0x31,0x0B,1},
  {0x32,0x0B,2},
  {0x33,0x0B,3},
  {0x36,0x0E,0},
  {0xAB,0x0E,1},
  {0x2A,0x06,0},
  {0x2B,0x06,1},
  {0x34,0x0C,0},
  {0x35,0x0D,0},
  {0x17,0x0D,1},
  {0x6D,0x0D,2},
  {0x37,0x0F,0},
  {0x38,0x10,0},
  {0x01,0x13,0},
  {0x02,0x16,0},
  {0x0E,0x17,0},
  {0x08,0x19,0},
  {0x09,0x1F,0},
  {0x0A,0x20,0},
  {0x94,0x21,0},
  {0x52,0x1C,0},
  {0x3E,0x0B,0},
};
void writeInventory(OriginalSaveSections &out,const NativeSaveSnapshot &s) {
  if(s.caseSize>3 || s.inventory.size()>0x180)throw std::runtime_error("Original save: Case budget");
  InventoryCase validation;validation.restoreSnapshot(s.caseSize,s.inventory,s.equippedSlot);
  // Keep the other inventory set and the original per-slot tuning/part data.
  const auto old=out.inventory;
  auto *p=out.inventory.data();std::set<unsigned> slots;
  for(unsigned i=0;i<0x180;++i) {
    auto *item=p+4+i*12;
    if(read16(item)!=0xffff && (read16(item)&0x8000))continue;
    std::fill_n(item,12,0);write16(item,0xffff);
  }
  write16(p,0x800);write16(p+2,0xffff); // Original bareHand(), no armed slot.
  for(const auto &i:s.inventory) {
    if(!i.active)continue;
    const auto info=OriginalItemInfo(i.id);
    if(i.slot>=0x180 || i.id>=0x8000 || !info.type || !i.count || i.count>65535 ||
       i.loaded>0x1fff || i.rotation>3 || !slots.insert(i.slot).second)
      throw std::runtime_error("Original save: Item record");
    auto *d=p+4+i.slot*12;const auto *prior=old.data()+4+i.slot*12;
    if(read16(prior)!=0xffff && (read16(prior)&0x8000))
      throw std::runtime_error("Original save: slot occupied by a different inventory set");
    const bool same=read16(prior)==i.id;
    write16(d,std::uint16_t(i.id));
    if(info.type==1 || info.type==9) {
      write16(d+2,info.type==9?(i.attachedTo==~0u?0:1):same?read16(prior+2):i.id==0x34?0x6025:0);
      write16(d+4,info.type==9?(i.attachedTo==~0u?0xffff:std::uint16_t(i.attachedTo)):
          std::uint16_t((same?read16(prior+4)&0xe000:0)|i.loaded));
    } else {
      write16(d+2,std::uint16_t(i.count));
      if(info.type==10 && same)write16(d+4,read16(prior+4));
    }
    auto shape=OriginalItemFootprint(i.id);
    if(shape.width && i.inCase()) {
      if(i.rotation&1)std::swap(shape.width,shape.height);
      d[8]=std::uint8_t(centerByte(i.x,shape.width));d[9]=std::uint8_t(centerByte(i.y,shape.height));
      d[10]=i.rotation;d[11]=1;
    }
    if(i.slot==s.equippedSlot) {
      if(!OriginalWeaponCapacity(i.id) && i.id!=0x52)
        throw std::runtime_error("Original save: equipped slot is not a weapon");
      const auto effective=validation.weaponId(i.slot);
      write16(p,std::uint16_t(effective));write16(p+2,std::uint16_t(i.slot));
      const auto w=std::find_if(std::begin(weaponMap),std::end(weaponMap),[&](auto x){return x.id==effective;});
      if(w!=std::end(weaponMap)) {out.global[0x30]=w->no;out.global[0x31]=w->type;}
      const auto level=read16(d+2);out.global[0x32]=read16(d+4)>>13;
      out.global[0x33]=level>>12;out.global[0x34]=(level>>8)&15;
      out.global[0x3a]=(level>>4)&15;out.global[0x35]=level&15;
    }
  }
  if(s.equippedSlot!=~0u && !slots.count(s.equippedSlot))
    throw std::runtime_error("Original save: equipped slot is missing");
  // Board size is represented by original case items7c..7f, never a new field.
  unsigned largest=0;bool found=false;
  for(const auto &i:s.inventory)if(i.active && i.id>=0x7c && i.id<=0x7f) {largest=std::max(largest,i.id-0x7c);found=true;}
  if(!found || largest!=s.caseSize)throw std::runtime_error("Original save: Case size item is missing");
}
}
const std::vector<std::uint16_t> &OriginalSaveRoomIds() {
  // roomdata.cpp::St0..4_data_tbl save_flg1, source ascending stage/room order.
  static const std::vector<std::uint16_t> ids={
    0x100,0x101,0x102,0x103,0x104,0x105,0x106,0x107,0x108,0x109,0x10a,0x10b,0x10c,0x10d,0x10e,0x10f,
    0x111,0x112,0x113,0x117,0x118,0x119,0x11a,0x11b,0x11c,0x11d,0x11e,0x11f,
    0x200,0x201,0x202,0x203,0x204,0x205,0x206,0x207,0x208,0x209,0x20a,0x20b,0x20c,0x20d,0x20e,0x20f,
    0x210,0x211,0x212,0x213,0x214,0x215,0x216,0x217,0x218,0x219,0x21a,0x21b,0x21d,0x220,0x221,0x222,
    0x223,0x224,0x225,0x226,0x227,0x228,0x229,0x22a,0x22b,0x22c,
    0x300,0x301,0x303,0x304,0x305,0x306,0x307,0x308,0x309,0x30a,0x30b,0x30c,0x30d,0x30e,0x30f,0x310,
    0x311,0x312,0x315,0x316,0x317,0x318,0x31a,0x31b,0x31c,0x31d,0x320,0x321,0x325,0x326,0x327,0x328,
    0x329,0x330,0x331,0x332,0x333,
    0x400,0x402,0x403,0x404,0x405,0x406,0x40a,0x40b,0x40c,0x40d,0x40e,0x40f,0x410,0x411};
  return ids;
}
OriginalSaveSections NewOriginalSaveSections() {
  OriginalSaveSections s;s.global.resize(OriginalSaveGlobalBytes);s.inventory.resize(OriginalSaveInventoryBytes);
  s.rooms.resize(16+OriginalSaveRoomIds().size()*0xd8);s.subscreen.resize(4);s.merchant.resize(OriginalSaveMerchantBytes);
  write32(s.rooms.data(),std::uint32_t(s.rooms.size()));write32(s.rooms.data()+4,unsigned(OriginalSaveRoomIds().size()));
  for(unsigned i=0;i<OriginalSaveRoomIds().size();++i)write16(s.rooms.data()+16+i*0xd8,OriginalSaveRoomIds()[i]);
  write16(s.inventory.data(),0xffff);write16(s.inventory.data()+2,0xffff);
  for(unsigned i=0;i<0x180;++i)write16(s.inventory.data()+4+i*12,0xffff);
  // Original MerchantGameInit/stockDataInit/levelDataInit.
  for(unsigned i=0;i<64;++i)write16(s.merchant.data()+i*8,0xffff);
  for(unsigned i=0;i<32;++i)write16(s.merchant.data()+0x200+i*8,0xffff);
  s.merchant[0x300]=50;
  write16(s.global.data()+0x28,600);write16(s.global.data()+0x2a,600);
  s.global[0x23]=0xff; // em_list_no until the actual source ESL is supplied.
  write32(s.global.data()+0x3380,0x18); // SubScreenGameInit::ope_mdt_no.
  return s;
}
OriginalSaveSections EncodeNativeSave(const NativeSaveSnapshot &s,const OriginalSaveSections *basis) {
  auto out=basis?*basis:NewOriginalSaveSections();ValidateOriginalSaveSections(out);
  if(s.life>s.maxLife || !s.maxLife || s.difficulty>6 || s.scenario.pesetas>99999999)
    throw std::runtime_error("Original save: Player values outside bounds");
  auto *g=out.global.data();write32(g+4,std::uint32_t(s.difficultyPoint));g[8]=s.gameLevel;g[0xa]=s.chapter;
  write16(g+0xc,s.saveCount);write16(g+0xe,s.roundCount);g[0x12]=s.terminal;g[0x13]=s.language;
  write16(g+0x10,s.roomContinues);write16(g+0x33b8,s.chapterContinues);write16(g+0x33ba,s.totalContinues);
  if(s.saveKind)write32(g+0x33d8,std::uint32_t(*s.saveKind));
  for(unsigned i=0;i<3;++i)write32(g+0x3370+i*4,s.radioHeard[i]);
  write32(g+0x3380,s.radioMessageNo);
  write32(g+0x14,s.playSeconds);write32(g+0x18,s.scenario.pesetas);
  write16(g+0x1c,s.room);g[0x1e]=s.part;g[0x1f]=s.jumpPoint;write16(g+0x20,s.previousRoom);g[0x22]=s.previousPart;
  write16(g+0x24,s.life);write16(g+0x26,s.maxLife);
  write16(g+0x28,s.ashleyLife);write16(g+0x2a,s.ashleyMaxLife);g[0x38]=s.playerType;
  for(unsigned i=0;i<3;++i)writeFloat(g+0x40+i*4,s.position[i]);
  writeFloat(g+0x4c,s.heading);
  writeFlags(g+0x21c,s.scenario.globalItems);writeFlags(g+0x22c,s.scenario.globalItemsFound);
  writeFlags(g+0x23c,s.scenario.scenario);writeFlags(g+0x25c,s.scenario.key);
  write32(g+0x33bc,s.chapterKills);write32(g+0x33c0,s.totalKills);write32(g+0x33c4,s.chapterHits);
  write32(g+0x33c8,s.totalHits);write32(g+0x33cc,s.chapterShots);write32(g+0x33d0,s.totalShots);g[0x33d4]=s.difficulty;
  for(const auto &[id,r]:s.scenario.rooms) {
    auto *p=roomRecord(out.rooms,id);
    if(!p) {if(!emptyRoom(r))throw std::runtime_error("Original save: Room has no record");continue;}
    writeFlags(p+4,r.room);writeFlags(p+8,r.items);writeFlags(p+0x18,r.itemsFound);
    for(unsigned i=0;i<64;++i)write16(p+0x28+i*2,r.etc[i]);
  }
  for(const auto &[id,bits]:s.passedRooms) {
    if(auto *p=roomRecord(out.rooms,id))p[2]=bits;
    else if(bits)throw std::runtime_error("Original save: visited room has no record");
  }
  if(s.enemyDeaths)for(unsigned list=0;list<12;++list)for(unsigned i=0;i<8;++i)
    write32(g+0x9c+list*32+i*4,(*s.enemyDeaths)[list][i]);
  if(s.enemyList) {
    if(s.enemyList->size()!=255*32 && s.enemyList->size()!=256*32)
      throw std::runtime_error("Original save: ESL size");
    std::fill_n(g+0x368,256*32,0);std::copy(s.enemyList->begin(),s.enemyList->end(),g+0x368);
    if(s.enemyListNo<-1 || s.enemyListNo>=12)throw std::runtime_error("Original save: ESL number");
    g[0x23]=std::uint8_t(s.enemyListNo);
  }
  if(s.looseItems) {
    if(s.looseItems->size()>256)throw std::runtime_error("Original save: Pickup budget");
    std::fill_n(g+0x2368,0x1000,0);
    for(unsigned i=0;i<s.looseItems->size();++i) {
      const auto &v=(*s.looseItems)[i];auto *p=g+0x2368+i*16;
      if(!v.room || v.type>1 || !v.count || !OriginalItemInfo(v.id).type)
        throw std::runtime_error("Original save: Pickup record");
      p[0]=v.type;p[1]=v.area;p[2]=std::uint8_t(v.effect);write16(p+4,v.room);write16(p+6,v.id);write16(p+8,v.count);
      for(unsigned j=0;j<3;++j) {
        const float quantized=v.position[j]/10.0f;
        if(!std::isfinite(quantized) || quantized<-32768 || quantized>=32768)
          throw std::runtime_error("Original save: Pickup position");
        write16(p+10+j*2,std::uint16_t(std::int16_t(quantized)));
      }
    }
  }
  writeInventory(out,s);ValidateOriginalSaveSections(out);return out;
}
NativeSaveSnapshot DecodeNativeSave(const OriginalSaveSections &s) {
  ValidateOriginalSaveSections(s);NativeSaveSnapshot out;const auto *g=s.global.data();
  out.difficultyPoint=std::int32_t(read32(g+4));out.gameLevel=g[8];out.chapter=g[0xa];
  out.saveCount=read16(g+0xc);out.roundCount=read16(g+0xe);out.terminal=g[0x12];out.language=g[0x13];
  for(unsigned i=0;i<3;++i)out.radioHeard[i]=read32(g+0x3370+i*4);
  out.radioMessageNo=read32(g+0x3380);
  out.playSeconds=read32(g+0x14);out.scenario.pesetas=read32(g+0x18);out.room=read16(g+0x1c);
  out.roomContinues=read16(g+0x10);out.chapterContinues=read16(g+0x33b8);out.totalContinues=read16(g+0x33ba);
  out.saveKind=std::int32_t(read32(g+0x33d8));
  out.part=g[0x1e];out.jumpPoint=g[0x1f];out.previousRoom=read16(g+0x20);out.previousPart=g[0x22];
  out.life=read16(g+0x24);out.maxLife=read16(g+0x26);
  out.ashleyLife=read16(g+0x28);out.ashleyMaxLife=read16(g+0x2a);out.playerType=g[0x38];
  for(unsigned i=0;i<3;++i)out.position[i]=readFloat(g+0x40+i*4);
  out.heading=readFloat(g+0x4c);
  readFlags(g+0x21c,out.scenario.globalItems);readFlags(g+0x22c,out.scenario.globalItemsFound);
  readFlags(g+0x23c,out.scenario.scenario);readFlags(g+0x25c,out.scenario.key);
  out.chapterKills=read32(g+0x33bc);out.totalKills=read32(g+0x33c0);out.chapterHits=read32(g+0x33c4);
  out.totalHits=read32(g+0x33c8);out.chapterShots=read32(g+0x33cc);out.totalShots=read32(g+0x33d0);out.difficulty=g[0x33d4];
  const auto n=read32(s.rooms.data()+4);
  for(unsigned i=0;i<n;++i) {
    const auto *p=s.rooms.data()+16+i*0xd8;const auto id=read16(p);
    auto &r=out.scenario.room(id);out.passedRooms[id]=p[2];
    readFlags(p+4,r.room);readFlags(p+8,r.items);readFlags(p+0x18,r.itemsFound);
    for(unsigned j=0;j<64;++j)r.etc[j]=read16(p+0x28+j*2);
  }
  out.enemyDeaths.emplace();
  for(unsigned list=0;list<12;++list)for(unsigned i=0;i<8;++i)(*out.enemyDeaths)[list][i]=read32(g+0x9c+list*32+i*4);
  out.enemyList=std::vector<std::uint8_t>(g+0x368,g+0x2368);out.enemyListNo=std::int8_t(g[0x23]);out.looseItems.emplace();
  for(unsigned i=0;i<256;++i) {
    const auto *p=g+0x2368+i*16;if(!read16(p+4))continue;
    NativeSaveLooseItem v;v.type=p[0];v.area=p[1];v.effect=std::int8_t(p[2]);v.room=read16(p+4);v.id=read16(p+6);v.count=read16(p+8);
    for(unsigned j=0;j<3;++j)v.position[j]=float(std::int16_t(read16(p+10+j*2)))*10;
    out.looseItems->push_back(v);
  }
  const auto *p=s.inventory.data();out.equippedSlot=read16(p+2)==0xffff?~0u:read16(p+2);
  for(unsigned i=0;i<0x180;++i) {
    const auto *v=p+4+i*12;const auto id=read16(v);if(id==0xffff || (id&0x8000))continue;
    const auto info=OriginalItemInfo(id);if(!info.type)throw std::runtime_error("Original save: unknown item");
    CaseItem c;c.slot=i;c.id=id;c.active=true;c.rotation=v[10];
    c.count=(info.type==1 || info.type==9)?1:read16(v+2);c.loaded=info.type==1?read16(v+4)&0x1fff:0;
    if(info.type==9 && read16(v+2)==1)c.attachedTo=read16(v+4);
    auto shape=OriginalItemFootprint(id);
    if(shape.width && v[11]) {
      if(c.rotation>3)throw std::runtime_error("Original save: unsupported mirrored case shape");
      if(c.rotation&1)std::swap(shape.width,shape.height);
      const int x=int(std::int8_t(v[8]))-(int(shape.width)-1),y=int(std::int8_t(v[9]))-(int(shape.height)-1);
      if((x&1)||(y&1))throw std::runtime_error("Original save: Case shape is not aligned to the cell grid");
      c.x=x/2;c.y=y/2;
    }
    if(id>=0x7c && id<=0x7f)out.caseSize=std::max(out.caseSize,unsigned(id-0x7c));
    out.inventory.push_back(c);
  }
  InventoryCase validation;validation.restoreSnapshot(out.caseSize,out.inventory,out.equippedSlot);
  return out;
}
NativeSaveSnapshot RestoreNativeCheckpoint(const NativeSaveSnapshot &checkpoint,const NativeSaveSnapshot &current) {
  auto result=checkpoint;
  result.playSeconds=current.playSeconds;
  result.roomContinues=std::uint16_t(current.roomContinues+1);
  result.chapterContinues=std::uint16_t(current.chapterContinues+1);
  result.totalContinues=std::uint16_t(current.totalContinues+1);
  static constexpr float down[11]={.5f,.6f,.7f,.8f,.9f,1,1.1f,1.2f,1.3f,1.5f,2};
  const unsigned rank=unsigned(std::clamp(result.difficultyPoint/1000,0,10));
  result.difficultyPoint=std::clamp(result.difficultyPoint+int(-800.5f*down[rank]),0,10999);
  if(result.difficulty==6)result.difficultyPoint=10999;
  if(result.language && result.difficultyPoint<1000)result.difficultyPoint=1000;
  result.gameLevel=std::uint8_t(result.difficulty==6?10:result.difficultyPoint/(result.difficulty==1?2750:result.difficulty==3?1833:1000));
  return result;
}
std::vector<NativeSaveLooseItem> CaptureOriginalPersistentDrops(const std::map<std::uint16_t,std::vector<RoomPickupRecord>> &drops) {
  std::vector<NativeSaveLooseItem> out;
  for(const auto &[room,items]:drops)for(const auto &i:items) {
    if(!i.runtime || i.runtimeTaken)continue;
    const auto type=OriginalItemInfo(i.id).type;
    if(type!=5 && type!=7)continue;
    NativeSaveLooseItem v;v.room=room;v.id=std::uint16_t(i.id);v.count=std::uint16_t(i.count);v.effect=std::int8_t(i.effectType);
    for(unsigned j=0;j<3;++j)v.position[j]=float(i.position[j]);
    v.position[1]-=i.effectType==8?1000:100;out.push_back(v);
  }
  return out;
}
std::map<std::uint16_t,std::vector<RoomPickupRecord>> RestoreOriginalPersistentDrops(const std::vector<NativeSaveLooseItem> &items) {
  std::map<std::uint16_t,std::vector<RoomPickupRecord>> out;
  for(const auto &v:items) {
    if(v.type)continue;
    RoomPickupRecord r;r.runtime=true;r.id=v.id;r.count=v.count;
    r.effectType=v.effect<0?0:unsigned(v.effect);
    for(unsigned j=0;j<3;++j)r.position[j]=v.position[j];
    r.position[1]+=r.effectType==8?1000:100;
    out[v.room].push_back(r);
  }
  return out;
}
OriginalSaveCardArt ParseOriginalSaveCardArt(const std::vector<std::uint8_t> &bytes,bool japanese) {
  const AssetReader r{bytes};
  if(bytes.size()<0x400 || bytes.size()>1024*1024)throw std::runtime_error("Save artwork: DAS size");
  std::vector<std::uint8_t> common;bool end=false;
  // SndSubDataRead/cDvdQueue TRANS_MRAM, then cCard::initSub CardArc.ofs[0].
  for(unsigned entry=0x20;entry<0x400;entry+=32) {
    const auto kind=r.u32(entry);
    if(kind==0xffffffffu) {end=true;break;}
    if(kind!=0)continue;
    const auto size=r.u32(entry+4),offset=r.u32(entry+12);r.range(offset,size);
    if(offset<0x400 || !common.empty())throw std::runtime_error("Save artwork: Ambiguous MRAM section");
    common.assign(bytes.begin()+offset,bytes.begin()+offset+size);
  }
  if(!end || common.empty())throw std::runtime_error("Save artwork: MRAM section is missing");
  RoomArchive archive(std::move(common));
  const auto *entry=archive.find("TPL");
  if(!entry || entry!=&archive.entries().front())throw std::runtime_error("Save artwork: Original TPL is missing");
  auto tpl=archive.copy(0);const AssetReader t{tpl};
  if(t.u32(0)!=0x20af30 || t.u32(4)<3)throw std::runtime_error("Save artwork: TPL table is missing");
  const auto table=t.u32(8);t.range(table,t.u32(4)*8);
  OriginalSaveCardArt art;
  const auto copyTexture=[&](unsigned no,unsigned width,unsigned height,unsigned format,auto &out) {
    const auto tex=t.u32(table+no*8);t.range(tex,36);
    if(t.u16(tex)!=height || t.u16(tex+2)!=width || t.u32(tex+4)!=format)
      throw std::runtime_error("Save artwork: Original texture format");
    const auto data=t.u32(tex+8);t.range(data,out.size());std::copy_n(tpl.data()+data,out.size(),out.data());
  };
  copyTexture(japanese?0:1,96,32,5,art.banner);copyTexture(2,32,32,9,art.icon);
  const auto palette=t.u32(table+2*8+4);t.range(palette,12);
  if(t.u16(palette)!=256 || t.u32(palette+4)!=2)throw std::runtime_error("Save artwork: Original TLUT format");
  const auto data=t.u32(palette+8);t.range(data,art.palette.size());std::copy_n(tpl.data()+data,art.palette.size(),art.palette.data());
  return art;
}
OriginalSaveCardArt LoadOriginalSaveCardArt(const std::string &files,bool japanese) {
  std::ifstream f(re4lang::DiscFile(files,"etc/memcard.das"),std::ios::binary|std::ios::ate);
  if(!f)throw std::runtime_error("Save artwork: etc/memcard.das is missing");
  const auto n=f.tellg();if(n<0x400 || n>1024*1024)throw std::runtime_error("Save artwork: DAS-Budget");
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(n));f.seekg(0);
  if(!f.read(reinterpret_cast<char*>(bytes.data()),n))throw std::runtime_error("Save artwork: Truncated DAS");
  return ParseOriginalSaveCardArt(bytes,japanese);
}
bool OriginalSaveEnemyDead(const NativeSaveEnemyFlags &flags,unsigned list,unsigned index) {
  if(list>=12 || index>=256)throw std::out_of_range("Original save: Enemy death bit");
  return (flags[list][index>>5]&(0x80000000u>>(index&31)))!=0;
}
void SetOriginalSaveEnemyDead(NativeSaveEnemyFlags &flags,unsigned list,unsigned index,bool dead) {
  if(list>=12 || index>=256)throw std::out_of_range("Original save: Enemy death bit");
  const auto bit=0x80000000u>>(index&31);auto &word=flags[list][index>>5];
  if(dead)word|=bit;else word&=~bit;
}
} // namespace re4
