#ifndef RE4_RUNTIME_H
#define RE4_RUNTIME_H

/* Platform runtime of the whole-program port: logging, generated stubs and
 * the REL module loader. Exported by libre4_main; plain C linkage so the
 * generated files and the launcher can use it without game headers. */

#ifdef __cplusplus
extern "C" {
#endif

void re4_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
/* re4_log's lines into this file as well (appended, 2 x 4 MB at most: <path>.1), the app's error
   log without logcat; nullptr or "" stops it. The core process takes it from RE4_LOG_FILE. */
void re4_log_file(const char* path);

/* Generated stubs (stubs.cpp) report their first call through this. */
void re4_stub_hit(const char* const* names, unsigned char* seen, int index);
int re4_stub_count(void);

typedef struct Re4ModuleEntry {
    unsigned id;
    const char* name;
} Re4ModuleEntry;
extern const Re4ModuleEntry kRe4Modules[];

/* Directory holding libre4_<module>.so; set by the launcher. */
void re4_set_module_dir(const char* dir);
/* Loads a module library (idempotent); returns 0 on failure. */
void* re4_module_load(const char* name);
/* Unloads a module library; trampolines of the others into it resolve again. */
void re4_module_unload(const char* name);
int re4_module_is_loaded(const char* name);
/* OSLink support (os_link.cpp): converts a big-endian REL header from disc to
 * host byte order in place. 1 = converted, 0 = was host order, -1 = no REL. */
int re4_rel_header_to_host(void* rel);
const char* re4_module_name(unsigned id);
/* Adds a module id outside the original table (tests). */
void re4_module_register(unsigned id, const char* name);
/* Finds an exported function of a loaded module. */
void* re4_try_resolve_import(const char* symbol);
/* Like re4_try_resolve_import, but logs and aborts when nothing provides it. */
void* re4_resolve_import(const char* importer, const char* symbol);

/* The game's entry point (src/game/main.cpp, renamed by patch 0008). */
int re4_game_main(void);

/* GameCube main RAM at its original address (mem1.cpp). 48 MB like a
 * development console; the game itself assumes the 24 MB retail map. */
#define RE4_MEM1_BASE 0x80000000u
#define RE4_MEM1_SIZE 0x03000000u
#define RE4_MEM1_RETAIL_SIZE 0x01800000u
/* OSGetArenaLo at boot: end of main.dol in the original (SystemMemInit halts
 * above 0x8034FFFF). The DOL itself lives in libre4_main.so. */
#define RE4_MEM1_ARENA_LO 0x80300000u
/* LP64 (milestone 12): the game's heaps end this much higher, in the half of
 * MEM1 only a development console has (lp64_sizes.json: SystemMemInit's
 * Heap_end_addr, the same value). The works with pointers need more room in
 * them on 64 bit: the compare run of 18.10.2026 had 0x4EF40 bytes less free in
 * heap 4 (r100), and on a POCO X7 Pro em12 found no room in r107 (0x1F0E0 short).
 * 0 on ILP32: the GameCube map. */
#if __SIZEOF_POINTER__ == 4
#define RE4_MEM1_LP64_HEAP_EXTRA 0u
#else
#define RE4_MEM1_LP64_HEAP_EXTRA 0x00100000u
#endif
/* The disc's file system table sits at the top of retail memory, above the
 * game's last heap (0x817F4000), as the IPL places it; on LP64 above the
 * higher heap end. */
#define RE4_MEM1_FST_ADDR (0x817F4000u + RE4_MEM1_LP64_HEAP_EXTRA)
#define RE4_MEM1_FST_MAX 0x0000C000u
/* Retail console: the development paths (SN host file I/O) stay off. */
#define RE4_CONSOLE_TYPE 0x00000003u
/* Reserves MEM1 and writes the boot globals; 0 when the range is taken. */
int re4_mem1_init(void);
int re4_in_mem1(const void* p);
/* Game libraries must lie at 0x80000000 and above: the game tells its data
 * from offsets and table links by bit 31. Logs and aborts otherwise. */
void re4_check_game_address(const char* what, const void* addr);
/* LP64 (milestone 12): what the game can address lies in 0x80000000..0xFFFFFFFF,
 * MEM1 at its start, the rest from RE4_WINDOW_START (game_window.cpp). */
#define RE4_WINDOW_START 0x83000000ul
#define RE4_WINDOW_END 0x100000000ull
/* Address space for the game in the window (ILP32: anywhere), 0 when full. */
void* re4_window_map(unsigned long size, int prot);
/* 1 when the game can hold p in 32 bits (ILP32: always); the check logs and aborts. */
int re4_in_window(const void* p);
void re4_check_window(const char* what, const void* p);
void re4_window_report(void);

/* Folder with the extracted disc files (dvd.cpp): <dir>/disc0, <dir>/disc1,
 * each with sys/fst.bin, sys/boot.bin and files/. Set by the launcher. */
void re4_set_data_dir(const char* dir);
const char* re4_data_dir(void);

/* File access for code built with the game's flags (host_file.cpp): there the
 * C library's renames of open/pread/pwrite do not work (empty __asm__). Open
 * makes the folders on the way; read/write return the bytes moved or -1. */
int re4_host_file_open(const char* path);
long re4_host_file_read(int fd, void* data, unsigned long length, unsigned long long offset);
long re4_host_file_write(int fd, const void* data, unsigned long length, unsigned long long offset);
void re4_host_file_sync(int fd);
void re4_host_file_close(int fd);
const char* re4_host_file_error(void);
/* Disc number in the virtual drive (0/1), -1 when no disc folder was found. */
int re4_dvd_inserted_disc(void);
/* The start's reads without the frame pace (patch 0085): 1 while the next
   step of a read may follow at once, the end at the first title.dat. */
int re4_dvd_boot_fast(void);
int re4_dvd_header_check(const void* table, const char* name, unsigned offset, unsigned length, int depth);
void re4_dvd_read_done(const char* name);

/* Byte order (byte_order.cpp): in-place swaps of big-endian disc data. */
void re4_swap32_words(void* p, unsigned bytes);
void re4_swap16_words(void* p, unsigned bytes);

/* Formatumwandler (formats/): converts disc data to host byte order once per
 * load. Called by the game's DVD queue for every completed part (patch 0015;
 * `part` is its DvdHeader, `addr` the MRAM or ARAM address) and by the Yz2
 * decoder for every unpacked stream. */
void re4_formats_part_loaded(const char* name, int nest, int index, const void* part, unsigned addr);
void re4_formats_unpacked(void* dst, unsigned size);
// ARAM -> main memory DMA (aram.cpp): the data at dst came from src (ARAM)
void re4_formats_aram_copied(void* dst, const void* src, unsigned length);
// MemorySwap (patch 0028) exchanged size bytes between MRAM and ARAM
void re4_formats_memory_swapped(void* mram, unsigned aram, unsigned size);

/* ARAM contents (aram.cpp), 16 MB. */
unsigned char* re4_aram(void);

/* Dolphin OS threads (os_thread.cpp). re4_os_run runs `entry` as the boot
 * thread and dispatches every OSThread until it returns. */
int re4_os_run(int (*entry)(void));
int re4_os_in_dispatcher(void);
void re4_os_exit_current(void* val);
void re4_os_interrupt_begin(void);
void re4_os_interrupt_end(void);
int re4_os_in_interrupt(void);
/* Runs the registered handler of hardware interrupt `interrupt`
 * (__OSSetInterruptHandler) in interrupt context; 0 when there is none. */
int re4_os_hw_interrupt(short interrupt);
/* VALID_PTR outside MEM1 (patch 0037): 1 for the host stacks of the OS
 * threads and the static data of the game libraries. */
int re4_valid_port_ptr(const void* p);
/* The emulated graphics processor (gx_fifo.cpp): its pending PE interrupts
 * (draw done, token) are delivered when a thread starts waiting and with the
 * video retraces, the moments the GP would have caught up on the GameCube. */
void re4_gx_deliver_interrupts(void);
/* EXI (exi.cpp, M10): the memory card's pending transfer ends and "done"
 * interrupts, and the due OS alarms (alarm.cpp), delivered at the interrupt
 * points (re4_os_idle, OSSleepThread). */
void re4_exi_deliver(void);
/* The VI's frame buffer (vi.cpp -> gx_decode.cpp): VISetNextFrameBuffer
 * passes the XFB the picture switches to, VIFlush ends the frame. A display
 * copy into an XFB the VI does not switch to is held (RE4_GX_CHUNK_HOLD). */
void re4_gx_vi_next(const void* fb);
void re4_gx_vi_flush(void);
/* VISetBlack, applied with VIFlush (vi.cpp): the chunks are shown black
 * (RE4_GX_CHUNK_BLACK) while it holds. */
void re4_gx_vi_black(int black);
/* Completions (DVD reads, ARAM DMAs) started inside an interrupt handler run
 * when the handler has returned: re4_os_defer queues one (0 when the queue is
 * full: complete at once), re4_os_deferred_set changes the value of a queued
 * one (a cancelled read), re4_os_run_deferred runs the queued ones (after
 * every audio frame and at the end of the interrupt). */
typedef void (*Re4Deferred)(void* object, int value);
int re4_os_defer(Re4Deferred fn, void* object, int value);
int re4_os_deferred_set(Re4Deferred fn, void* object, int value);
void re4_os_run_deferred(void);

/* AI (ai.cpp): the audio blocks of one video field (5 ms each, 200 per
 * second; vi.cpp calls it with every retrace), one block at once (tests), the
 * blocks so far. Each block runs AX's DMA callback (the DSP mixes the next,
 * runtime/dsp.cpp; the game's audio frame) and goes to the sound output; a
 * hook of the game side runs before that callback (state_trace.cpp counts the
 * sound requests waiting for the game's audio frame). */
void re4_ai_field(void);
void re4_ai_block(void);
void re4_ai_set_block_hook(void (*hook)(void));
unsigned re4_ai_blocks(void);
/* The sound output (ai.cpp): stereo frames at 32 kHz for the device (any
 * thread; always fills `out`, returns the frames the game played), and the
 * counters for the log (blocks, frames waiting, blocks lost while the app did
 * not read, reads that ran dry, skips after a pause). */
int re4_audio_read(short* out, int frames);
/* The frames waiting in the ring as they are (tests). */
int re4_audio_take(short* out, int frames);
/* The same frames as four channels (front left, front right, rear left, rear
 * right; ai.cpp): the reader, the tests' raw reader, the blocks that carried
 * Dolby Pro Logic II's rear so far, and 1 while such blocks play (the last
 * within 0.5 s). */
int re4_audio_read4(short* out, int frames);
/* Both at once: per frame the stereo and then the four channels (6 values; the core process). */
int re4_audio_read6(short* out, int frames);
int re4_audio_take4(short* out, short* out4, int frames);
unsigned re4_audio_surround_blocks(void);
int re4_audio_surround_now(void);
void re4_audio_stats(unsigned* blocks, unsigned* fill, unsigned* overruns, unsigned* underruns,
                     unsigned* skips);
/* The DSP (dsp.cpp): command lists run, voice milliseconds mixed, the most
 * voices running in one frame since the last call. */
void re4_dsp_stats(unsigned* lists, unsigned* voiceMs, unsigned* peakVoices);
/* The four channels of a Dolby Pro Logic II block (front L/R, rear L/R) by its
 * AI address: 1 and taken, else 0 (dsp.cpp). */
int re4_dsp_surround(unsigned blockAddr, short* out);

/* Interrupts (vi.cpp). re4_interrupt_point delivers the retraces that are due
 * (cheap when none is); re4_os_idle / re4_wait_interrupt wait for the next one. */
int re4_interrupt_point(void);
void re4_os_idle(void);
void re4_wait_interrupt(void);

/* Per-frame observer, called from PADRead (pad.cpp; the state trace and the
 * run control, state_trace.cpp). re4_pad_script_stop ends RE4_PAD_SCRIPT. */
void re4_set_frame_hook(void (*hook)(unsigned));
void re4_pad_script_stop(void);
/* The player is free in the room: RE4_PAD_FREE_SCRIPT starts (pad.cpp). */
void re4_pad_script_free(void);
/* Buttons (PAD_BUTTON_* bits) a frame hook adds to channel 0 for the frame it
 * runs in, on top of the controller (pad.cpp; the knife swipe, knife_swipe.cpp). */
void re4_pad_add(unsigned short buttons);

/* Every OS thread and where it waits (os_thread.cpp; RE4_WAIT_TRACE=1 records
 * the places): the hang search of host/main.cpp on SIGUSR1. */
void re4_os_dump_threads(int fd);

/* Debug start room (dolphin_os.cpp): RE4_START_ROOM=0x100 preselects that
 * room in the original debug start menu (patch 0026, title.cpp titleExit). */
void re4_debug_start_room(unsigned short* room_id);
int re4_debug_start_room_set(void);
/* The debug start menu after "new game" (patch 0043): open 1 every frame it
 * waits (returns 0 with RE4_ROOM_MENU=0: skipped), open 0 when it is done.
 * re4_title_room_menu_open: 1 while it is shown (its text needs debug_mode). */
int re4_title_room_menu(int open);
int re4_title_room_menu_open(void);
/* Does nothing; ~cCamera hands its object here (patch 0046), so the compiler
 * cannot leave out the store of the cCamera vtable pointer before it. */
void re4_keep_vtable(void* object);
/* Time the game thread slept waiting for the next video retrace, in all (ns):
 * what is left of each frame (runtime/vi.cpp). */
long long re4_idle_ns(void);
/* The display's vsync (vi.cpp, from the app through re4_core_vsync; ns of
 * CLOCK_MONOTONIC): the retraces follow a display near 60 Hz. The two helpers
 * are the arithmetic of it (exported for the runtime test). */
void re4_vi_vsync(long long ns);
long long re4_vi_locked_field(long long now, long long at, long long period);
long long re4_vi_lock_phase(long long next, long long field, long long at, long long period);
/* The CRI middleware's clock (uty_tmr.c, patch 0048; runtime/vi.cpp): the
 * time base as OSGetTime, with RE4_VI_FAST=1 the fields delivered instead. */
long long re4_cri_timer(void);
/* Marks the GX chunk being recorded (RE4_GX_CHUNK_MARK, runtime/gx_decode.cpp;
 * the core thread) and lets the next one start over: the app keeps the
 * pictures from there. */
void re4_gx_mark(void);

#ifdef __cplusplus
}
#endif

#endif
