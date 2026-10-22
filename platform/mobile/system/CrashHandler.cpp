#include "CrashHandler.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

namespace mobile_system {
namespace {
int reportFd = -1;
uintptr_t imageBase = 0;
struct sigaction previousActions[NSIG];
bool handlersInstalled = false;

char* hex(char* out, const char* label, uintptr_t value) {
    while (*label) *out++ = *label++;
    *out++ = '0'; *out++ = 'x';
    for (int shift = int(sizeof(value) * 8) - 4; shift >= 0; shift -= 4)
        *out++ = "0123456789abcdef"[(value >> shift) & 15];
    *out++ = '\n';
    return out;
}

void crashSignalHandler(int sig, siginfo_t* info, void* context) {
    // No logger, allocator, stdio, JVM or filesystem-wide sync in a signal
    // handler: any of those may deadlock a crashing render/game thread.
    char report[512];
    char* end = hex(report, "signal=", uintptr_t(sig));
    end = hex(end, "code=", info ? uintptr_t(info->si_code) : 0);
    end = hex(end, "address=", info ? uintptr_t(info->si_addr) : 0);
    end = hex(end, "image_base=", imageBase);
    auto* uc = static_cast<ucontext_t*>(context);
#if defined(__aarch64__)
    if (uc) {
        end = hex(end, "pc=", uc->uc_mcontext.pc);
        end = hex(end, "lr=", uc->uc_mcontext.regs[30]);
        end = hex(end, "sp=", uc->uc_mcontext.sp);
    }
#elif defined(__x86_64__)
    if (uc) end = hex(end, "pc=", uc->uc_mcontext.gregs[REG_RIP]);
#else
    (void)uc;
#endif
    const char* p = report;
    while (reportFd >= 0 && p < end) {
        const ssize_t n = write(reportFd, p, size_t(end - p));
        if (n > 0) p += n;
        else if (n < 0 && errno == EINTR) continue;
        else break;
    }
    // Chain Android's previously installed crash action, if present, rather
    // than swallowing the system's native diagnostic/termination handler.
    sigaction(sig, &previousActions[sig], nullptr);
    if (raise(sig) != 0) _exit(128 + sig);
}
}  // namespace

void installCrashHandler(const char* privateDirectory) {
    if (!privateDirectory || !*privateDirectory) return;
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/galaxy_crash.log", privateDirectory) >= int(sizeof(path))) return;
    if (reportFd >= 0) close(reportFd);
    reportFd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (reportFd < 0) return;
    if (handlersInstalled) return;
    Dl_info image{};
    if (dladdr(reinterpret_cast<void*>(&installCrashHandler), &image)) imageBase = uintptr_t(image.dli_fbase);
    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = crashSignalHandler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
    const int signals[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
    for (int sig : signals) sigaction(sig, &sa, &previousActions[sig]);
    handlersInstalled = true;
}
}  // namespace mobile_system
