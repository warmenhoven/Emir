#include "discovery.hpp"

#include <cxxopts.hpp>
#include <fmt/format.h>

#include <array>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
    #include <sys/select.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

namespace {

#if defined(__unix__) || defined(__APPLE__)
std::vector<const char *> BuildArgv(const std::filesystem::path &headlessPath,
                                    const std::vector<std::string> &extraArgs) {
    std::vector<const char *> argv;
    argv.push_back(headlessPath.c_str());
    for (const auto &arg : extraArgs) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);
    return argv;
}
bool WriteAll(int fd, const char *buf, size_t n) {
    size_t written = 0;
    while (written < n) {
        ssize_t result = write(fd, buf + written, n - written);
        if (result == -1) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (result == 0) {
            return false;
        }
        written += static_cast<size_t>(result);
    }
    return true;
}
#endif

} // namespace

int main(int argc, char **argv) {
    // ---- Parse our flags ----
    cxxopts::Options options{"ymir-dbg", "Ymir headless debug CLI"};
    // clang-format off
    options.add_options()
        ("headless-path", "Explicit path to ymir-headless binary",
         cxxopts::value<std::string>())
        ("h,help",         "Show help");
    // clang-format on

    // Allow unknown flags so they flow through as headless args.
    options.allow_unrecognised_options();
    auto result = options.parse(argc, argv);

    if (result.count("help") > 0) {
        fmt::print("{}", options.help());
        return 0;
    }

    // ---- Resolve headless binary ----
    std::string headlessFlag;
    if (result.count("headless-path") > 0) {
        headlessFlag = result["headless-path"].as<std::string>();
    }

    auto headlessPath = ymir::debug::find_headless_binary(headlessFlag);
    if (!headlessPath.has_value()) {
        fmt::print(stderr, "ymir-dbg: could not locate ymir-headless binary\n");
        return 1;
    }

    // ---- Gather unrecognised args for headless forwarding ----
    std::vector<std::string> extraArgs;
    for (const auto &arg : result.unmatched()) {
        extraArgs.push_back(arg);
    }

    // ---- Spawn headless subprocess ----
#if defined(__unix__) || defined(__APPLE__)
    int pipeStdin[2], pipeStdout[2];
    if (pipe(pipeStdin) == -1) {
        fmt::print(stderr, "ymir-dbg: pipe() failed: {}\n", std::strerror(errno));
        return 1;
    }
    if (pipe(pipeStdout) == -1) {
        close(pipeStdin[0]);
        close(pipeStdin[1]);
        fmt::print(stderr, "ymir-dbg: pipe() failed: {}\n", std::strerror(errno));
        return 1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipeStdin[0]);
        close(pipeStdin[1]);
        close(pipeStdout[0]);
        close(pipeStdout[1]);
        fmt::print(stderr, "ymir-dbg: fork() failed: {}\n", std::strerror(errno));
        return 1;
    }

    if (pid == 0) {
        // ---- Child: wire pipes and exec ----
        close(pipeStdin[1]);  // close write end of stdin pipe
        close(pipeStdout[0]); // close read end of stdout pipe

        if (dup2(pipeStdin[0], STDIN_FILENO) == -1 || dup2(pipeStdout[1], STDOUT_FILENO) == -1) {
            fmt::print(stderr, "ymir-dbg: dup2 failed: {}\n", std::strerror(errno));
            _exit(127);
        }

        close(pipeStdin[0]);
        close(pipeStdout[1]);

        auto argvVec = BuildArgv(headlessPath.value(), extraArgs);
        execvp(argvVec[0], const_cast<char *const *>(argvVec.data()));

        // execvp only returns on error
        fmt::print(stderr, "ymir-dbg: execvp failed: {}\n", std::strerror(errno));
        _exit(127);
    }

    // ---- Parent: close unused ends ----
    close(pipeStdin[0]);  // read end of stdin pipe
    close(pipeStdout[1]); // write end of stdout pipe

    // ---- Stdio relay loop ----
    bool running = true;
    bool stdinClosed = false;
    fd_set readfds;
    std::array<char, 4096> buffer;

    signal(SIGPIPE, SIG_IGN);

    while (running) {
        FD_ZERO(&readfds);
        FD_SET(pipeStdout[0], &readfds);
        if (!stdinClosed) {
            FD_SET(STDIN_FILENO, &readfds);
        }

        int maxFd = std::max(pipeStdout[0], STDIN_FILENO);
        // 100 ms prevents infinite block if headless hangs.
        struct timeval tv{0, 100000};
        int sel = select(maxFd + 1, &readfds, nullptr, nullptr, &tv);
        if (sel == -1) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        // Read from our stdin, write to headless stdin
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            ssize_t n = read(STDIN_FILENO, buffer.data(), buffer.size());
            if (n <= 0) {
                stdinClosed = true;
                close(pipeStdin[1]);
            } else {
                if (!WriteAll(pipeStdin[1], buffer.data(), static_cast<size_t>(n))) {
                    stdinClosed = true;
                    close(pipeStdin[1]);
                }
            }
        }

        // Read from headless stdout, write to our stdout
        if (FD_ISSET(pipeStdout[0], &readfds)) {
            ssize_t n = read(pipeStdout[0], buffer.data(), buffer.size());
            if (n <= 0) {
                running = false;
            } else {
                WriteAll(STDOUT_FILENO, buffer.data(), static_cast<size_t>(n));
            }
        }
    }

    // Wait for child and return its exit code
    int status = 0;
    int wp;
    do {
        wp = waitpid(pid, &status, 0);
    } while (wp == -1 && errno == EINTR);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#else
    fmt::print(stderr, "ymir-dbg: subprocess spawn not supported on this platform\n");
    return 1;
#endif
}
