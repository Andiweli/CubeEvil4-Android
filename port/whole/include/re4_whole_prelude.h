/* Forced include for every game unit of the whole-program port build. */

/* The platform C library is declared before any game header. Game headers
 * redeclare a few C functions themselves (memcpy in joy.h, sprintf in xml.h);
 * after the system declaration those redeclarations are compatible.
 * math_sub.h defines its own inline fabsf, so libm's is renamed here exactly
 * as include/dolphin/types.h does it. */
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#define fabsf fabsf_libm
#include <math.h>
#undef fabsf

/* newlib's character class table (include/newlib_local.h, prodg/ctype.h,
 * t_movie): Android's C library declares a pointer of the same name
 * (`const char* _ctype_`), so the game's table is renamed here, after the
 * system declaration (runtime/newlib_ctype.c defines it). */
#define _ctype_ re4_newlib_ctype_

/* Symbol names of operator new / new[] for asm labels (whole_prepare.py
 * SIZE_T_SYMBOLS): the Itanium name depends on the width of size_t. */
/* the Itanium letter of u32 for asm labels (whole_prepare.py GCC295_SYMBOLS) */
#if __SIZEOF_LONG__ == 4
#define RE4_ASM_U32 "m"
#else
#define RE4_ASM_U32 "j"
#endif
#if __SIZEOF_SIZE_T__ == 4
#define RE4_ASM_NEW "_Znwj"
#define RE4_ASM_NEW_ARRAY "_Znaj"
#else
#define RE4_ASM_NEW "_Znwm"
#define RE4_ASM_NEW_ARRAY "_Znam"
#endif

/* Fixed work areas and pool slots on 64 bit (milestone 12, whole_prepare.py
 * lp64_sizes, port/whole/lp64_sizes.json): the GameCube size on ILP32, on LP64
 * the size the classes and the works laid over the areas need there. The value
 * carries the enumerator re4_lp64_size (0) on both widths: the checks in
 * port/tools/m12 see a reviewed size in the syntax tree, also inside other
 * macros (no token names); `+ 0` keeps the value and its type. */
enum { re4_lp64_size = 0 };
#if __SIZEOF_POINTER__ == 4
#define RE4_LP64_SIZE(gc, lp64) ((gc) + re4_lp64_size)
#else
#define RE4_LP64_SIZE(gc, lp64) ((lp64) + re4_lp64_size)
#endif

/* A GameCube byte offset into a structure whose members move on LP64
 * (`(u8*) p + 0x378`, milestone 12, patch 0073): the member's offset, on ILP32
 * checked at compile time against the GameCube value the code had. */
#define RE4_OFS(T, member, gc)                                                              \
    ((void) sizeof(char[(sizeof(void*) != 4 || __builtin_offsetof(T, member) == (gc)) ? 1 : -1]), \
     __builtin_offsetof(T, member))

/* A signed 32-bit value the game turns into a pointer (milestone 12,
 * whole_prepare.py lp64_int_casts, port/whole/lp64_int_casts.json): read as
 * unsigned, so the pointer gets the ILP32 bit pattern; the game's addresses
 * have bit 31 set, which a signed value would extend to 0xFFFFFFFF8xxxxxxx on
 * LP64 (primInit on the Retroid, 17.10.2026). The same value on ILP32. */
#define RE4_ADDR32(x) ((unsigned int) (x))

#ifdef __cplusplus
/* Where the own work of a class derived from B begins (its first member, of type T),
 * for views that reach past B (patch 0074): on LP64 the compiler puts it into B's tail
 * padding (cEm: 0x49C, sizeof 0x4A0; cEsp: 0x10C, sizeof 0x110), so sizeof(B) is not
 * it. On ILP32 checked at compile time against the GameCube value, like RE4_OFS. */
template <class B, class T> struct Re4OwnWork : B {
    T re4_first;
};
#define RE4_OWN(B, T, gc)                                                                         \
    ((void) sizeof(char[(sizeof(void*) != 4 || __builtin_offsetof(Re4OwnWork<B, T>, re4_first) == (gc)) ? 1 : -1]), \
     __builtin_offsetof(Re4OwnWork<B, T>, re4_first))
#endif

/* Entry `no` of an archive with a table (PL_ARC_PTR, patch 0134): u32 count, u32 REL offset, two
 * zero words, then the entries' offsets; the game indexes from the start, so entry k is index
 * k + 4. Some code reads indexes past the table, into the tags behind it ("EFF\0", "BIN\0" ...):
 * Hunk's machine gun (wep29, 48 entries) takes its neck motions from indexes 0x34..0x39, both
 * releases (Leon's TMP, wep11, has them). The GameCube reads those tags big-endian, as offsets of
 * about 1 GB: the pointer lies outside its memory and VALID_PTR2 drops it (cPlNeck::init, the
 * head stays still). The converted archive holds host-order offsets, but the tags are bytes; read
 * little-endian they are offsets of a few MB, a pointer into memory, read as a motion: Mercenaries
 * with Hunk crashed in HermiteInterpolation (tester report 26.10.2026). An index past the count
 * of a table-shaped header reads its word as the GameCube does. Logged once per place
 * (runtime/formats/formats_arc.cpp). */
#ifdef __cplusplus
extern "C" {
#endif
void re4_arc_past_table(unsigned int base, unsigned int no, unsigned int count, unsigned int word);
#ifdef __cplusplus
}
#endif
static inline void* re4_arc_entry(const void* table, const void* arc, unsigned int no)
{
    const unsigned int* ofs = (const unsigned int*) table;
    unsigned int base = (unsigned int) (size_t) arc;
    unsigned int v = ofs[no];
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    if (no >= 4 && no - 4 >= ofs[0] && ofs[0] < 0x1000 && ofs[2] == 0 && ofs[3] == 0) {
        v = __builtin_bswap32(v);
        re4_arc_past_table(base, no, ofs[0], v);
    }
#endif
    return (void*) (size_t) (unsigned int) (v + base);
}

/* C equivalents of the Gekko instructions of the replaced inline asm. */
#include "re4_ppc.h"

/* acosf / asinf of the game code (whole_prepare.py ARC_CALL: every call outside src/lib): an
 * argument at most RE4_ARC_SLACK beyond +-1 counts as +-1. The game takes the angle between
 * two unit vectors as acosf of their dot product. On the GameCube PSVECNormalize (frsqrte and
 * one Newton step) makes unit vectors a little short, so the product stays within +-1; the port
 * normalizes with 1/sqrtf (C_VECNormalize), its unit vectors can be a last bit long, and
 * acosf(1.0000001) is NaN: the wall step of the insects (em2d, em2c: sinf(acosf(...)) * 700
 * added to the position) put them at NaN on about 30 % of slanted surfaces, where every
 * quadrilateral area counts them inside (tester report 22.10.2026, r21d: the pistons crushed
 * Leon at once, also after each continue). Further out stays NaN, as on the GameCube. The
 * first clamp and every tenfold count are logged (runtime/log.cpp). */
#define RE4_ARC_SLACK 1e-5f
#ifdef __cplusplus
extern "C" {
#endif
void re4_arc_clamped(float x);
#ifdef __cplusplus
}
#endif
static inline float re4_arc_arg(float x)
{
    if (x > 1.0f && x <= 1.0f + RE4_ARC_SLACK) {
        re4_arc_clamped(x);
        return 1.0f;
    }
    if (x < -1.0f && x >= -1.0f - RE4_ARC_SLACK) {
        re4_arc_clamped(x);
        return -1.0f;
    }
    return x;
}
static inline float re4_acosf(float x)
{
    return acosf(re4_arc_arg(x));
}
static inline float re4_asinf(float x)
{
    return asinf(re4_arc_arg(x));
}

/* GameCube inline asm (register pinning, PPC instructions, section
 * directives) has no meaning on ARM or x86 and is dropped. Defined after the
 * system headers, which use __asm__ for symbol redirection. Symbol aliases
 * (`decl asm("name")`) are rewritten to __asm by whole_prepare.py and kept. */
#define asm(...)
#define __asm__(...)

/* C++ declares const-correct overloads of the C search functions; the game
 * assigns their results to char* as C allows. Route the calls to wrappers with
 * the C signatures. */
inline char* re4_strstr(const char* s, const char* n) { return (char*)strstr(s, n); }
inline char* re4_strchr(const char* s, int c) { return (char*)strchr(s, c); }
inline char* re4_strrchr(const char* s, int c) { return (char*)strrchr(s, c); }
inline char* re4_strpbrk(const char* s, const char* a) { return (char*)strpbrk(s, a); }
inline void* re4_memchr(const void* s, int c, size_t n) { return (void*)memchr(s, c, n); }
#define strstr re4_strstr
#define strchr re4_strchr
#define strrchr re4_strrchr
#define strpbrk re4_strpbrk
#define memchr re4_memchr

#ifdef __cplusplus
/* Pointer members of fixed-form structures (fixed_form.json): RE4_P32(T). */
#include "re4_ptr32.h"

/* Two bytes of a big-endian halfword as an array view (u8 v[2] in a union
 * with the halfword, patch 0016): index 0 is the high byte, 1 the low byte,
 * as the original code expects, whatever the host byte order. */
struct Re4BeBytes2 {
    unsigned char v[2];
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    unsigned char& operator[](int i) { return v[1 - i]; }
    const unsigned char& operator[](int i) const { return v[1 - i]; }
#else
    unsigned char& operator[](int i) { return v[i]; }
    const unsigned char& operator[](int i) const { return v[i]; }
#endif
};

/* Big-endian views (port/whole/byte_views.json, applied by whole_prepare.py):
 * the game reads and writes several small fields at once through a cast word
 * pointer (`*(u32*) &col = 0xFF`, colours, tags, AX address pairs). On the
 * GameCube the first field is the most significant part of the word; on ARM
 * and x86 the least. RE4_BE(T, E, p) is the word T over the fields of E bytes
 * at p, each field in host order, the first field most significant, as on the
 * GameCube. RE4_BE_AT(T, p, i) reads part i (counted from the most
 * significant end, as on the GameCube) of the host-order scalar at p. */
template <int N> struct Re4Uint;
template <> struct Re4Uint<1> { typedef unsigned char T; };
template <> struct Re4Uint<2> { typedef unsigned short T; };
template <> struct Re4Uint<4> { typedef unsigned int T; };
template <> struct Re4Uint<8> { typedef unsigned long long T; };

template <class T, int E>
struct Re4BeView {
    typedef typename Re4Uint<sizeof(T)>::T U;
    typedef typename Re4Uint<E>::T Field;
    unsigned char* p;
    explicit Re4BeView(const volatile void* q) : p((unsigned char*) q) {}
    U load() const
    {
        U v = 0;
        for (unsigned k = 0; k < sizeof(T) / E; k++) {
            Field f;
            __builtin_memcpy(&f, p + k * E, E);
            v = (U) ((v << (8 * E)) | f);
        }
        return v;
    }
    void store(U v)
    {
        for (int k = (int) (sizeof(T) / E) - 1; k >= 0; k--) {
            Field f = (Field) v;
            __builtin_memcpy(p + k * E, &f, E);
            v = (U) (v >> (8 * E));
        }
    }
    operator T() const
    {
        U u = load();
        T t;
        __builtin_memcpy(&t, &u, sizeof t);
        return t;
    }
    Re4BeView& operator=(T t)
    {
        U u;
        __builtin_memcpy(&u, &t, sizeof u);
        store(u);
        return *this;
    }
    Re4BeView& operator=(const Re4BeView& o) { return *this = (T) o; }
    template <class V> Re4BeView& operator|=(V v) { return *this = (T) ((T) *this | v); }
    template <class V> Re4BeView& operator&=(V v) { return *this = (T) ((T) *this & v); }
    template <class V> Re4BeView& operator^=(V v) { return *this = (T) ((T) *this ^ v); }
    template <class V> Re4BeView& operator+=(V v) { return *this = (T) ((T) *this + v); }
    template <class V> Re4BeView& operator-=(V v) { return *this = (T) ((T) *this - v); }
};
/* a function, not a constructor call: `RE4_BE(...) = v;` at the start of a
 * statement would otherwise declare a reference */
template <class T, int E>
inline Re4BeView<T, E> re4_be(const volatile void* p)
{
    return Re4BeView<T, E>(p);
}
#define RE4_BE(T, E, p) re4_be<T, E>(p)

template <class T, class O>
inline T re4_be_part(const O* obj, int i)
{
    typename Re4Uint<sizeof(O)>::T whole;
    __builtin_memcpy(&whole, obj, sizeof whole);
    typename Re4Uint<sizeof(T)>::T part =
        (typename Re4Uint<sizeof(T)>::T) (whole >> (8 * (sizeof(O) - (i + 1) * sizeof(T))));
    T t;
    __builtin_memcpy(&t, &part, sizeof t);
    return t;
}
#define RE4_BE_AT(T, p, i) re4_be_part<T>((p), (i))
#endif
