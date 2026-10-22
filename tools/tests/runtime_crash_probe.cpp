#include "mobile/system/CrashHandler.h"
#include <cassert>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

int main(int argc, char** argv) {
    assert(argc == 2);
    char directory[] = "/tmp/galaxy-crash-XXXXXX";
    assert(mkdtemp(directory));
    auto path = std::filesystem::path(directory) / "galaxy_crash.log";
    pid_t child = fork(); assert(child >= 0);
    if (child == 0) {
        struct rlimit limit{0, 0}; setrlimit(RLIMIT_CORE, &limit);
        mobile_system::installCrashHandler(directory);
        if (argv[1][0] == 't') mobile_system::installCrashHandler(directory);
        if (argv[1][0] == 'n') _exit(0);
        if (argv[1][0] == 'w') { std::thread worker([] { raise(SIGABRT); }); worker.join(); }
        else raise(SIGSEGV);
        _exit(99);
    }
    int status = 0; assert(waitpid(child, &status, 0) == child);
    if (argv[1][0] == 'n') assert(WIFEXITED(status) && WEXITSTATUS(status) == 0 && std::filesystem::file_size(path) == 0);
    else {
        assert(WIFSIGNALED(status));
        assert(WTERMSIG(status) == (argv[1][0] == 'w' ? SIGABRT : SIGSEGV));
        std::ifstream input(path); std::string text((std::istreambuf_iterator<char>(input)), {});
        for (const char* key : {"signal=0x", "address=0x", "pc=0x", "image_base=0x"}) assert(text.find(key) != std::string::npos);
        assert(text.size() < 512);
    }
    std::filesystem::remove_all(directory);
}
