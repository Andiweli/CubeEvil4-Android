// The menus' words no HD pack replaces (tex_pack.cpp, KeepsGameTexture). Made 16.11.2026 from the UI files of the
// debug, US (G4BE08), EU (G4BP08, English, German, French, Spanish, Italian) and JP (G4BJ08) discs: ss/<language>/
// *.dat and *.tpl, ss/Cmn (save screens, item and weapon pictures), etc (core.das: the HUD; memcard.das; logos).
// Every picture of those files was looked at; kept are the ones made of letters (also with a button symbol or on a
// board): menu entries, labels, help lines, results, warnings. Not kept: icons, item, map and weapon pictures,
// frames, bars, backgrounds, logos, digits and number signs, the letter fonts (font/*.fnt) and the full-screen
// pages of the files (playing manual, letters, title art). A word is its texels' hash (the first part of Dolphin's
// name), its size and its format, with any palette. The comment says where it is: file and discs (eu with the
// languages E G F S I when not in all).
#pragma once

#include <cstdint>

namespace re4gx {

struct KeptTexture {
  std::uint64_t tex;
  std::uint16_t width, height;
  std::uint8_t format;  // Dolphin's format number (2 IA4, 3 IA8, 6 RGBA8, 8 C4, 9 C8, 14 CMPR)
};

// 677 pictures, sorted by tex, width, height, format
constexpr KeptTexture kMenuWords[] = {
    {0x006b279d64f4b0ffull, 105, 21, 3}, // save: jp eu G
    {0x00a5c9e305c65a9full, 184, 48, 8}, // id400: eu S
    {0x01560fa554e6517eull, 147, 26, 2}, // option: debug us jp eu E
    {0x0160a3f9b816e9c6ull, 112, 24, 8}, // ss_map: eu F
    {0x03258ec88fe94a40ull, 47, 26, 2}, // option: eu F
    {0x034c4ababd3971a4ull, 265, 31, 2}, // option: eu F
    {0x03a5ff8f1c44bd05ull, 95, 17, 8}, // ss_shop: debug us eu E
    {0x03df998e8839b361ull, 167, 21, 8}, // chap01: eu G; chap02: eu G; chap03: eu G; chap04: eu G; chap05: eu G; cha...
    {0x03fd2573f41831b9ull, 61, 20, 8}, // title: eu S
    {0x04284afd6fad8112ull, 89, 26, 2}, // option: eu G
    {0x047832ea44b448aeull, 98, 27, 8}, // chap01: jp; chap02: jp; chap03: jp; chap04: jp; chap05: jp; chap06: jp; ch...
    {0x04c6447d4d30c5c6ull, 144, 24, 8}, // ss_map: eu G
    {0x04fba78dc3a75761ull, 131, 19, 3}, // save: jp eu F
    {0x053331dc733ca6b6ull, 528, 132, 9}, // title: jp
    {0x05a72d2191f8e9b7ull, 85, 16, 8}, // ss_cmmn: eu G; title: eu G
    {0x067c8b69941b0729ull, 62, 18, 3}, // save: jp eu FG
    {0x06af90c815a774daull, 132, 25, 3}, // save: debug us
    {0x06e17e3e1cc08655ull, 144, 24, 8}, // ss_item: eu G; ss_map: eu G
    {0x070495efdd3ab1eeull, 175, 26, 2}, // option: eu I
    {0x075fc624a0771462ull, 472, 32, 14}, // ss_map: eu G
    {0x076d714d02bb504aull, 319, 23, 14}, // chap06: eu I
    {0x0859620f1fed1452ull, 89, 26, 2}, // option: eu I
    {0x08c241e8e021f468ull, 142, 32, 8}, // ss_cmmn: jp; title: jp
    {0x097310588711ba71ull, 96, 32, 8}, // ss_cmmn: eu G; title: eu G
    {0x0b119dd25aac0dd7ull, 64, 19, 3}, // save: jp eu G
    {0x0b14a224bc832546ull, 176, 26, 14}, // option: eu E
    {0x0b3612d4e3a04ec0ull, 160, 24, 8}, // ss_shop: eu S
    {0x0b58c2ddaf3a1ab6ull, 85, 25, 2}, // option: debug us jp eu E
    {0x0c1799fa1d356adaull, 527, 47, 14}, // option: eu S
    {0x0c1da3b442a1e0a6ull, 431, 27, 3}, // option: eu I
    {0x0c8587f70634eb3dull, 128, 24, 8}, // ss_cmmn: eu S; title: eu S
    {0x0cb99fce2cfeda75ull, 176, 24, 8}, // omk_t0: eu G; omk_t1: eu G
    {0x0d057458ced8f8afull, 48, 48, 14}, // core: eu
    {0x0d3c0e64a3540f73ull, 144, 24, 8}, // ss_map: eu I
    {0x0dbc8fe09b02dd2eull, 90, 14, 8}, // title: eu
    {0x0e4d2cdcd8b5b48dull, 168, 24, 3}, // save: jp
    {0x0e74a4b3063c1b61ull, 472, 32, 14}, // ss_map: eu F
    {0x0e9275bc17bf6392ull, 123, 20, 3}, // save: jp eu G
    {0x0eaace0b12e92e3dull, 282, 52, 9}, // id22c: jp
    {0x0eb0882edddf0940ull, 121, 21, 3}, // save: jp eu I
    {0x0f7d2a34e35e33fdull, 128, 24, 8}, // ss_cmmn: eu G; title: eu G
    {0x0feaa4a36d657f50ull, 122, 22, 3}, // save: debug us
    {0x0ff6415068b3cdd1ull, 144, 24, 8}, // ss_map: eu F
    {0x1022040da8c9eb90ull, 139, 21, 3}, // save: eu S
    {0x108f9139ef1dd7faull, 112, 24, 8}, // ss_map: eu G
    {0x10df035ad738c5e7ull, 512, 372, 3}, // save: debug us jp eu J
    {0x10e807d99baefc5bull, 57, 27, 8}, // ss_shop: jp
    {0x110babe70b8548f6ull, 88, 16, 8}, // ss_cmmn: eu S; title: eu S
    {0x11354a1f3a926830ull, 144, 24, 8}, // ss_map: eu G
    {0x117815e2cf1531a8ull, 80, 16, 8}, // ss_cmmn: eu G; title: eu G
    {0x119d6a748b8c689eull, 144, 24, 8}, // ss_map: eu G
    {0x11af0b251db2399dull, 160, 14, 8}, // title: eu F
    {0x12f848c212cdbab9ull, 528, 132, 9}, // title: eu F
    {0x12ffb60a34880cc9ull, 100, 37, 8}, // ss_shop: eu S
    {0x133d7fe4a1a1b73eull, 128, 24, 8}, // ss_cmmn: eu G; title: eu G
    {0x13ee9633e6abb9a3ull, 57, 19, 8}, // ss_shop: jp
    {0x15470370fe3f4a91ull, 49, 32, 8}, // ss_shop: debug us eu E
    {0x1573cee5fcd06096ull, 148, 31, 2}, // option: debug us jp eu E
    {0x15f30e47c2924a6dull, 239, 31, 2}, // option: eu G
    {0x166cc395f43f8d65ull, 129, 26, 2}, // option: eu F
    {0x16db71c7b5c99020ull, 472, 32, 14}, // ss_map: eu G
    {0x170243bc43013b43ull, 528, 132, 9}, // title: eu G
    {0x170d099cf172becdull, 188, 14, 8}, // title: eu F
    {0x17f42cbfccbf9ee0ull, 218, 20, 8}, // chap01: eu I; chap02: eu I; chap03: eu I; chap04: eu I; chap05: eu I; cha...
    {0x17f77bbda065ee6aull, 48, 25, 3}, // save: debug us
    {0x182a52437e958951ull, 75, 16, 8}, // ss_cmmn: eu F; title: eu F
    {0x192ed34750c54b51ull, 56, 24, 8}, // ss_cmmn: eu I; title: eu I
    {0x19443651a3cc9ef2ull, 168, 24, 3}, // save: eu S
    {0x19ccd0e1ad9b2ba3ull, 128, 24, 8}, // ss_cmmn: eu F; title: eu F
    {0x1a0e94fb4d5908d6ull, 320, 32, 8}, // title: eu S
    {0x1a1ae8131c4117feull, 152, 24, 8}, // chap01: debug us jp eu E; chap02: debug us jp eu E; chap03: debug us jp e...
    {0x1aa4fc0d796a9624ull, 100, 37, 8}, // ss_shop: eu F
    {0x1ac24d4dc7299edbull, 60, 20, 8}, // chap01: debug us jp eu EFS; chap02: debug us jp eu EFS; chap03: debug us j...
    {0x1b05dbf5951f7adcull, 60, 18, 3}, // save: jp eu I
    {0x1b3a5334a13df2cfull, 104, 24, 8}, // ss_item: eu G
    {0x1c4b589308185495ull, 320, 32, 8}, // ss_cmmn: eu S
    {0x1c53f86066cf7297ull, 113, 14, 8}, // title: jp eu E
    {0x1cd83454de3d67d3ull, 258, 27, 8}, // chap01: eu G; chap02: eu G; chap03: eu G; chap04: eu G; chap05: eu G; cha...
    {0x1d951215e41fc1f5ull, 328, 32, 14}, // ss_cap: eu I
    {0x1e1695e0cd02d745ull, 536, 104, 14}, // ss_shop: eu G
    {0x1e6236c4420a5e2aull, 96, 19, 8}, // ss_cmmn: eu I; title: eu I
    {0x1e6d25173720787bull, 46, 16, 8}, // ss_cmmn: eu F; title: eu F
    {0x1e73687bac194903ull, 144, 24, 8}, // ss_map: eu I
    {0x1f21699772d44c72ull, 118, 24, 3}, // save: debug us jp eu EJ
    {0x1fa33ca922527ee0ull, 380, 34, 9}, // id22c: debug us eu E
    {0x1fd18b4e918de605ull, 77, 27, 8}, // chap01: jp; chap02: jp; chap03: jp; chap04: jp; chap05: jp; chap06: jp; ch...
    {0x1ff1b841d6edb1c3ull, 137, 21, 8}, // chap01: eu G; chap02: eu G; chap03: eu G; chap04: eu G; chap05: eu G; cha...
    {0x20083b4d085105d9ull, 35, 17, 8}, // core: debug us jp eu
    {0x2043ab416edf4ca4ull, 190, 26, 2}, // option: eu S
    {0x2045c0edb89c174bull, 81, 14, 8}, // title: eu I
    {0x20f9ab1fbeb9408dull, 114, 17, 8}, // ss_map: debug us eu E
    {0x214bbff4875bc4b3ull, 472, 32, 14}, // ss_map: eu F
    {0x21a2f642a9e9aefeull, 100, 37, 8}, // ss_shop: eu G
    {0x2209b6c12033842aull, 51, 25, 8}, // ss_map: jp
    {0x2252bc20c2d67696ull, 152, 20, 8}, // chapter: debug us jp eu E
    {0x2293581f4f059cbfull, 98, 14, 8}, // title: eu
    {0x22e1ab776d6cfbf9ull, 128, 24, 8}, // ss_cmmn: eu S; title: eu S
    {0x2316d2fcac81a681ull, 400, 52, 14}, // id22c: eu G
    {0x2363a02020de74b1ull, 85, 16, 8}, // ss_cmmn: eu S; title: eu S
    {0x23c0aac7af8bb9c6ull, 435, 42, 14}, // option: eu G
    {0x23cdafd6838a429cull, 144, 24, 8}, // ss_map: eu F
    {0x23f2146b3eb990bdull, 128, 24, 8}, // ss_cmmn: eu G; title: eu G
    {0x24b774411e263755ull, 112, 24, 8}, // ss_map: eu I
    {0x254ba964422697bdull, 46, 16, 8}, // ss_cmmn: eu G; title: eu G
    {0x25868d81f8e651f0ull, 490, 32, 9}, // save: eu S
    {0x2617fbcc574ac0deull, 537, 39, 14}, // option: eu G
    {0x26a68f82a712fca5ull, 142, 28, 8}, // id400: debug jp
    {0x26c719152800dcaaull, 139, 14, 8}, // title: jp
    {0x26d71980db0dab1aull, 212, 20, 8}, // chap01: eu F; chap02: eu F; chap03: eu F; chap04: eu F; chap05: eu F; cha...
    {0x271a1604dc64e207ull, 640, 336, 14}, // id333: eu F
    {0x2764dbde82a8accfull, 104, 22, 8}, // ss_cmmn: eu S
    {0x282b44335e11796dull, 89, 26, 2}, // option: eu S
    {0x28c15d874d4f2b65ull, 84, 16, 8}, // ss_cmmn: eu S; title: eu S
    {0x2946cfa60deb096eull, 171, 26, 2}, // option: debug us jp eu E
    {0x295e9b5249433a41ull, 84, 16, 8}, // ss_item: debug us eu E; ss_map: debug us eu E
    {0x2a0cc530e2d902cfull, 257, 24, 14}, // ss_map: debug us eu E
    {0x2a9fa032b56eb7b8ull, 78, 20, 8}, // ss_map: jp
    {0x2ad5ff60b0d99ad0ull, 40, 24, 8}, // ss_cmmn: jp; title: jp
    {0x2b9d8e5e9ef4c951ull, 104, 22, 8}, // ss_cmmn: eu G; title: eu G
    {0x2c13fdccd0ed6b52ull, 112, 20, 8}, // ss_map: debug us jp eu E
    {0x2c64675c7907e826ull, 65, 17, 8}, // core: debug us jp eu
    {0x2c70c31d7b68427full, 104, 24, 8}, // ss_shop: eu F
    {0x2ca67644272d9520ull, 183, 14, 8}, // title: eu G
    {0x2d659e053d13172dull, 490, 32, 9}, // save: jp
    {0x2d924f8f551d7530ull, 56, 24, 8}, // ss_cmmn: eu S; title: eu S
    {0x2d9be8b6341f5640ull, 528, 132, 9}, // title: eu I
    {0x2dc7ba5d34000ff1ull, 131, 24, 3}, // save: jp
    {0x2df52fae6acaca6aull, 190, 26, 2}, // option: eu F
    {0x2eb6c169aefffe6bull, 31, 31, 2}, // option: eu S
    {0x2ec167acb4e13dd9ull, 99, 14, 8}, // title: eu I
    {0x2edc747f6ef8c1d0ull, 400, 52, 14}, // id22c: eu S
    {0x2fd6cc0db64d41b5ull, 376, 32, 8}, // id400: eu F
    {0x2ff20c73068d1ab0ull, 39, 20, 8}, // ss_cmmn: jp; title: jp
    {0x2ffbe0320ad8d63cull, 58, 20, 8}, // ss_cmmn: jp; title: jp
    {0x303358cf1fb0f90bull, 90, 14, 8}, // title: eu I
    {0x305341e0aeda7d0bull, 490, 32, 9}, // save: jp eu I
    {0x314b9d225061ba48ull, 134, 18, 3}, // save: jp eu I
    {0x319c82ec44db4fceull, 313, 31, 2}, // option: eu S
    {0x31e845163409b0a7ull, 320, 32, 8}, // ss_cmmn: eu I; title: eu I
    {0x328b71358d2928cfull, 135, 20, 3}, // save: jp
    {0x329956864972202aull, 177, 14, 8}, // title: eu I
    {0x33018303972ead24ull, 128, 24, 8}, // ss_cmmn: eu I; title: eu I
    {0x331dfa6cbe295563ull, 490, 32, 9}, // save: eu I
    {0x336350586de56560ull, 176, 56, 8}, // id400: eu I
    {0x33dd60269bd873bdull, 112, 24, 8}, // ss_map: eu I
    {0x353a6d0924d24dabull, 144, 24, 8}, // ss_map: eu S
    {0x357aad6a96d86dd6ull, 352, 32, 8}, // id400: eu F
    {0x35d7e51b61d972a7ull, 57, 26, 2}, // option: eu I
    {0x35f6c5ed236eeb8eull, 160, 136, 8}, // ss_map: eu F
    {0x3644bd93791d9a98ull, 344, 16, 8}, // title: jp eu FGIS
    {0x36b16dee0e23adb6ull, 64, 26, 2}, // option: eu I
    {0x37308c1f3247ff27ull, 128, 24, 8}, // ss_cmmn: eu I; title: eu I
    {0x37d214cd0f45e6bdull, 48, 24, 8}, // option: eu F
    {0x37d2a2fff41f42e0ull, 523, 175, 8}, // result: eu G
    {0x37de801a39f0d672ull, 412, 42, 14}, // option: eu I
    {0x3869a0e3b2db9c01ull, 376, 32, 8}, // id400: eu I
    {0x39030fe1302f0422ull, 64, 24, 8}, // ss_cmmn: jp; title: jp
    {0x3928037210e2844dull, 86, 14, 8}, // title: eu S
    {0x39956a6ec1222a40ull, 72, 24, 8}, // option: eu F
    {0x3a33f1da862abd87ull, 523, 175, 8}, // result: debug us eu E
    {0x3a9cd412f45d9becull, 160, 24, 8}, // ss_shop: eu G
    {0x3ab171a7a470a8fcull, 160, 136, 8}, // ss_map: eu S
    {0x3ae79b12a5d3c9c0ull, 228, 14, 8}, // title: eu S
    {0x3af9f75cde957095ull, 48, 24, 8}, // option: eu G
    {0x3ba17870eb071c89ull, 145, 24, 14}, // ss_cap: debug us eu E
    {0x3bb3829dcb68e06bull, 400, 52, 14}, // id22c: eu F
    {0x3bb41eb2538be9a1ull, 472, 32, 14}, // ss_map: eu I
    {0x3c402f4f14b8b7dbull, 77, 21, 9}, // ss_cmmn: jp; title: jp
    {0x3c42d7d13663e50aull, 79, 25, 2}, // option: debug us jp eu E
    {0x3cbaae5471f62325ull, 128, 24, 8}, // omk_t0: eu F; omk_t1: eu F
    {0x3d33825192904450ull, 27, 26, 8}, // ss_map: jp
    {0x3d7c5f8d23caa57cull, 64, 26, 2}, // option: eu G
    {0x3e181bc3cfb8c68eull, 54, 17, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0x3e3acad7c6d1f6e4ull, 523, 175, 8}, // result: eu I
    {0x3fc333876fcc479aull, 432, 80, 9}, // id22c: eu G
    {0x406090bd40785e9bull, 56, 21, 8}, // ss_cmmn: jp; title: jp
    {0x40977070cb210376ull, 128, 22, 8}, // chap01: eu F; chap02: eu F; chap03: eu F; chap04: eu F; chap05: eu F; cha...
    {0x41bf5e4a16df1d1dull, 376, 32, 8}, // id400: eu S
    {0x42502086756a25b9ull, 88, 16, 8}, // ss_cmmn: eu I; title: eu I
    {0x4256786c3f05d22eull, 162, 26, 2}, // option: debug us jp eu E
    {0x42f3d352201e1582ull, 85, 16, 8}, // ss_cmmn: eu I; title: eu I
    {0x43eefdc710c72516ull, 19, 19, 8}, // ss_item: jp; ss_map: jp
    {0x44911b492e45f8e2ull, 80, 16, 8}, // ss_cmmn: eu F; title: eu F
    {0x44ba42ddb273f642ull, 85, 16, 8}, // ss_cmmn: eu F; title: eu F
    {0x451253f927aa9b41ull, 165, 24, 3}, // save: jp eu S
    {0x455920c6e2476a55ull, 600, 101, 9}, // save: jp eu F
    {0x456acd03680c9635ull, 125, 20, 3}, // save: jp
    {0x46383989e1783375ull, 128, 24, 8}, // ss_cmmn: eu S; title: eu S
    {0x463fbdf76c5ef8fcull, 104, 24, 8}, // ss_item: eu F
    {0x4788097c907aae4cull, 186, 19, 8}, // chap01: eu I; chap02: eu I; chap03: eu I; chap04: eu I; chap05: eu I; cha...
    {0x48471570241fa769ull, 55, 14, 8}, // title: debug us jp eu E
    {0x499779f7ba058712ull, 104, 24, 8}, // ss_shop: eu S
    {0x49d84ccd569812fcull, 400, 24, 9}, // ss_map: jp
    {0x49fadbe83651dfd1ull, 85, 19, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0x4ae277b691b08a0cull, 164, 21, 3}, // save: jp eu F
    {0x4b1a5e3d06594190ull, 101, 31, 2}, // option: eu S
    {0x4b51cfe7e550ddffull, 450, 24, 14}, // ss_map: debug us eu E
    {0x4b5d5c1f1ec8619dull, 112, 24, 8}, // ss_map: eu F
    {0x4bd80d6381f11c7cull, 472, 32, 14}, // ss_map: eu G
    {0x4bfcbb507cf42fdeull, 310, 27, 2}, // option: eu G
    {0x4cd9d0cd857bb04full, 216, 42, 14}, // option: eu S
    {0x4d0f75d701807d30ull, 536, 251, 8}, // option: debug us jp eu E
    {0x4d399667c4d52bc2ull, 36, 26, 2}, // option: eu S
    {0x4d444eb54d43e3d4ull, 70, 31, 8}, // ss_cmmn: jp; title: jp
    {0x4d5eb911382fe844ull, 191, 20, 8}, // ss_map: eu FGIS
    {0x4dd26e2b88eaebbbull, 104, 24, 8}, // ss_item: eu I
    {0x4df34a09ab301757ull, 368, 40, 9}, // option: eu G
    {0x4e7cdfe79959e073ull, 122, 24, 3}, // save: debug us
    {0x4eb54fae02573940ull, 192, 48, 2}, // option: debug us
    {0x4f3b389ea2069068ull, 128, 24, 8}, // ss_cmmn: eu F; title: eu F
    {0x4f7123afa033082bull, 96, 32, 8}, // ss_cmmn: eu I; title: eu I
    {0x4f7cf0275b7d9ad4ull, 129, 19, 8}, // chap01: eu I; chap02: eu I; chap03: eu I; chap04: eu I; chap05: eu I; cha...
    {0x500933d1a76ff670ull, 144, 24, 8}, // ss_item: eu IS; ss_map: eu IS
    {0x516e5875f41b4f13ull, 512, 119, 14}, // id22c: debug us eu E
    {0x51792442f2423697ull, 400, 52, 14}, // id22c: us jp eu E
    {0x51cfc5ba4fd33557ull, 352, 32, 8}, // id400: eu I
    {0x51e5a4e23d61d200ull, 528, 132, 9}, // title: debug us eu E
    {0x520ad91517dd2c3cull, 193, 14, 8}, // title: eu E
    {0x531bbf14eddd1280ull, 248, 26, 8}, // option: eu S
    {0x5363b32e454934daull, 46, 16, 8}, // ss_cmmn: eu IS; title: eu IS
    {0x53b017028cefd1d7ull, 297, 26, 2}, // option: eu F
    {0x5494b48ef2e16fddull, 352, 32, 8}, // id400: eu G
    {0x54e2d20d7899654aull, 104, 24, 8}, // ss_item: eu S
    {0x55233c5fe387e8ecull, 125, 26, 2}, // option: debug us jp eu E
    {0x5525b178b5537c72ull, 176, 24, 8}, // omk_t0: eu F; omk_t1: eu F
    {0x55cd85dadcf6e699ull, 512, 119, 14}, // id22c: eu I
    {0x5603de1387f11d17ull, 111, 21, 3}, // save: jp eu F
    {0x5662c454f6b6395eull, 264, 29, 8}, // chap01: eu I; chap02: eu I; chap03: eu I; chap04: eu I; chap05: eu I; cha...
    {0x568810273bfc25bcull, 98, 180, 9}, // ss_map: jp
    {0x576a609e527c2cd2ull, 104, 19, 3}, // save: eu F
    {0x57b5432247ec27ebull, 85, 16, 8}, // ss_cmmn: eu F; title: eu F
    {0x57c98fc5587a4c79ull, 344, 16, 8}, // title: eu E
    {0x58c6dd1f4c916762ull, 128, 24, 8}, // omk_t0: eu I; omk_t1: eu I
    {0x58e029b73ee4d390ull, 512, 119, 14}, // id22c: eu S
    {0x59914177d1a4bf38ull, 36, 20, 8}, // ss_cmmn: jp; title: jp
    {0x59a0d15d78b4c707ull, 184, 48, 8}, // id400: eu F
    {0x59b09fe09534b7f9ull, 220, 30, 6}, // core: debug us jp eu
    {0x5a1a14e6cecb527aull, 73, 17, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0x5a8f54c5ad578dd5ull, 199, 20, 8}, // chap01: eu S; chap02: eu S; chap03: eu S; chap04: eu S; chap05: eu S; cha...
    {0x5aa917174108bdc6ull, 84, 16, 8}, // ss_cmmn: eu F; title: eu F
    {0x5abc67433d65efd5ull, 176, 56, 8}, // id400: eu G
    {0x5af945b8e074a692ull, 149, 14, 8}, // title: debug us jp eu E
    {0x5b75729fc2182b45ull, 44, 18, 3}, // save: jp eu IS
    {0x5bbf2cefed581e6eull, 400, 52, 14}, // id22c: eu I
    {0x5bcf3a0434b79e75ull, 160, 24, 8}, // ss_shop: eu S
    {0x5be7bfb148cda59full, 82, 14, 8}, // title: eu I
    {0x5c060344136530d0ull, 490, 32, 9}, // save: eu F
    {0x5c16d3fbf02cbbb0ull, 104, 24, 8}, // ss_shop: eu F
    {0x5c2136c5fd06ccc9ull, 528, 132, 9}, // title: debug us eu E
    {0x5d520e3919f4a332ull, 210, 26, 2}, // option: eu I
    {0x5e0645d375bdc770ull, 176, 26, 14}, // option: eu F
    {0x5e4c506e28c07159ull, 136, 142, 8}, // ss_map: debug us eu E
    {0x5effb2bf30f1b579ull, 165, 24, 3}, // save: jp eu I
    {0x60cf80b6117ab668ull, 54, 23, 3}, // save: jp eu S
    {0x60dc59a79be33539ull, 85, 16, 8}, // ss_cmmn: eu I; title: eu I
    {0x615457396eefabdcull, 96, 18, 3}, // save: debug us jp eu EJ
    {0x61c73c02e6e9f045ull, 48, 25, 3}, // save: debug us
    {0x61f6574a07e70f1full, 476, 53, 14}, // option: eu F
    {0x620e53ab349aa4baull, 472, 32, 14}, // ss_map: eu S
    {0x622cbc93fda68d9aull, 528, 132, 9}, // title: eu I
    {0x62add5e27e2514f8ull, 100, 14, 8}, // title: eu F
    {0x631e33d7d4b0aa55ull, 336, 27, 8}, // title: debug us eu E
    {0x6326b67c11eac047ull, 139, 21, 3}, // save: jp
    {0x6437e82041448148ull, 96, 19, 8}, // ss_cmmn: eu G; title: eu G
    {0x64e2bf68a0a170baull, 92, 29, 8}, // ss_shop: debug us eu E
    {0x65d9b1ca02dad51aull, 200, 120, 8}, // ss_map: eu F
    {0x6656170a01a6f26dull, 64, 19, 3}, // save: jp eu F
    {0x66f3c51618b434eaull, 536, 251, 8}, // option: eu S
    {0x6726aafbec2d6eb7ull, 523, 175, 8}, // result: jp
    {0x67ddbacf7027e19aull, 301, 33, 8}, // id400: debug us jp eu E
    {0x67e78a555b62fb9full, 541, 47, 14}, // option: eu S
    {0x68da23a45975e1a7ull, 298, 42, 14}, // option: eu G
    {0x697c2b5ab7a049d5ull, 214, 21, 14}, // id400: debug us jp eu E
    {0x6a9bd0dd564680ebull, 88, 22, 3}, // save: debug us
    {0x6ae1862897c7d78bull, 536, 104, 14}, // ss_shop: eu I
    {0x6b0fa13204862e1dull, 124, 120, 14}, // ss_map: debug us eu E
    {0x6ba2211df5173de7ull, 80, 16, 8}, // ss_cmmn: eu I; title: eu I
    {0x6bc0b175b7341f85ull, 448, 42, 14}, // option: eu G
    {0x6bce670b39674fdcull, 444, 42, 8}, // option: debug us jp eu E
    {0x6c26be60edaee1faull, 48, 48, 14}, // core: eu
    {0x6c2db2c50448d599ull, 176, 24, 8}, // omk_t0: eu I; omk_t1: eu I
    {0x6c42cc870638743aull, 536, 251, 8}, // option: eu G
    {0x6c540fef05839aaaull, 600, 100, 9}, // save: debug us jp eu E
    {0x6c5609d63ea69e40ull, 112, 24, 8}, // ss_map: eu F
    {0x6c64bc998b14f333ull, 128, 24, 8}, // ss_cmmn: eu I; title: eu I
    {0x6d5d02d21cf80d06ull, 193, 26, 2}, // option: eu S
    {0x6de3bc4f2068d260ull, 140, 29, 8}, // ss_map: debug us eu E
    {0x6e1935f096a5e927ull, 36, 19, 3}, // save: jp eu FS
    {0x6f267a2862ea4c2bull, 129, 31, 2}, // option: eu S
    {0x6f508b957bed73d1ull, 200, 26, 2}, // option: eu S
    {0x70283ed961b5a4d9ull, 328, 32, 14}, // ss_cap: eu F
    {0x7075c935cb762a03ull, 286, 42, 8}, // option: debug us jp eu E
    {0x70d128e54d53a3f0ull, 75, 16, 8}, // ss_cmmn: eu G; title: eu G
    {0x711b5b4aa4768dbaull, 200, 120, 8}, // ss_map: eu I
    {0x717986ea57e36801ull, 112, 24, 8}, // ss_map: eu I
    {0x71a5005f8332659dull, 491, 36, 2}, // option: eu F
    {0x72780aade71c9bc2ull, 308, 26, 2}, // option: eu I
    {0x727fe5941e5453c7ull, 168, 20, 8}, // chap01: eu G; chap02: eu G; chap03: eu G; chap04: eu G; chap05: eu G; cha...
    {0x7289b4f1874e9a29ull, 504, 42, 14}, // option: eu F
    {0x730323919d395471ull, 230, 26, 2}, // option: eu S
    {0x738d4780e9db4798ull, 135, 20, 3}, // save: eu S
    {0x73f06791d2be6a53ull, 432, 80, 9}, // id22c: eu I
    {0x743abc6540ec7270ull, 160, 24, 8}, // ss_shop: eu F
    {0x753ada6262f377a9ull, 236, 26, 2}, // option: eu S
    {0x75be4f4d6f040e99ull, 102, 14, 8}, // title: eu G
    {0x75e09ec8de81ef6cull, 77, 17, 8}, // core: us jp eu
    {0x76353de418254033ull, 56, 24, 8}, // ss_cmmn: eu F; title: eu F
    {0x76591886dd1acef7ull, 35, 26, 2}, // option: debug us jp eu E
    {0x7776bd63c7e47b6dull, 490, 32, 9}, // save: jp
    {0x779cd227c6623a21ull, 319, 23, 14}, // chap06: eu S
    {0x77f4bea8b9813180ull, 224, 24, 14}, // ss_cap: eu I
    {0x7822de9f65862d3eull, 536, 251, 8}, // option: eu I
    {0x783bd5d48df30e9cull, 176, 24, 8}, // omk_t0: eu S; omk_t1: eu S
    {0x7964a32caaedb5ccull, 96, 20, 8}, // chap01: debug us eu E; chap02: debug us eu E; chap03: debug us eu E; chap0...
    {0x79cfe10760bfb55eull, 328, 32, 14}, // ss_cap: eu S
    {0x79d204abaa78feb9ull, 153, 26, 2}, // option: eu G
    {0x79f33ebbca114c48ull, 47, 23, 8}, // core: debug us jp eu
    {0x7a215d7f7f456876ull, 486, 31, 9}, // save: debug us jp eu EJ
    {0x7a9e1e10aacf651dull, 490, 32, 9}, // save: jp
    {0x7a9fa23a3584f567ull, 528, 132, 9}, // title: eu G
    {0x7ac5b1953ceb274eull, 472, 32, 14}, // ss_map: eu I
    {0x7b50ac53ad4bd946ull, 99, 32, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0x7bbbae1a7f29947cull, 44, 16, 8}, // ss_cmmn: debug us eu; title: debug us eu
    {0x7be698a89b6e2c66ull, 112, 24, 8}, // ss_map: eu S
    {0x7c8cd59057ffaee3ull, 132, 25, 3}, // save: debug us jp eu EJ
    {0x7ca77898f0b2a9d1ull, 104, 19, 3}, // save: jp
    {0x7ccdd41a1aa4b78dull, 366, 40, 9}, // option: eu F
    {0x7d3c3dadeed914c3ull, 104, 24, 8}, // ss_shop: eu S
    {0x7d5d6f78757a8bcfull, 48, 24, 8}, // core: debug us jp eu
    {0x7daa397646e53026ull, 74, 29, 8}, // ss_shop: debug us eu E
    {0x7e1fa95f3bf2d96full, 47, 17, 8}, // core: debug
    {0x7e6804f91a41aee1ull, 32, 16, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0x7fa6ed8da34bed32ull, 640, 360, 8}, // id333: debug us jp eu E
    {0x7fa8f2f83163a8d4ull, 72, 24, 8}, // option: eu G
    {0x80039a1a3d0f1da0ull, 44, 16, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0x8035eaedf335fc22ull, 54, 19, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0x8082a53b2f9fee0full, 472, 32, 14}, // ss_map: eu S
    {0x809d2c0039b73177ull, 319, 23, 14}, // chap06: debug us jp eu E
    {0x819890543605920bull, 48, 26, 2}, // option: eu G
    {0x81ddbe8b2ea9e565ull, 486, 32, 9}, // save: debug us
    {0x82858981f6c725efull, 88, 31, 2}, // option: eu F
    {0x838559713fa6ef04ull, 72, 24, 8}, // option: eu IS
    {0x838c17a5296a57b2ull, 88, 26, 2}, // option: eu I
    {0x83c4855df39a8212ull, 34, 16, 8}, // ss_cmmn: eu I; title: eu I
    {0x84a56de9f285d08eull, 98, 22, 3}, // save: debug us
    {0x84b995f8868bd66full, 487, 31, 9}, // save: debug us jp eu EJ
    {0x84d2685aaf653ff9ull, 104, 24, 8}, // ss_shop: eu I
    {0x854a73da4449ef48ull, 21, 19, 8}, // ss_item: jp
    {0x855334eb692544bcull, 97, 20, 8}, // ss_map: jp
    {0x8560efb5e0cd8554ull, 65, 20, 3}, // save: debug us jp eu EJ
    {0x856b40c60a594039ull, 56, 24, 8}, // ss_cmmn: jp; title: jp
    {0x8620490b9ef9a94full, 36, 21, 8}, // option: debug us jp eu E
    {0x865540e93e86b9aeull, 186, 26, 2}, // option: eu G
    {0x8729a3cad78c790dull, 600, 101, 9}, // save: eu S
    {0x877938074173ef4full, 65, 20, 3}, // save: jp eu J
    {0x891bb938e3cbd4c9ull, 476, 308, 8}, // ss_term: debug us jp eu
    {0x89ec07f8409f07b9ull, 130, 14, 8}, // title: eu G
    {0x8a3b05cad63de110ull, 86, 19, 8}, // ss_item: debug us eu E
    {0x8a513df9deb5ca46ull, 490, 32, 9}, // save: jp
    {0x8a7b40c3e3273c92ull, 257, 32, 8}, // chap01: eu S; chap02: eu S; chap03: eu S; chap04: eu S; chap05: eu S; cha...
    {0x8a87a69f00898f87ull, 64, 64, 8}, // core: debug us jp
    {0x8aa7a78200bafdd3ull, 126, 18, 3}, // save: jp eu F
    {0x8aaaba7fabf5e495ull, 472, 32, 14}, // ss_map: eu S
    {0x8b5f9a7b440afe2aull, 228, 26, 2}, // option: eu G
    {0x8bab5d63e8e9d578ull, 70, 14, 8}, // title: eu G
    {0x8c16a03652f87dc1ull, 339, 26, 2}, // option: debug us jp eu E
    {0x8c97608a26e3bcd2ull, 144, 24, 8}, // ss_map: eu F
    {0x8cf11e61708e71f9ull, 451, 42, 14}, // option: eu S
    {0x8e7634b41b9cea94ull, 190, 42, 14}, // option: debug us jp eu E
    {0x8e7c34b0b632a75aull, 560, 180, 14}, // title: debug us jp eu
    {0x8e842f8af8c0d87eull, 270, 24, 14}, // ss_cap: jp
    {0x8f9c2f4d45efc6faull, 96, 32, 8}, // ss_cmmn: eu F; title: eu F
    {0x8fe5f014be2a9fc2ull, 160, 24, 8}, // ss_shop: eu I
    {0x9019399356010b33ull, 100, 37, 8}, // ss_shop: eu I
    {0x906e03179ee005b2ull, 185, 14, 8}, // title: debug us eu
    {0x915770d316a55a42ull, 115, 19, 3}, // save: jp eu F
    {0x917e1667c813a503ull, 226, 26, 2}, // option: debug us jp eu E
    {0x91a931c711a7e1baull, 112, 24, 8}, // ss_map: eu G
    {0x926d98ba684658deull, 99, 35, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0x927ac43141737185ull, 34, 16, 8}, // ss_cmmn: eu G; title: eu G
    {0x928f24ab534361f2ull, 144, 24, 8}, // ss_map: eu I
    {0x9291cb18f92c6dceull, 144, 24, 8}, // ss_map: eu S
    {0x92eac8c7a7b16f03ull, 60, 14, 8}, // title: eu S
    {0x9323906c2c49818bull, 214, 20, 8}, // chap01: eu F; chap02: eu F; chap03: eu F; chap04: eu F; chap05: eu F; cha...
    {0x9324832e7c0db368ull, 144, 24, 8}, // ss_map: eu I
    {0x936712624ee5e50eull, 35, 26, 2}, // option: eu I
    {0x937af4003a7abe8aull, 376, 32, 8}, // id400: eu G
    {0x93bef42647260ecdull, 595, 100, 9}, // save: debug us
    {0x93f4de840c652567ull, 432, 80, 9}, // id22c: eu I
    {0x946b611417b25dd3ull, 48, 48, 14}, // core: eu
    {0x948033dfd9c03ac2ull, 432, 80, 9}, // id22c: eu F
    {0x94d73f393c46657aull, 104, 24, 8}, // ss_shop: eu G
    {0x9551b801007f8a24ull, 59, 18, 9}, // core: debug us jp eu
    {0x9578749b00c62cf4ull, 230, 26, 8}, // option: debug us jp eu E
    {0x9590e0c44cbf9b9aull, 176, 56, 8}, // id400: eu S
    {0x959a864948937a1eull, 144, 24, 8}, // ss_map: eu F
    {0x95ec2d80fe87ce52ull, 119, 18, 3}, // save: jp eu G
    {0x9608c37dfbe86d17ull, 106, 23, 3}, // save: debug us jp eu EJ
    {0x96e048a3be941cf2ull, 48, 48, 14}, // core: eu
    {0x9769ff33c6ff32aaull, 128, 24, 8}, // ss_cmmn: eu F; title: eu F
    {0x97d3b637c764519cull, 75, 16, 8}, // ss_cmmn: eu I; title: eu I
    {0x988e9d4a0fa25c48ull, 56, 20, 8}, // ss_map: jp
    {0x988ee1a42f31cdc6ull, 125, 20, 3}, // save: eu S
    {0x98a14c9b84dd770full, 51, 25, 8}, // ss_map: jp
    {0x98c6767d794be07eull, 496, 31, 2}, // option: eu S
    {0x993ead48b8050d7eull, 222, 21, 8}, // chap01: eu S; chap02: eu S; chap03: eu S; chap04: eu S; chap05: eu S; cha...
    {0x9990c5620d2f22ccull, 112, 24, 8}, // ss_map: eu G
    {0x99c303d000287505ull, 160, 24, 8}, // ss_shop: eu G
    {0x9a9df506f837bf51ull, 80, 16, 8}, // ss_cmmn: eu S; title: eu S
    {0x9c92a790da9a2d64ull, 184, 48, 8}, // id400: eu G
    {0x9cb8becc2bb742f8ull, 128, 24, 8}, // ss_cmmn: eu S; title: eu S
    {0x9d55c627235f5640ull, 187, 30, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0x9dcd0547af0d7a78ull, 80, 30, 8}, // ss_cmmn: jp; title: jp
    {0x9df7faf6304307f6ull, 86, 17, 8}, // ss_map: debug us eu E
    {0x9e71c8fa98f96bc3ull, 128, 24, 8}, // ss_cmmn: eu I; title: eu I
    {0x9e76ccdfe4ec6e87ull, 239, 26, 2}, // option: eu I
    {0x9f3016518f1e2d5eull, 160, 136, 8}, // ss_map: eu G
    {0x9f8b2d5acde897f7ull, 161, 29, 8}, // ss_map: debug us eu E
    {0xa03ad0e9fe8f0c65ull, 333, 42, 8}, // option: debug us jp eu E
    {0xa1842a21eb4f47f3ull, 49, 26, 2}, // option: eu F
    {0xa38ade3abe66aa8dull, 106, 20, 14}, // omk_t0: us jp eu E; omk_t1: debug us jp eu E
    {0xa3cbc05f749f9e01ull, 105, 20, 3}, // save: jp eu G
    {0xa4bb1b734e278de9ull, 174, 23, 8}, // chap01: eu G; chap02: eu G; chap03: eu G; chap04: eu G; chap05: eu G; cha...
    {0xa5d28ce8a7a07a9cull, 320, 32, 8}, // ss_cmmn: eu F; title: eu F
    {0xa5eb6cfa3fb6d31bull, 305, 31, 2}, // option: eu S
    {0xa611abd441b2c2d3ull, 471, 42, 14}, // option: eu I
    {0xa632357a930e0abfull, 87, 14, 8}, // title: debug us jp eu EFGS
    {0xa658507764925f3cull, 104, 19, 8}, // ss_map: debug us eu E
    {0xa65dcf6d1ec644d0ull, 34, 16, 8}, // ss_cmmn: eu F; title: eu F
    {0xa689c35bc10af2f6ull, 142, 28, 14}, // id400: us eu E
    {0xa694a01e526e2280ull, 45, 20, 3}, // save: jp eu EJ
    {0xa6b981ebea381b05ull, 219, 24, 9}, // ss_map: jp
    {0xa6cc7676d2571fcaull, 160, 136, 8}, // ss_map: eu I
    {0xa6de0de36cb8d74dull, 125, 20, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0xa74a1496e909c54bull, 536, 104, 14}, // ss_shop: eu F
    {0xa74efda27c817e0cull, 176, 24, 8}, // omk_t0: eu S; omk_t1: eu S
    {0xa763cc5acbefa92bull, 95, 21, 8}, // ss_map: jp
    {0xa7aee40600c153e3ull, 176, 24, 8}, // omk_t0: eu G; omk_t1: eu G
    {0xa805ba9d5dcc0229ull, 320, 32, 8}, // ss_cmmn: eu G; title: eu G
    {0xa877508d95c41950ull, 188, 42, 14}, // option: eu I
    {0xa8d0ba56fc1ec7b7ull, 242, 31, 2}, // option: eu F
    {0xa8f040c1d6a7cff7ull, 640, 336, 14}, // id333: eu S
    {0xa90a9cbb20512134ull, 34, 16, 8}, // ss_cmmn: eu S; title: eu S
    {0xa954dbce7c2c4203ull, 169, 17, 8}, // chap01: eu F; chap02: eu F; chap03: eu F; chap04: eu F; chap05: eu F; cha...
    {0xa9a9195d25160fe1ull, 60, 14, 8}, // title: eu G
    {0xaab151bd2c28c404ull, 176, 26, 14}, // option: eu G
    {0xaafc114859c95420ull, 528, 132, 9}, // title: eu S
    {0xac411fe8e64eb912ull, 152, 20, 8}, // chap01: debug us eu E; chap02: debug us eu E; chap03: debug us eu E; chap...
    {0xac77407fd0a6f018ull, 144, 24, 8}, // ss_map: eu S
    {0xac934ccd7f935093ull, 248, 26, 8}, // option: eu I
    {0xacd744cb59f28133ull, 48, 24, 8}, // option: eu I
    {0xaf1a954efd17ef73ull, 167, 14, 8}, // title: eu I
    {0xaf234a92089b76d8ull, 248, 26, 8}, // option: eu G
    {0xaf5587d5439bbd92ull, 77, 27, 8}, // chap01: jp; chap02: jp; chap03: jp; chap04: jp; chap05: jp; chap06: jp; ch...
    {0xaf7f613de2994690ull, 282, 34, 9}, // id22c: debug us eu E
    {0xafe6acb9be1a9e67ull, 153, 24, 3}, // save: jp eu G
    {0xaff5926da8f3f783ull, 600, 101, 9}, // save: jp
    {0xb09186468fb729d9ull, 115, 20, 8}, // ss_cmmn: debug us eu; title: debug us eu
    {0xb09547577eb0284aull, 47, 17, 8}, // core: us jp eu
    {0xb155951044dc269eull, 103, 25, 2}, // option: eu G
    {0xb156d89cccb0c2e8ull, 512, 119, 14}, // id22c: eu G
    {0xb2121d20f1ff0cd7ull, 70, 20, 8}, // chap01: eu G; chap02: eu G; chap03: eu G; chap04: eu G; chap05: eu G; chap...
    {0xb2a087542a311eb4ull, 400, 52, 8}, // id22c: debug
    {0xb3267da9cac8685bull, 104, 22, 3}, // save: debug us
    {0xb3b5f5581d4c5d40ull, 104, 22, 8}, // ss_cmmn: eu F; title: eu F
    {0xb4279d1c3d14fd76ull, 68, 14, 8}, // title: eu I
    {0xb45d5d50677f8ee8ull, 157, 21, 14}, // ss_map: debug us eu E
    {0xb4b5f97c3a825d6eull, 53, 19, 3}, // save: jp eu G
    {0xb4e23b5ea5bf9c06ull, 536, 251, 8}, // option: eu F
    {0xb551213269aece66ull, 128, 24, 8}, // ss_cmmn: eu G; title: eu G
    {0xb5722dc146a98fa4ull, 98, 14, 8}, // title: eu S
    {0xb5d3c491aa569443ull, 640, 336, 14}, // id333: eu I
    {0xb5f8446a4670b3e5ull, 77, 26, 2}, // option: eu S
    {0xb6311db14e4a670aull, 147, 26, 2}, // option: eu G
    {0xb6a61af1ddf389a2ull, 523, 175, 8}, // result: eu S
    {0xb6c22f75614a12ccull, 528, 132, 9}, // title: eu F
    {0xb74a09bf9bdbd938ull, 56, 29, 8}, // ss_shop: jp
    {0xb7841825fa622221ull, 248, 26, 8}, // option: eu F
    {0xb7d06045091304bfull, 104, 24, 8}, // ss_shop: eu G
    {0xb85ffb2c86d4a8ecull, 104, 22, 8}, // ss_cmmn: eu I; title: eu I
    {0xb87aed31b49a37f2ull, 490, 32, 9}, // save: eu G
    {0xb8964b32410a4aa8ull, 115, 16, 8}, // ss_map: debug us eu E
    {0xb8f608f846bc7df1ull, 600, 100, 9}, // save: jp eu J
    {0xb90c5272547c4e94ull, 184, 48, 8}, // id400: eu I
    {0xb90d3bfa215b6793ull, 472, 32, 14}, // ss_map: eu I
    {0xb92ce368feb3397dull, 77, 24, 2}, // option: eu G
    {0xb9b72415820d35b0ull, 64, 26, 2}, // option: eu S
    {0xba0efefb45c931ffull, 533, 38, 14}, // option: eu I
    {0xbb21ed701073bd0eull, 523, 175, 8}, // result: eu F
    {0xbb598f0902c307bfull, 176, 56, 8}, // id400: eu F
    {0xbb6c7a33bc33f030ull, 366, 40, 9}, // option: eu I
    {0xbb9b826129bd3a32ull, 33, 14, 8}, // ss_shop: debug us jp eu
    {0xbc004995ba823745ull, 112, 32, 8}, // ss_cmmn: eu S; title: eu S
    {0xbc7b7ff27af1b605ull, 64, 26, 2}, // option: eu F
    {0xbcc27a9acc8294f3ull, 104, 24, 8}, // ss_shop: eu I
    {0xbcf592a01e612f1dull, 129, 26, 2}, // option: jp
    {0xbd1de53ff06b5254ull, 74, 14, 8}, // title: eu FI
    {0xbde486abb1aec7e3ull, 387, 20, 8}, // title: debug us jp eu
    {0xbe45d20af198bfddull, 128, 24, 8}, // ss_cmmn: eu I; title: eu I
    {0xbeed8d2ae9aa3d92ull, 48, 24, 8}, // option: eu S
    {0xbf04f1b3f08878e4ull, 462, 26, 2}, // option: eu G
    {0xbfbdf715678b1600ull, 600, 101, 9}, // save: eu G
    {0xbfd1017e5e23ce47ull, 490, 32, 9}, // save: eu G
    {0xc0046012e9ceb428ull, 104, 22, 8}, // title: eu S
    {0xc0434435fe97c6a2ull, 84, 16, 8}, // ss_cmmn: eu G; title: eu G
    {0xc0b2128c72d8db39ull, 40, 24, 8}, // ss_cmmn: jp; title: jp
    {0xc0f120fdf1d90f12ull, 145, 48, 2}, // option: jp eu
    {0xc14b30f40fac9745ull, 112, 24, 8}, // ss_map: eu S
    {0xc208c7937461327aull, 432, 80, 9}, // id22c: eu F
    {0xc4393099e6a67596ull, 256, 26, 2}, // option: eu F
    {0xc4725bbbc18dc544ull, 640, 336, 14}, // id333: eu G
    {0xc5b9cbc4915c77f1ull, 260, 28, 8}, // chap01: debug us jp eu E; chap02: debug us jp eu E; chap03: debug us jp e...
    {0xc6e170dc15a37b36ull, 176, 24, 8}, // omk_t0: eu I; omk_t1: eu I
    {0xc7c3467f1d7677a7ull, 108, 37, 8}, // ss_shop: jp
    {0xc8a6440a6dea0a5cull, 104, 24, 8}, // ss_shop: eu I
    {0xc8a86f7bf2eea558ull, 536, 104, 14}, // ss_shop: eu S
    {0xc91b1ba8d4c88e49ull, 79, 25, 2}, // option: eu F
    {0xc9bad7560d66eb15ull, 128, 24, 8}, // omk_t0: eu S; omk_t1: eu S
    {0xc9e44d7cf2b5122dull, 536, 104, 14}, // ss_shop: jp
    {0xc9fa8a1bfff00881ull, 148, 19, 8}, // omk_t0: debug us jp eu E; omk_t1: debug us jp eu E
    {0xca95c24399fc16b2ull, 368, 40, 9}, // option: eu S
    {0xcaa0d825081c5c06ull, 190, 42, 14}, // option: eu F
    {0xcaa4d3a065900db3ull, 423, 42, 8}, // option: debug us jp eu E
    {0xcb1c9f1c0183d148ull, 77, 26, 2}, // option: eu I
    {0xcb93bd55f6183a88ull, 197, 14, 8}, // title: jp
    {0xcb96dafc3e66c37dull, 145, 20, 8}, // chap01: eu I; chap02: eu I; chap03: eu I; chap04: eu I; chap05: eu I; cha...
    {0xcc4075233e394f86ull, 144, 24, 8}, // ss_map: eu G
    {0xcc495aee81f36e8dull, 48, 24, 8}, // core: debug us jp eu
    {0xcc5e0fe80eaa5b5full, 432, 80, 9}, // id22c: eu S
    {0xccbd5f851637c7e7ull, 128, 24, 8}, // omk_t0: eu G; omk_t1: eu G
    {0xcdfe5b188e9d8b8aull, 109, 22, 3}, // save: debug us
    {0xce16566cd049ae97ull, 528, 132, 9}, // title: eu S
    {0xce7e5681b6aefaefull, 64, 16, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0xcf226312e5bbccf4ull, 57, 14, 8}, // title: debug us jp eu E
    {0xcf46a3ae07e580c4ull, 144, 24, 8}, // ss_map: eu S
    {0xcfa416b92864decbull, 528, 132, 9}, // title: jp
    {0xcfc3618407bad70eull, 583, 53, 14}, // option: eu I
    {0xcfc84727d88bea2full, 224, 24, 14}, // ss_cap: eu G
    {0xd0f251b7c74e3c43ull, 176, 26, 14}, // option: eu I
    {0xd248ee6181553891ull, 75, 26, 2}, // option: eu S
    {0xd2fe6dfd5a803447ull, 160, 24, 8}, // ss_shop: eu F
    {0xd40cb86f9492686cull, 490, 32, 9}, // save: jp eu F
    {0xd41ff480c913e225ull, 77, 14, 8}, // title: eu G
    {0xd45601f463f2cb85ull, 92, 25, 2}, // option: debug us eu EGI
    {0xd4857420f3111167ull, 122, 26, 2}, // option: debug us jp eu E
    {0xd4fa8b56d5df9bc6ull, 85, 16, 8}, // ss_cmmn: eu G; title: eu G
    {0xd61b47ec8f9c6cc6ull, 112, 24, 8}, // ss_map: eu S
    {0xd61e99a4ad9cb4ceull, 154, 21, 3}, // save: jp eu G
    {0xd675dbea58c10722ull, 104, 24, 8}, // ss_shop: eu S
    {0xd6f3d6eb00486bcaull, 191, 26, 2}, // option: eu I
    {0xd7227e782976e114ull, 600, 101, 9}, // save: jp eu I
    {0xd7cfdf8a81741a30ull, 122, 22, 3}, // save: debug us jp eu EJ
    {0xd7f6334547d864c9ull, 77, 21, 8}, // ss_cmmn: jp; title: jp
    {0xd9600e6d0369c6ddull, 104, 24, 8}, // ss_shop: eu F
    {0xd9ba5d68ace638feull, 38, 21, 3}, // save: debug us jp eu EJ
    {0xd9c771125d250d32ull, 228, 57, 9}, // id22c: jp
    {0xda8bab38bac7e829ull, 512, 119, 14}, // id22c: eu F
    {0xdaaa2e070a949360ull, 136, 26, 2}, // option: debug us jp eu E
    {0xdb05a7cddd0001eeull, 95, 19, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0xdb851584e32ad217ull, 91, 25, 2}, // option: eu F
    {0xdbb1370b690ceaeaull, 600, 101, 9}, // save: jp
    {0xdc2ff33c0d92af99ull, 84, 22, 3}, // save: debug us
    {0xdd0062e2b297f399ull, 88, 16, 8}, // ss_cmmn: eu G; title: eu G
    {0xdd058008e1c1b546ull, 112, 32, 8}, // ss_cmmn: eu F; title: eu F
    {0xdda70d6044459603ull, 25, 16, 8}, // ss_cmmn: debug us jp eu E; title: debug us jp eu E
    {0xddd8ff7f928fa41full, 486, 42, 9}, // omk_t0: debug us jp eu; omk_t1: debug us jp eu
    {0xdf43655537e8f058ull, 190, 26, 2}, // option: eu I
    {0xdf4406fd5d5d0cd5ull, 618, 38, 14}, // option: eu F
    {0xdf810b5937e71895ull, 320, 32, 8}, // ss_cmmn: jp; title: jp
    {0xe0025868dac4baa9ull, 198, 24, 9}, // ss_map: jp
    {0xe09c9ec9f1785d33ull, 46, 19, 8}, // ss_cmmn: jp; title: jp
    {0xe0a608864bd9b544ull, 75, 16, 8}, // ss_cmmn: eu S; title: eu S
    {0xe12520e3fe7e19c8ull, 85, 16, 8}, // ss_cmmn: eu S; title: eu S
    {0xe14a370a9506ca11ull, 32, 16, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0xe1ce085801a1f3d3ull, 472, 32, 14}, // ss_map: eu F
    {0xe1d11d703f4f6d72ull, 328, 32, 14}, // ss_cap: eu G
    {0xe21d6d014633e104ull, 48, 48, 14}, // core: eu
    {0xe23049d5023a96c2ull, 125, 20, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0xe282ada8987ccaf8ull, 259, 24, 14}, // ss_cap: debug us eu E
    {0xe2f11e96c2d96395ull, 222, 26, 2}, // option: eu G
    {0xe3253f56ac258a1aull, 224, 24, 14}, // ss_cap: eu S
    {0xe3263779e0ff1494ull, 319, 23, 14}, // chap06: eu F
    {0xe3bd546420613e43ull, 176, 26, 14}, // option: eu S
    {0xe408fc6796d4b879ull, 62, 20, 8}, // chap01: eu I; chap02: eu I; chap03: eu I; chap04: eu I; chap05: eu I; chap...
    {0xe435566fe8d1fd44ull, 128, 24, 8}, // ss_cmmn: eu F; title: eu F
    {0xe45b61626fb865f4ull, 66, 14, 8}, // title: debug us jp eu EG
    {0xe4a3f1ad55edfd89ull, 216, 26, 2}, // option: debug us jp eu E
    {0xe4b26284755e0a53ull, 221, 42, 14}, // option: eu G
    {0xe4be0c55f6a0ba25ull, 122, 24, 3}, // save: debug us jp eu EJ
    {0xe504a0102c3ea786ull, 56, 18, 8}, // omk_t0: debug us jp eu E; omk_t1: debug us jp eu E
    {0xe51937aca5f697bdull, 163, 21, 3}, // save: eu I
    {0xe536981743eb68a3ull, 196, 14, 8}, // title: debug us jp eu
    {0xe5683007fe058635ull, 91, 26, 2}, // option: eu F
    {0xe569c60f6966b5d0ull, 72, 24, 8}, // core: debug us jp eu
    {0xe5a26cd4a912ec7aull, 56, 24, 8}, // ss_cmmn: eu G; title: eu G
    {0xe5a9331f2d07c37bull, 64, 17, 8}, // ss_cmmn: debug us eu; title: debug us eu
    {0xe5b9c76c8b455878ull, 32, 18, 8}, // ss_cmmn: jp; title: jp
    {0xe5cef97d457eabb5ull, 490, 32, 9}, // save: jp
    {0xe6054768f1cbbc64ull, 165, 24, 3}, // save: jp eu I
    {0xe701e81997e9b202ull, 166, 24, 8}, // chap01: eu S; chap02: eu S; chap03: eu S; chap04: eu S; chap05: eu S; cha...
    {0xe749f997185e2b0aull, 207, 22, 8}, // chap01: eu S; chap02: eu S; chap03: eu S; chap04: eu S; chap05: eu S; cha...
    {0xe7bc89ccd60c80eeull, 432, 80, 9}, // id22c: eu S
    {0xe7d5d0932259dcc8ull, 112, 32, 8}, // ss_cmmn: eu G; title: eu G
    {0xe7fa0026e8d828beull, 87, 14, 8}, // title: debug us jp eu EF
    {0xe8429ed8c29729eaull, 185, 31, 2}, // option: eu F
    {0xe843c71ebbf64b85ull, 89, 30, 8}, // ss_cmmn: jp; title: jp
    {0xe849004f8558103bull, 49, 26, 2}, // option: eu I
    {0xe86b9812bb6f5c92ull, 512, 119, 14}, // id22c: jp
    {0xe92a0014f35ecf6cull, 258, 29, 8}, // chap01: eu F; chap02: eu F; chap03: eu F; chap04: eu F; chap05: eu F; cha...
    {0xe93f2acc5922ec7full, 117, 20, 3}, // save: jp eu I
    {0xe93f523ec7ce3c1bull, 77, 17, 8}, // core: debug
    {0xe9d527706d7296edull, 296, 31, 2}, // option: eu I
    {0xea54ec7318da042cull, 58, 21, 8}, // ss_shop: jp
    {0xeb4de41d585f6fabull, 357, 26, 2}, // option: eu F
    {0xecd4fbcf26dc81bcull, 106, 18, 8}, // title: eu
    {0xecf4e6c344196d57ull, 536, 104, 14}, // ss_shop: debug us eu E
    {0xed766f60bdfb81aaull, 25, 16, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0xed7e5bc2a1a7c7c4ull, 34, 19, 3}, // save: jp eu I
    {0xedb1e1face708652ull, 40, 24, 8}, // ss_cmmn: debug us jp eu; title: debug us jp eu
    {0xee6dae13df9e88cbull, 140, 14, 8}, // title: eu S
    {0xeeba32095e5daae7ull, 236, 20, 8}, // chap01: debug us eu E; chap02: debug us eu E; chap03: debug us eu E; chap...
    {0xeecbf5d4126fbec8ull, 101, 14, 8}, // title: eu
    {0xeeed9db478a45e43ull, 88, 19, 8}, // title: eu
    {0xef39b67805ba5ae7ull, 131, 14, 8}, // title: eu F
    {0xf025b5c7f61492a4ull, 319, 23, 14}, // chap06: eu G
    {0xf11ee0eb386215b1ull, 96, 19, 8}, // ss_cmmn: eu S; title: eu S
    {0xf14b2eab66c25dd9ull, 200, 120, 8}, // ss_map: eu S
    {0xf15fa1fdb54082e4ull, 88, 26, 2}, // option: eu G
    {0xf20c941db34bca10ull, 128, 24, 8}, // ss_cmmn: eu S; title: eu S
    {0xf2345ad248a3c00bull, 112, 32, 8}, // ss_cmmn: eu I; title: eu I
    {0xf2f6e3abaa6e2f61ull, 372, 42, 14}, // option: eu S
    {0xf344d4727280a76cull, 191, 26, 2}, // option: eu F
    {0xf3b7ba71d2685789ull, 76, 17, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0xf45077f482c25f64ull, 490, 32, 9}, // save: jp
    {0xf45a6c4eb567011bull, 88, 16, 8}, // ss_cmmn: eu F; title: eu F
    {0xf4a8f35663df6a6dull, 88, 26, 2}, // option: debug us jp eu E
    {0xf4f19f1a2d00cd81ull, 125, 17, 8}, // ss_shop: debug us eu E
    {0xf55213a064721095ull, 144, 24, 8}, // ss_item: eu F; ss_map: eu F
    {0xf56f75ec8bf51cacull, 49, 26, 2}, // option: debug us jp eu E
    {0xf584b539fb577366ull, 57, 27, 8}, // ss_shop: jp
    {0xf64d9c530de206a3ull, 131, 24, 3}, // save: eu S
    {0xf6a8579ffefdb12eull, 47, 25, 2}, // option: eu G
    {0xf6b7e95e87773c67ull, 39, 20, 8}, // ss_cmmn: jp; title: jp
    {0xf6bb295348d0f5a2ull, 352, 32, 8}, // id400: eu S
    {0xf73c12042297645bull, 72, 32, 8}, // ss_cmmn: jp; title: jp
    {0xf76e8a642df6e34full, 224, 24, 14}, // ss_cap: eu F
    {0xf7b56f9260653aa1ull, 163, 21, 3}, // save: jp
    {0xf8282a36ef80f036ull, 138, 29, 8}, // ss_map: debug us eu E
    {0xf8367bb2300f831cull, 77, 21, 8}, // ss_cmmn: jp; title: jp
    {0xf8661018b19fc2e6ull, 96, 19, 8}, // ss_cmmn: eu F; title: eu F
    {0xf86d0ec0ac213ce5ull, 200, 120, 8}, // ss_map: eu G
    {0xf8889c7e98578851ull, 124, 120, 8}, // ss_map: jp
    {0xf8ba051b4fc174abull, 523, 53, 14}, // option: eu F
    {0xfa1dbf7506cfa561ull, 74, 16, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0xfa4cc1d3245991c1ull, 146, 38, 8}, // id400: debug us jp eu E
    {0xfa77161fe674d162ull, 432, 80, 9}, // id22c: eu G
    {0xfaa166ba4007edcbull, 490, 32, 9}, // save: eu S
    {0xfaa26fe61309c754ull, 35, 19, 8}, // ss_cmmn: debug us eu E; title: debug us eu E
    {0xfaa6e4408e6d4af8ull, 487, 32, 9}, // save: debug us
    {0xfac995dc960c6778ull, 48, 21, 8}, // option: debug us jp eu E
    {0xfad9235389dc82b5ull, 66, 24, 2}, // option: eu G
    {0xfb03181528b14608ull, 169, 14, 8}, // title: eu S
    {0xfb2ac2bb22075903ull, 84, 16, 8}, // ss_cmmn: eu I; title: eu I
    {0xfb45a411aecab487ull, 145, 24, 14}, // ss_cap: jp
    {0xfb494fd39259e323ull, 96, 32, 8}, // ss_cmmn: eu S; title: eu S
    {0xfbdf2335a7f6787aull, 128, 24, 8}, // ss_cmmn: eu F; title: eu F
    {0xfbff784a54340fc1ull, 128, 24, 8}, // ss_cmmn: eu G; title: eu G
    {0xfd180c6a2f04ac77ull, 48, 19, 8}, // ss_cmmn: jp; title: jp
    {0xfd3e4b2b168f93d9ull, 272, 14, 8}, // title: eu F
    {0xfd93207e2007316aull, 100, 37, 8}, // ss_shop: debug us eu E
    {0xfdc28289e15b545cull, 190, 26, 2}, // option: eu I
    {0xfee9a406c69d8e95ull, 61, 25, 2}, // option: debug us jp eu E
    {0xfef3ba7fef4ab2a2ull, 272, 40, 9}, // option: debug us jp eu E
    {0xff133990df0e1e4cull, 104, 24, 8}, // ss_shop: eu G
    {0xff99901838604af6ull, 160, 24, 8}, // ss_shop: eu I
    {0xffc0601310ce30a5ull, 176, 24, 8}, // omk_t0: eu F; omk_t1: eu F
    {0xfffe7b6d13870a74ull, 193, 26, 2}, // option: eu S
};

}  // namespace re4gx
