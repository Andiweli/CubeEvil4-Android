// The HD pack's groups the player can keep original (tex_pack.cpp, TextureGroups; the Mods page's "Costumes" and
// "Weapons", 1.0.0, decision of the project owner, 17.11.2026). Made 17.11.2026 from the model files of the debug, US,
// EU and JP discs: costumes are the textures of the player-side figures (em/pl*.drs: Leon pl00 jacket, pl08 shirt,
// pl09 vest, pl10 special; Ashley pl01, pl05 as player, pl03, pl11, pl15, pl21 as escort and in events; Ada pl0b, pl0c
// as player, pl02, pl12; HUNK pl06, Krauser pl0a, Wesker pl0d; etc/pl010a.tpl, pl050a.tpl), weapons those of
// em/wep*.drs (in the hand) and ss/Cmn/ss_wep*.dat (in the attache case). Not here: enemies, Saddler (pl04, pl07),
// Luis (pl14, an ally, not playable), pl0e, pl0f (objects). A texture other files use too (the events, the Krauser
// fight em39, a few small shared ones of enemies and items; the comment says "also") switches with its group. A
// texture is its texels' hash (the first part of Dolphin's name), its size and its format, with any palette.
#pragma once

#include <cstdint>

namespace re4gx {

struct GroupTexture {
  std::uint64_t tex;
  std::uint16_t width, height;
  std::uint8_t format;  // Dolphin's format number
  std::uint8_t groups;  // 1 costumes, 2 weapons (TextureGroup)
};

// 239 textures (188 costumes, 49 weapons, 2 both), sorted by tex, width, height, format
constexpr GroupTexture kGroupTextures[] = {
    {0x00842bfa0c8bce3full, 128, 64, 14, 2}, // ss_wep08 (case), wep08, wep33
    {0x0126389a11b653ebull, 256, 256, 14, 1}, // Ashley
    {0x0191943bc3004fc6ull, 128, 64, 14, 2}, // wep26; also em39.drs
    {0x0371cc71f0dcbbeeull, 128, 128, 14, 1}, // Leon (jacket)
    {0x03a0ba78aa644374ull, 128, 64, 14, 1}, // Ashley, Ashley (escort), Ashley (escort, armour) ...
    {0x044c7f193b3b2ca1ull, 256, 256, 14, 1}, // HUNK
    {0x05aa9ad0d6691391ull, 64, 128, 14, 1}, // Ashley, Ashley (escort), Ashley (escort, armour) ...
    {0x06ff714c5800be20ull, 256, 128, 14, 1}, // Krauser (player)
    {0x07e739c6fe73a32cull, 128, 64, 14, 2}, // ss_wep09 (case), wep09, wep21 ...
    {0x0a001989b5869cebull, 64, 16, 3, 1}, // Ashley, Ashley (escort), Ashley (player) ...
    {0x0cb9ef04fe9f8f5dull, 128, 64, 14, 2}, // ss_wep07 (case), wep07
    {0x0f918e58b7f048aeull, 64, 128, 0, 1}, // Leon (jacket), Leon (shirt), Leon (special RPD) ...
    {0x1016a54a65070b66ull, 128, 64, 14, 1}, // Wesker
    {0x1230f88a62af5e7dull, 32, 32, 0, 1}, // Ashley, Ashley (escort), Ashley (escort, armour) ...; also cap04.tpl
    {0x12cc2be08f7aaa31ull, 128, 64, 14, 2}, // ss_wep11 (case), ss_wep39 (case), wep11 ...
    {0x13f4a48755feda8eull, 6, 176, 8, 2}, // wep10, wep31, wep32 ...
    {0x158c45e6eae78282ull, 256, 128, 14, 1}, // Leon (vest)
    {0x15efb8f3c40de884ull, 64, 32, 14, 1}, // Ada (red dress)
    {0x1789522b15ab43b8ull, 64, 64, 14, 1}, // Leon (special RPD), Leon (vest)
    {0x186333d35db8c0d7ull, 32, 32, 8, 2}, // wep14, wep25
    {0x1877ddac2857d887ull, 32, 32, 14, 2}, // ss_wep11 (case), ss_wep12 (case), ss_wep29 (case) ...; also em39.drs
    {0x1965425e3057e96bull, 128, 64, 14, 1}, // Leon (vest)
    {0x1aabd5fa7ccf36c6ull, 32, 32, 14, 1}, // Ada, Ada (player), Ada (player, 2nd) ...
    {0x1b5dc75d4013694eull, 128, 64, 14, 2}, // ss_wep04 (case), wep04
    {0x1c01a4016760b23eull, 32, 32, 3, 1}, // Leon (special RPD), Leon (vest)
    {0x1ca7569538dbe723ull, 64, 64, 14, 1}, // Ashley
    {0x1cbc7cc7c83fb8ecull, 256, 256, 14, 1}, // Ashley, Ashley (escort), Ashley (escort, armour) ...
    {0x1d2a471654871ce7ull, 128, 256, 14, 1}, // Ada (player)
    {0x203815c673aae4d6ull, 128, 64, 14, 1}, // Ada (player, 2nd)
    {0x20a08a349fe6fac9ull, 16, 8, 14, 1}, // Krauser (player)
    {0x223724391d2a1911ull, 256, 512, 14, 1}, // Ada
    {0x233869ecca7f8bcaull, 64, 64, 14, 2}, // ss_wep00 (case), ss_wep02 (case), ss_wep43 (case) ...
    {0x23f30d07647fd843ull, 512, 512, 14, 1}, // Ada
    {0x2629bdcd7311ea69ull, 128, 64, 14, 2}, // ss_wep00 (case), ss_wep02 (case), ss_wep43 (case) ...
    {0x276b2dcf6fc5f5eaull, 256, 256, 14, 1}, // Ada (player, 2nd)
    {0x27704e7b7fc61c65ull, 128, 128, 14, 2}, // ss_wep28 (case), wep28; also em39.drs, idm052.tpl, ss_pzzl.dat
    {0x28bb558e6763e558ull, 128, 128, 3, 1}, // Leon (jacket)
    {0x28eadc8ff1bcbe39ull, 32, 32, 14, 1}, // Wesker
    {0x2b2eced79478db9eull, 64, 32, 14, 1}, // Ada (player), Ada (player, 2nd)
    {0x2e061d64e47482a9ull, 256, 32, 14, 1}, // Ashley
    {0x2ea469ba55580634ull, 128, 128, 3, 1}, // Ada (player, 2nd)
    {0x2f539c8bfc10b669ull, 48, 72, 9, 2}, // wep09, wep10, wep13 ...
    {0x31d4531e222ce538ull, 128, 64, 14, 2}, // ss_wep29 (case), wep27, wep29; also em39.drs
    {0x32910ebb4503743dull, 128, 64, 0, 1}, // Ashley
    {0x32c9641dbe4a4394ull, 64, 128, 14, 3}, // Krauser (player), ss_wep28 (case), wep28; also em39.drs
    {0x3451bafca0bff199ull, 128, 64, 14, 2}, // ss_wep12 (case), wep12
    {0x34bfec1d0d4331d9ull, 128, 128, 14, 1}, // Leon (vest)
    {0x35d1ad7ed8138286ull, 128, 88, 9, 2}, // wep01, wep02, wep04 ...
    {0x36306809328c104dull, 32, 128, 0, 1}, // Ashley
    {0x366b60764d1868c7ull, 32, 16, 14, 1}, // Ada (player), Ada (player, 2nd)
    {0x36b393a4ea8106afull, 256, 256, 14, 1}, // Wesker
    {0x37b11bded8158818ull, 256, 128, 14, 1}, // Ashley
    {0x386aca4abc0a77caull, 32, 16, 0, 1}, // Leon (jacket), Leon (shirt), Leon (special RPD) ...
    {0x392f82bc651c8a2full, 64, 64, 14, 1}, // HUNK, Leon (jacket), Leon (shirt) ...; also cap01.tpl, em10.drs, em1a....
    {0x3a4cd9a87f9b3532ull, 64, 128, 14, 1}, // Ashley (escort, armour), Ashley (player, armour), Ashley parts (etc)
    {0x3a7b31606365702dull, 120, 10, 8, 2}, // wep09, wep10, wep13 ...
    {0x3ac6a0606f9f78c3ull, 256, 256, 14, 1}, // Leon (special RPD)
    {0x3ae8b5f501cfff59ull, 20, 51, 8, 2}, // wep13, wep46
    {0x3b355f80f4de3b57ull, 256, 128, 14, 1}, // Krauser (player)
    {0x3deca7f1aa482172ull, 128, 64, 14, 1}, // Ada (player)
    {0x3efcb3311380cd8cull, 128, 128, 14, 1}, // Ashley
    {0x3f4c351ee155db61ull, 64, 64, 14, 2}, // ss_wep10 (case), ss_wep40 (case), ss_wep47 (case) ...
    {0x3fb0f8bcabf07597ull, 64, 64, 3, 1}, // Leon (shirt)
    {0x406bf35358d724a7ull, 64, 64, 14, 1}, // Ashley
    {0x41d56785c224d630ull, 64, 64, 14, 1}, // HUNK, Leon (jacket), Leon (shirt) ...
    {0x42b65440065d0b4aull, 256, 180, 9, 2}, // wep09, wep10, wep13 ...
    {0x43ce9a73e084796dull, 64, 32, 3, 1}, // Wesker
    {0x44b14a7e19a34c41ull, 64, 64, 14, 1}, // HUNK, Leon (jacket), Leon (shirt) ...; also em10.drs, em1a.drs, em1d.d...
    {0x458abbc358522d8cull, 256, 256, 14, 1}, // HUNK
    {0x46927d06d3d73527ull, 128, 64, 3, 1}, // Leon (jacket), Leon (shirt), Leon (vest)
    {0x46c4936af082fbbcull, 128, 64, 14, 1}, // Ashley
    {0x47964b8c8c4a6335ull, 16, 16, 3, 2}, // ss_wep11 (case), ss_wep39 (case), wep01 ...; also em39.drs
    {0x4799d80e2ab56ebfull, 32, 128, 0, 1}, // Ashley
    {0x4954588f715687bfull, 64, 64, 14, 1}, // Ada, Ada (player), Ada (player, 2nd) ...; also em24.drs, em28.drs
    {0x49e20843bfbed0daull, 64, 128, 0, 1}, // Ashley, Ashley (escort), Ashley (escort, armour) ...
    {0x4c3235f196073166ull, 128, 64, 14, 2}, // ss_wep14 (case), wep14, wep25
    {0x4c5fa9cd7e65421cull, 64, 64, 3, 1}, // Krauser (player)
    {0x4ca6c3fb62d333e0ull, 256, 128, 14, 1}, // Ada
    {0x4d0c0c26f427f3b3ull, 256, 128, 14, 1}, // Ashley, Ashley (escort), Ashley (player) ...
    {0x4d1a9bb26403976eull, 32, 32, 14, 2}, // wep09, wep21, wep24
    {0x4f83c8533e30fac9ull, 128, 64, 14, 1}, // Ada (player), Ada (player, 2nd)
    {0x501f787a32a669c2ull, 128, 64, 3, 1}, // Leon (vest)
    {0x5123dcd5ece0195aull, 128, 128, 3, 1}, // HUNK
    {0x51de4c7fe14db4b4ull, 32, 32, 14, 1}, // Wesker
    {0x52021113447c98beull, 128, 128, 14, 1}, // Ada (player)
    {0x542d2ac25fe39281ull, 256, 128, 14, 1}, // Leon (shirt)
    {0x5445c57ed86215f7ull, 64, 24, 8, 2}, // wep14, wep25
    {0x54cd8ef53b812dc4ull, 64, 64, 14, 1}, // Ashley (player), Ashley (player, armour)
    {0x56fceae8ac18887full, 256, 128, 14, 1}, // Ada (red dress)
    {0x581d27c6dc7635acull, 64, 128, 14, 1}, // Leon (jacket), Leon (shirt), Leon (special RPD) ...
    {0x58886d8c6a7f91e5ull, 256, 128, 14, 1}, // Leon (shirt)
    {0x58ad5da6de38325bull, 256, 512, 14, 1}, // Ada
    {0x5968ac06c76b9309ull, 32, 32, 14, 2}, // ss_wep09 (case), wep09, wep21 ...
    {0x5a1481bffb05817eull, 128, 64, 14, 2}, // ss_wep10 (case), ss_wep40 (case), ss_wep47 (case) ...
    {0x5a66f97929ff5aa2ull, 128, 64, 3, 1}, // Krauser (player)
    {0x5ae7f0104fce1963ull, 256, 32, 14, 1}, // Ashley, Ashley (escort), Ashley (player) ...
    {0x5cc9bffcbc39bd67ull, 64, 32, 3, 2}, // ss_wep07 (case), wep07
    {0x5ce207a4357700f3ull, 6, 10, 8, 2}, // wep13, wep46
    {0x5d818f7ff996ee72ull, 128, 64, 14, 2}, // ss_wep05 (case), wep05
    {0x5f4d27a2df2c6ac2ull, 128, 64, 14, 2}, // ss_wep17 (case), wep17
    {0x605a1a34e5a30758ull, 64, 32, 3, 1}, // Krauser (player)
    {0x621fb623697389c8ull, 128, 64, 14, 1}, // Krauser (player), Leon (jacket), Leon (shirt) ...; also em39.drs
    {0x622c3c67efc0ffedull, 64, 32, 0, 1}, // Ada (player), Ada (player, 2nd)
    {0x62573f436f0d87cfull, 32, 16, 14, 1}, // Leon (jacket), Leon (shirt), Leon (special RPD) ...
    {0x6676fa499fb5098dull, 64, 32, 3, 1}, // Krauser (player), Leon (jacket), Leon (shirt) ...; also em39.drs
    {0x672710cf4a17f83eull, 128, 128, 14, 1}, // Leon (shirt)
    {0x680e6f61fff055feull, 64, 64, 14, 1}, // Wesker
    {0x685647890d2945d5ull, 128, 256, 14, 1}, // Ada (player)
    {0x6a38b3beb2f4ff90ull, 256, 128, 14, 1}, // Krauser (player)
    {0x6bfbc4a43db54889ull, 64, 64, 14, 1}, // Ashley
    {0x6c6b509be12d4bdbull, 256, 128, 14, 1}, // Ashley (escort), Ashley (player), Ashley parts (etc)
    {0x6cae989ba17e2e7aull, 128, 64, 14, 2}, // ss_wep33 (case), wep33
    {0x7200b6e8a2945d9dull, 256, 128, 14, 1}, // Leon (special RPD)
    {0x7296c62f9fba1c36ull, 256, 256, 14, 1}, // Ada (player, 2nd)
    {0x72f2110209c76c28ull, 256, 128, 14, 1}, // Ada (red dress)
    {0x74de0b263b454ce4ull, 320, 224, 9, 2}, // wep09, wep10, wep13 ...
    {0x76db19a20792741eull, 64, 128, 14, 1}, // Wesker
    {0x77e04cac53a38cd9ull, 128, 64, 14, 2}, // ss_wep06 (case), ss_wep44 (case), wep06 ...
    {0x780ccefec37bc71eull, 32, 32, 0, 1}, // Wesker
    {0x7c3ca06a223ce429ull, 64, 64, 14, 1}, // Ada, Ada (player), Ada (player, 2nd) ...; also em24.drs, em28.drs
    {0x7f496320edbf7e7bull, 64, 64, 3, 1}, // Ashley
    {0x8137158c8a32ea52ull, 256, 128, 14, 1}, // Leon (jacket), Leon (shirt), Leon (special RPD) ...
    {0x82d301c7309161fdull, 256, 256, 14, 1}, // Wesker
    {0x82f129011bd2a48aull, 128, 16, 3, 1}, // Ashley, Ashley (escort), Ashley (player) ...
    {0x8300e2cae10c7aa4ull, 256, 128, 14, 1}, // Leon (vest)
    {0x837a1c3d66f4cf7eull, 64, 64, 3, 1}, // Ashley (escort), Ashley (player), Ashley parts (etc)
    {0x83d0f6bad8d8d26eull, 64, 64, 14, 1}, // Ada (red dress), Ashley, Ashley (escort) ...; also em30.drs, ss_oc101....
    {0x869ba576fb7e4ac6ull, 128, 128, 14, 1}, // Wesker
    {0x893393e154720047ull, 64, 64, 0, 1}, // Ashley, Ashley (escort), Ashley (escort, armour) ...
    {0x8ea75dc954c51dd7ull, 24, 24, 8, 2}, // wep09, wep10, wep13 ...
    {0x8fd79218d5634726ull, 128, 256, 14, 1}, // Krauser (player); also em39.drs
    {0x9061c1db152b4536ull, 32, 64, 3, 1}, // Ashley (escort, armour), Ashley (player, armour), Ashley parts (etc)
    {0x90ab63c782360d44ull, 64, 64, 14, 1}, // HUNK, Leon (jacket), Leon (shirt) ...
    {0x90e7489befb3156aull, 128, 256, 14, 1}, // Ashley
    {0x90fbddb797d17f30ull, 128, 64, 14, 3}, // Ada, Ada (player), Ada (player, 2nd) ...
    {0x91748765b6ece54aull, 128, 128, 14, 1}, // Ada (player), Ada (player, 2nd)
    {0x938a5b628f2d9bd0ull, 128, 64, 14, 1}, // Ashley, Ashley (escort), Ashley (player) ...
    {0x9412bcba962d78beull, 128, 128, 14, 1}, // Ashley, Ashley (escort), Ashley (escort, armour) ...
    {0x965419a86684654aull, 128, 128, 14, 1}, // Ada
    {0x96770778f040fbb7ull, 256, 128, 14, 1}, // HUNK
    {0x987ba3c46a8784ccull, 256, 256, 14, 1}, // Leon (jacket)
    {0x99f9c771931287f4ull, 64, 64, 14, 1}, // Ashley
    {0x9a5a233e48de0561ull, 128, 64, 14, 1}, // Ada, Ada (player), Ada (player, 2nd) ...
    {0x9b2dc0de4485feb1ull, 256, 128, 14, 1}, // Leon (jacket), Leon (shirt), Leon (vest)
    {0x9bb1402a5fe537feull, 128, 64, 14, 1}, // Ada, Ada (player), Ada (player, 2nd) ...
    {0x9d895770dd646410ull, 256, 256, 14, 1}, // Krauser (player); also em39.drs
    {0x9e72dfc808f2c662ull, 32, 32, 14, 1}, // Ashley, Ashley (escort), Ashley (escort, armour) ...; also cap04.tpl
    {0x9ec4266b49efba3cull, 32, 128, 14, 1}, // Ashley
    {0x9f2853c7caa68a72ull, 64, 64, 9, 2}, // wep01, wep02, wep03 ...
    {0xa3d8aabbf4d27810ull, 256, 256, 14, 1}, // Ada (player)
    {0xa4f4453f9ff4fa17ull, 64, 64, 3, 1}, // Krauser (player)
    {0xa976005417158697ull, 32, 128, 14, 1}, // Ashley
    {0xaa701ab01426c69bull, 64, 64, 9, 2}, // wep01, wep02, wep03 ...
    {0xac892d1ec16e8281ull, 128, 64, 14, 1}, // Ashley
    {0xada8a14def1085dfull, 128, 64, 14, 1}, // Ashley (escort, armour), Ashley (player, armour), Ashley parts (etc)
    {0xadbc9adbe8faefb0ull, 64, 64, 0, 1}, // Ashley
    {0xae82d696ab30109aull, 128, 64, 0, 1}, // Ada, Ada (player), Ada (player, 2nd)
    {0xafe4c6568620f1c2ull, 64, 64, 14, 1}, // Ada, Ada (player), Ada (player, 2nd) ...; also em24.drs, em28.drs
    {0xb02a7f3dab6b55d8ull, 128, 64, 3, 1}, // Krauser (player)
    {0xb05f672ff2d1db87ull, 64, 64, 14, 1}, // Ada, Ada (player), Ada (player, 2nd)
    {0xb17cd52ba81feaf2ull, 160, 8, 8, 2}, // wep14, wep25
    {0xb3fe7ce31c1df986ull, 128, 64, 3, 1}, // Krauser (player)
    {0xb4e85b43a1b5389bull, 64, 64, 14, 1}, // Wesker
    {0xb5e5d6d4f6c1dc2dull, 64, 64, 0, 1}, // Ada (red dress)
    {0xb648032a2ef1e2eaull, 128, 128, 0, 1}, // Ashley, Ashley (escort), Ashley (escort, armour) ...
    {0xb79038ed0d725fb1ull, 128, 128, 14, 1}, // Krauser (player)
    {0xb8776765a6afb611ull, 256, 256, 14, 1}, // Ada (red dress)
    {0xb9004d7058ababd0ull, 128, 64, 3, 1}, // Leon (vest)
    {0xbc7d2767585ae8e1ull, 256, 256, 14, 1}, // Ashley (escort), Ashley (player), Ashley parts (etc)
    {0xbdcf4ef93085acbbull, 128, 128, 14, 1}, // Ashley
    {0xc08279785e21912bull, 128, 256, 14, 1}, // Leon (jacket)
    {0xc3062cbe7fdd0094ull, 32, 128, 0, 1}, // Ashley
    {0xc307395de0762057ull, 64, 64, 14, 2}, // ss_wep14 (case), wep14, wep25
    {0xc4c7e112e31553a8ull, 64, 64, 14, 1}, // Ada (player)
    {0xc5006dce319ea469ull, 256, 128, 14, 1}, // Ada (player, 2nd)
    {0xc7438d7116f5e427ull, 64, 32, 14, 1}, // Krauser (player)
    {0xca4f8cb411454dc4ull, 64, 64, 14, 2}, // ss_wep09 (case), ss_wep10 (case), ss_wep47 (case) ...
    {0xca7ae5d2163c0e67ull, 512, 1024, 14, 1}, // Ada (red dress)
    {0xcaf305f72c634a5bull, 100, 19, 8, 2}, // wep14, wep25
    {0xcb8148644174e916ull, 128, 128, 14, 1}, // Leon (special RPD)
    {0xcca99e51927e1864ull, 256, 256, 14, 1}, // Ada (player, 2nd)
    {0xcd74026c37e69facull, 256, 128, 0, 1}, // Ada (red dress)
    {0xcf362bed53a2ff2full, 256, 128, 14, 1}, // HUNK
    {0xcfc8caca313994e5ull, 256, 128, 14, 1}, // Ashley, Ashley (escort), Ashley (escort, armour) ...
    {0xd26582f8a9aeec5dull, 128, 64, 14, 1}, // Ada (red dress)
    {0xd46362e0d18dd073ull, 128, 64, 3, 1}, // HUNK
    {0xd513451ce6814227ull, 128, 128, 14, 1}, // Leon (jacket)
    {0xd5556d38af9bac2bull, 256, 128, 14, 1}, // Ada (red dress)
    {0xd5a37ba7f0e83769ull, 64, 64, 0, 1}, // Ashley
    {0xd863934e998cba4cull, 128, 128, 3, 1}, // Ashley
    {0xd9599522dc54e0fcull, 128, 64, 14, 2}, // ss_wep03 (case), wep03, wep17 ...; also pl14.drs
    {0xd97f4aa3150593f3ull, 256, 256, 14, 1}, // Ashley (escort, armour), Ashley (player, armour), Ashley parts (etc)
    {0xdab9b7f354803f12ull, 128, 256, 14, 1}, // Ashley (escort, armour), Ashley (player, armour), Ashley parts (etc)
    {0xdb5be8c8b6abb4a1ull, 256, 256, 14, 1}, // Wesker
    {0xdb81c1efee3ef56full, 128, 32, 14, 1}, // Ashley, Ashley (escort), Ashley (player) ...
    {0xdbb92665673102b7ull, 128, 64, 14, 1}, // Ada (player), Ada (player, 2nd)
    {0xdf063c144e34eb0dull, 128, 64, 14, 1}, // Ada
    {0xdf316b8ee0ac47a1ull, 128, 64, 14, 2}, // ss_wep01 (case), wep01, wep38
    {0xe01b45da908d8be9ull, 128, 128, 14, 1}, // Ashley (escort), Ashley (player), Ashley parts (etc)
    {0xe17d4024ac33149eull, 128, 128, 0, 1}, // Wesker
    {0xe1a3a362e6b1248cull, 128, 128, 14, 1}, // Ada
    {0xe25c50a76472c109ull, 64, 128, 14, 1}, // Leon (jacket), Leon (shirt), Leon (special RPD) ...
    {0xe2bedb78d65fccbdull, 64, 64, 3, 1}, // Ashley
    {0xe2cb923d739bc07aull, 128, 17, 8, 2}, // wep13, wep46
    {0xe332afe4e3a6558eull, 256, 32, 14, 1}, // Ada (player)
    {0xe3b5bac3cc8621f6ull, 128, 256, 14, 1}, // Ashley, Ashley (escort), Ashley (player) ...
    {0xe7467eedc2bb7318ull, 64, 64, 0, 1}, // Ashley
    {0xe961486ffe4bdac6ull, 64, 64, 14, 1}, // Ashley, Ashley (escort), Ashley (escort, armour) ...
    {0xea3be9a69ba0e7fdull, 128, 64, 3, 1}, // Leon (special RPD)
    {0xea467bc72e32ff25ull, 64, 64, 3, 1}, // Leon (jacket)
    {0xeaaf04b49cd35276ull, 128, 128, 3, 1}, // Ada (player, 2nd)
    {0xeac1d56b554dbf33ull, 128, 128, 3, 1}, // Ada (red dress)
    {0xeadecf259f562087ull, 128, 64, 14, 2}, // ss_wep38 (case)
    {0xebb761fff13f2d0cull, 256, 256, 14, 1}, // Ashley
    {0xec0e12099713ddf3ull, 64, 32, 14, 1}, // Ashley (escort, armour), Ashley (player, armour), Ashley parts (etc)
    {0xec60572776e77288ull, 64, 64, 0, 1}, // Ada, Ada (player), Ada (player, 2nd) ...; also em30.drs, ss_oc101.dat, ...
    {0xecfe340230e44c12ull, 128, 64, 3, 1}, // Leon (shirt)
    {0xed119481d30a2f62ull, 256, 256, 14, 1}, // Ada, Ada (player), Ada (player, 2nd)
    {0xee91aacca7d232eeull, 256, 256, 14, 1}, // Ashley
    {0xeeed84d431278f56ull, 32, 32, 3, 1}, // HUNK, Leon (jacket), Leon (shirt) ...
    {0xef17a98f3f8db2adull, 128, 128, 14, 1}, // Krauser (player)
    {0xf1dd6e2fcc628f30ull, 128, 64, 14, 2}, // ss_wep15 (case), wep15
    {0xf2727d1f43d386d8ull, 64, 64, 3, 1}, // Leon (special RPD)
    {0xf2b9a7f9c625ee7full, 64, 128, 3, 1}, // Ada (red dress)
    {0xf36733bd7668a8f5ull, 128, 88, 9, 2}, // wep01, wep02, wep04 ...
    {0xf3ed96f6f5df59f5ull, 128, 256, 14, 1}, // Ada (red dress)
    {0xf40d365eca10f342ull, 64, 128, 14, 1}, // Krauser (player); also idm072.tpl
    {0xf422be68f141b965ull, 16, 32, 14, 2}, // wep03, wep18
    {0xf443c8a764b77e6full, 256, 256, 14, 1}, // Leon (special RPD)
    {0xf513581a8559dec3ull, 32, 16, 14, 2}, // ss_wep07 (case), ss_wep33 (case), wep07 ...
    {0xf75437a1d8976cacull, 128, 128, 3, 1}, // Wesker
    {0xf8b439ed37f503fbull, 128, 64, 14, 1}, // Ashley (escort, armour), Ashley (player, armour), Ashley parts (etc)
    {0xf91224a4ed530ee5ull, 256, 128, 14, 1}, // HUNK
    {0xfa67bf6062463b4aull, 256, 256, 14, 1}, // Krauser (player)
    {0xfb1bfbe321b8a3fbull, 128, 256, 14, 1}, // Leon (shirt), Leon (vest)
    {0xfbd95388995da6abull, 128, 64, 14, 2}, // ss_wep16 (case), wep16; also em2b.drs, em31.drs, em32.drs ...
    {0xfc63a3aced9b04b4ull, 32, 128, 14, 1}, // Ashley
    {0xfe4f623df245ae83ull, 64, 128, 14, 1}, // Leon (shirt)
    {0xfe7fdc1410ede41bull, 128, 64, 14, 1}, // Krauser (player)
};

}  // namespace re4gx
