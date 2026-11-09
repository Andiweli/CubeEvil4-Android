#include "stack_context.h"
#include <cstdlib>
#include <cfenv>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__ANDROID__) || (defined(__linux__) && (defined(__arm__) || defined(__aarch64__)))
// Android, and the Linux ARM test builds of the whole-program port (run under
// qemu), share the Boost.Context assembly.
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
// ABI declarations from Boost.Context's detail/fcontext.hpp (1.88.0).
// Only the two low-level assembly operations are linked; no Boost C++ runtime.
struct Transfer { void* context; void* data; };
extern "C" Transfer jump_fcontext(void* to, void* data);
extern "C" void* make_fcontext(void* stackTop, std::size_t size, void (*entry)(Transfer));
#elif defined(__linux__)
// Linux x86 test builds of the whole-program port: glibc ucontext.
#define RE4_UCONTEXT 1
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#else
#error "A tested stack context backend is required for this platform"
#endif
#ifndef _WIN32
// The whole-program port on 64 bit (port/whole/runtime/game_window.cpp): game
// code keeps addresses of its locals in 32 bits, so its stacks come from the
// window below 4 GB. Weak: other users of this file map anywhere.
extern "C" void* re4_window_map(unsigned long size, int prot) __attribute__((weak));
#endif
// The sanitizer check build of the whole-program port (port/whole/CMakeLists.txt RE4_SANITIZE):
// AddressSanitizer is told about each switch and the stack it goes to, so that it checks the
// locals of the right stack, and a new stack starts without the guard marks of an earlier one
// at the same address (a discarded stack keeps those of the frames that never returned).
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define RE4_ASAN 1
#endif
#endif
#ifdef RE4_ASAN
#include <sanitizer/asan_interface.h>
#include <sanitizer/common_interface_defs.h>
#define RE4_SWITCH_START(save, bottom, size) __sanitizer_start_switch_fiber(save, bottom, size)
#define RE4_SWITCH_FINISH(save, bottom, size) __sanitizer_finish_switch_fiber(save, bottom, size)
#else
#define RE4_SWITCH_START(save, bottom, size) ((void) 0)
#define RE4_SWITCH_FINISH(save, bottom, size) ((void) 0)
#endif

namespace re4::port::detail {
ContextDomain::ContextDomain() {
#ifdef _WIN32
    if (!IsThreadAFiber()) {
        if (!ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH))
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "ConvertThreadToFiberEx");
        converted_ = true;
    }
#endif
}
ContextDomain::~ContextDomain() {
#ifdef _WIN32
    if (converted_ && !ConvertFiberToThread()) std::terminate();
#endif
}

struct StackContext::Impl {
    Entry entry;
    void* argument;
    void* context = nullptr;
    void* caller = nullptr;
#ifdef _WIN32
    static void WINAPI start(void* data) {
        auto* self = static_cast<Impl*>(data);
        self->entry(self->argument);
        std::terminate(); // A context entry must always yield before completion.
    }
#else
    std::fenv_t floatingPoint{};
    std::fenv_t callerFloatingPoint{};
    void* mapping = MAP_FAILED;
    std::size_t mappingBytes = 0;
    // for the sanitizer: this stack, the one of whoever resumed it, and the fake stacks of each
    // side while it is switched away (RE4_SWITCH_START / RE4_SWITCH_FINISH)
    void* stackBottom = nullptr;
    std::size_t stackBytes = 0;
    const void* callerBottom = nullptr;
    std::size_t callerBytes = 0;
    void* selfFake = nullptr;
    void* callerFake = nullptr;
#ifdef RE4_UCONTEXT
    ucontext_t self_{};
    ucontext_t callerContext{};
    static void start(unsigned low, unsigned high) {
        auto* self = reinterpret_cast<Impl*>((static_cast<std::uintptr_t>(high) << 16 << 16) | low);
        RE4_SWITCH_FINISH(nullptr, &self->callerBottom, &self->callerBytes);
        self->entry(self->argument);
        std::terminate();
    }
#else
    static void start(Transfer transfer) {
        auto* self = static_cast<Impl*>(transfer.data);
        RE4_SWITCH_FINISH(nullptr, &self->callerBottom, &self->callerBytes);
        self->caller = transfer.context;
        self->entry(self->argument);
        std::terminate();
    }
#endif
#endif
    Impl(std::size_t bytes, Entry function, void* arg) : entry(function), argument(arg) {
        if (!entry || bytes < 64 * 1024 || bytes > 16 * 1024 * 1024)
            throw std::invalid_argument("Native stack size must be 64 KiB..16 MiB and entry non-null");
#ifdef _WIN32
        context = CreateFiberEx(bytes, bytes, FIBER_FLAG_FLOAT_SWITCH, start, this);
        if (!context) throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreateFiberEx");
#else
        std::fegetenv(&floatingPoint);
        const auto rawPage = sysconf(_SC_PAGESIZE);
        if (rawPage <= 0) throw std::runtime_error("Cannot query page size");
        const auto page = static_cast<std::size_t>(rawPage);
        const auto usable = ((bytes + page - 1) / page) * page;
        mappingBytes = usable + 2 * page;
        // Both ends are protected. The stack is writable and never executable.
        if (re4_window_map) {
            mapping = re4_window_map(mappingBytes, PROT_NONE);
            if (!mapping) mapping = MAP_FAILED;
            if (mapping == MAP_FAILED) throw std::runtime_error("no room for a stack below 4 GB");
        } else {
            mapping = mmap(nullptr, mappingBytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (mapping == MAP_FAILED) throw std::system_error(errno, std::generic_category(), "mmap stack");
        }
        auto* bottom = static_cast<char*>(mapping) + page;
        if (mprotect(bottom, usable, PROT_READ | PROT_WRITE) != 0) {
            const int error = errno;
            munmap(mapping, mappingBytes);
            mapping = MAP_FAILED;
            throw std::system_error(error, std::generic_category(), "mprotect stack");
        }
        stackBottom = bottom;
        stackBytes = usable;
#ifdef RE4_ASAN
        __asan_unpoison_memory_region(bottom, usable);
#endif
#ifdef RE4_UCONTEXT
        if (getcontext(&self_) != 0) throw std::system_error(errno, std::generic_category(), "getcontext");
        self_.uc_stack.ss_sp = bottom;
        self_.uc_stack.ss_size = usable;
        self_.uc_link = nullptr;
        const auto self = reinterpret_cast<std::uintptr_t>(this);
        makecontext(&self_, reinterpret_cast<void (*)()>(start), 2, static_cast<unsigned>(self),
                    static_cast<unsigned>(self >> 16 >> 16));
        context = &self_;
#else
        context = make_fcontext(bottom + usable, usable, start);
#endif
#endif
    }
    ~Impl() {
#ifdef _WIN32
        DeleteFiber(context);
#else
#ifdef RE4_ASAN
        if (stackBottom) __asan_unpoison_memory_region(stackBottom, stackBytes);
#endif
        if (mapping != MAP_FAILED) munmap(mapping, mappingBytes);
#endif
    }
};
StackContext::StackContext(std::size_t bytes, Entry entry, void* argument)
    : impl_(std::make_unique<Impl>(bytes, entry, argument)) {}
StackContext::~StackContext() = default;
bool StackContext::contains(const void* address) const {
#ifdef _WIN32
    (void)address;
    return false;
#else
    if (impl_->mapping == MAP_FAILED) return false;
    // the usable stack only: the guard pages at both ends are PROT_NONE, and a pointer into them
    // passed re4_valid_port_ptr (SIGSEGV SEGV_ACCERR in the state bridge, reports of 08.11.2026:
    // the end credits on a Mobicel P11 Plus, a loading screen on a Samsung A70)
    const auto* begin = static_cast<const char*>(impl_->stackBottom);
    const auto* at = static_cast<const char*>(address);
    return at >= begin && at < begin + impl_->stackBytes;
#endif
}
void StackContext::resume() {
#ifdef _WIN32
    impl_->caller = GetCurrentFiber();
    SwitchToFiber(impl_->context);
#else
    std::fegetenv(&impl_->callerFloatingPoint);
    std::fesetenv(&impl_->floatingPoint);
    RE4_SWITCH_START(&impl_->callerFake, impl_->stackBottom, impl_->stackBytes);
#ifdef RE4_UCONTEXT
    swapcontext(&impl_->callerContext, &impl_->self_);
#else
    impl_->context = jump_fcontext(impl_->context, impl_.get()).context;
#endif
    RE4_SWITCH_FINISH(impl_->callerFake, nullptr, nullptr);
#endif
}
void StackContext::yield() {
#ifdef _WIN32
    SwitchToFiber(impl_->caller);
#else
    std::fegetenv(&impl_->floatingPoint);
    std::fesetenv(&impl_->callerFloatingPoint);
    RE4_SWITCH_START(&impl_->selfFake, impl_->callerBottom, impl_->callerBytes);
#ifdef RE4_UCONTEXT
    swapcontext(&impl_->self_, &impl_->callerContext);
#else
    impl_->caller = jump_fcontext(impl_->caller, impl_.get()).context;
#endif
    RE4_SWITCH_FINISH(impl_->selfFake, &impl_->callerBottom, &impl_->callerBytes);
#endif
}
} // namespace re4::port::detail
