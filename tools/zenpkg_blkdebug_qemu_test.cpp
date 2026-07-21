#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr const char* kPrompt = "zenov> ";
constexpr const char* kBreakEvent = "pwritev";

struct Options {
    std::filesystem::path boot;
    std::filesystem::path fixtures;
    std::filesystem::path output;
    std::string qemu = "qemu-system-i386";
};

std::string errno_text(const char* operation) {
    return std::string(operation) + ": " + std::strerror(errno);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) throw std::runtime_error("cannot determine file size: " + path.string());
    std::string text(static_cast<std::size_t>(end), '\0');
    input.seekg(0, std::ios::beg);
    if (!text.empty()) input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!input && !text.empty()) throw std::runtime_error("short read: " + path.string());
    return text;
}

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot create: " + path.string());
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("short write: " + path.string());
}

bool contains(const std::filesystem::path& path, std::string_view needle) {
    const std::string text = read_text(path);
    return text.find(needle) != std::string::npos;
}

void wait_for_text(const std::filesystem::path& path, std::string_view needle,
                   std::chrono::milliseconds timeout = 60s) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
        if (contains(path, needle)) return;
        std::this_thread::sleep_for(50ms);
    }
    throw std::runtime_error("missing serial marker '" + std::string(needle) + "' in " + path.string());
}

std::string json_escape(std::string_view input) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string output;
    output.reserve(input.size() + 16U);
    for (const unsigned char value : input) {
        switch (value) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (value < 0x20U) {
                output += "\\u00";
                output.push_back(hex[value >> 4U]);
                output.push_back(hex[value & 0x0FU]);
            } else {
                output.push_back(static_cast<char>(value));
            }
        }
    }
    return output;
}

class QmpClient {
public:
    QmpClient(const std::filesystem::path& socket_path, std::chrono::milliseconds timeout = 20s) {
        if (socket_path.string().size() >= sizeof(sockaddr_un::sun_path))
            throw std::runtime_error("QMP socket path is too long: " + socket_path.string());
        fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ < 0) throw std::runtime_error(errno_text("socket"));
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, socket_path.c_str(), socket_path.string().size() + 1U);
        const auto deadline = Clock::now() + timeout;
        while (::connect(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            if (errno != ENOENT && errno != ECONNREFUSED) {
                const std::string message = errno_text("connect QMP");
                close();
                throw std::runtime_error(message);
            }
            if (Clock::now() >= deadline) {
                close();
                throw std::runtime_error("QMP socket did not become ready: " + socket_path.string());
            }
            std::this_thread::sleep_for(50ms);
        }
        const std::string greeting = read_line(deadline);
        if (greeting.find("\"QMP\"") == std::string::npos) {
            close();
            throw std::runtime_error("invalid QMP greeting: " + greeting);
        }
        command("qmp_capabilities");
    }

    ~QmpClient() { close(); }
    QmpClient(const QmpClient&) = delete;
    QmpClient& operator=(const QmpClient&) = delete;

    std::string command(std::string_view execute, std::string_view arguments_json = {},
                        std::chrono::milliseconds timeout = 20s) {
        const std::uint64_t id = next_id_++;
        std::string request = "{\"execute\":\"" + json_escape(execute) + "\"";
        if (!arguments_json.empty()) request += ",\"arguments\":" + std::string(arguments_json);
        request += ",\"id\":" + std::to_string(id) + "}\r\n";
        write_all(request);
        const auto deadline = Clock::now() + timeout;
        while (true) {
            const std::string line = read_line(deadline);
            if (!has_id(line, id)) continue;
            if (line.find("\"error\"") != std::string::npos)
                throw std::runtime_error("QMP command failed: " + line);
            return line;
        }
    }

    std::string hmp(std::string_view command_line, std::chrono::milliseconds timeout = 60s) {
        const std::string arguments = "{\"command-line\":\"" + json_escape(command_line) + "\"}";
        return command("human-monitor-command", arguments, timeout);
    }

    void close() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_ = -1;
    std::uint64_t next_id_ = 1U;
    std::string buffer_;

    static bool has_id(const std::string& line, std::uint64_t expected) {
        std::size_t position = line.find("\"id\"");
        if (position == std::string::npos) return false;
        position = line.find(':', position + 4U);
        if (position == std::string::npos) return false;
        ++position;
        while (position < line.size() && (line[position] == ' ' || line[position] == '\t')) ++position;
        std::uint64_t actual = 0U;
        bool saw_digit = false;
        while (position < line.size() && line[position] >= '0' && line[position] <= '9') {
            saw_digit = true;
            actual = actual * 10U + static_cast<std::uint64_t>(line[position] - '0');
            ++position;
        }
        return saw_digit && actual == expected;
    }

    void write_all(std::string_view bytes) {
        std::size_t written = 0U;
        while (written < bytes.size()) {
            const ssize_t result = ::send(fd_, bytes.data() + written, bytes.size() - written, MSG_NOSIGNAL);
            if (result < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error(errno_text("send QMP"));
            }
            if (result == 0) throw std::runtime_error("QMP connection closed while writing");
            written += static_cast<std::size_t>(result);
        }
    }

    std::string read_line(Clock::time_point deadline) {
        while (true) {
            const std::size_t newline = buffer_.find('\n');
            if (newline != std::string::npos) {
                std::string line = buffer_.substr(0U, newline);
                buffer_.erase(0U, newline + 1U);
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
                if (!line.empty()) return line;
                continue;
            }
            const auto now = Clock::now();
            if (now >= deadline) throw std::runtime_error("timed out waiting for QMP message");
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            pollfd descriptor{fd_, POLLIN, 0};
            const int timeout_ms = static_cast<int>(std::min<std::int64_t>(remaining.count(), 1000));
            const int status = ::poll(&descriptor, 1, timeout_ms);
            if (status < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error(errno_text("poll QMP"));
            }
            if (status == 0) continue;
            if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
                throw std::runtime_error("QMP connection closed");
            char chunk[65536];
            const ssize_t received = ::recv(fd_, chunk, sizeof(chunk), 0);
            if (received < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error(errno_text("receive QMP"));
            }
            if (received == 0) throw std::runtime_error("QMP connection closed");
            buffer_.append(chunk, static_cast<std::size_t>(received));
        }
    }
};

struct RunningQemu {
    pid_t pid = -1;
    std::unique_ptr<QmpClient> qmp;
    std::filesystem::path stderr_path;
};

std::vector<char*> argv_view(std::vector<std::string>& values) {
    std::vector<char*> output;
    output.reserve(values.size() + 1U);
    for (std::string& value : values) output.push_back(value.data());
    output.push_back(nullptr);
    return output;
}

RunningQemu launch_qemu(const Options& options, const std::filesystem::path& runtime,
                        const std::filesystem::path& serial, const std::filesystem::path& qmp_socket,
                        const std::filesystem::path& stderr_path, bool use_blkdebug) {
    std::error_code error;
    std::filesystem::remove(serial, error);
    error.clear();
    std::filesystem::remove(qmp_socket, error);
    error.clear();
    std::filesystem::remove(stderr_path, error);

    std::vector<std::string> arguments{
        options.qemu,
        "-drive", "file=" + options.boot.string() + ",format=raw,if=floppy",
    };
    if (use_blkdebug) {
        arguments.insert(arguments.end(), {
            "-blockdev", "driver=file,node-name=runtime-file,filename=" + runtime.string() + ",cache.direct=on,cache.no-flush=off",
            "-blockdev", "driver=raw,node-name=runtime-raw,file=runtime-file",
            "-blockdev", "driver=blkdebug,node-name=runtime-debug,image=runtime-raw",
            "-device", "ide-hd,drive=runtime-debug,bus=ide.0,unit=0",
        });
    } else {
        arguments.insert(arguments.end(), {
            "-drive", "file=" + runtime.string() + ",format=raw,if=ide,index=0,media=disk,cache=none",
        });
    }
    arguments.insert(arguments.end(), {
        "-boot", "a", "-m", "32M", "-machine", "pc,vmport=off", "-vga", "std",
        "-display", "none", "-serial", "file:" + serial.string(),
        "-qmp", "unix:" + qmp_socket.string() + ",server=on,wait=off", "-monitor", "none",
        "-no-reboot", "-no-shutdown",
    });

    const int stderr_fd = ::open(stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (stderr_fd < 0) throw std::runtime_error(errno_text("open QEMU stderr"));
    const int null_fd = ::open("/dev/null", O_WRONLY);
    if (null_fd < 0) {
        ::close(stderr_fd);
        throw std::runtime_error(errno_text("open /dev/null"));
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(stderr_fd);
        ::close(null_fd);
        throw std::runtime_error(errno_text("fork QEMU"));
    }
    if (pid == 0) {
        if (::dup2(null_fd, STDOUT_FILENO) < 0 || ::dup2(stderr_fd, STDERR_FILENO) < 0) _exit(126);
        ::close(null_fd);
        ::close(stderr_fd);
        std::vector<char*> child_argv = argv_view(arguments);
        ::execvp(child_argv[0], child_argv.data());
        _exit(127);
    }
    ::close(null_fd);
    ::close(stderr_fd);
    RunningQemu running{pid, nullptr, stderr_path};
    try {
        running.qmp = std::make_unique<QmpClient>(qmp_socket);
    } catch (...) {
        ::kill(pid, SIGKILL);
        ::waitpid(pid, nullptr, 0);
        throw;
    }
    return running;
}

void stop_qemu(RunningQemu& running) {
    if (running.qmp) {
        try { running.qmp->command("stop", {}, 3s); } catch (...) {}
        try { running.qmp->command("quit", {}, 3s); } catch (...) {}
        running.qmp->close();
        running.qmp.reset();
    }
    if (running.pid <= 0) return;
    const auto deadline = Clock::now() + 10s;
    int status = 0;
    while (Clock::now() < deadline) {
        const pid_t result = ::waitpid(running.pid, &status, WNOHANG);
        if (result == running.pid) {
            running.pid = -1;
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
                throw std::runtime_error("QEMU exited abnormally");
            return;
        }
        if (result < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error(errno_text("waitpid QEMU"));
        }
        std::this_thread::sleep_for(50ms);
    }
    ::kill(running.pid, SIGKILL);
    ::waitpid(running.pid, &status, 0);
    running.pid = -1;
    throw std::runtime_error("QEMU did not exit after quit");
}

void stop_qemu_noexcept(RunningQemu& running) noexcept {
    try { stop_qemu(running); } catch (...) {
        if (running.pid > 0) {
            ::kill(running.pid, SIGKILL);
            ::waitpid(running.pid, nullptr, 0);
            running.pid = -1;
        }
    }
}

void wait_boot(const std::filesystem::path& serial) {
    for (const char* marker : {
             "ZENOVOS_BOOT_OK",
             "ZENOVFS_MOUNT_OK",
             "ZENOV_GUARD_READY",
             "ZENREPO_READY trust=verified packages=2",
             "ZENPKG_SHA256_OK",
             "ZENPKG_MANAGER_READY",
             kPrompt,
         }) {
        wait_for_text(serial, marker);
    }
}

std::string key_for(char character) {
    if ((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9'))
        return std::string(1U, character);
    if (character >= 'A' && character <= 'Z')
        return "shift-" + std::string(1U, static_cast<char>(character - 'A' + 'a'));
    switch (character) {
    case ' ': return "spc";
    case '.': return "dot";
    case '-': return "minus";
    case '_': return "shift-minus";
    case '/': return "slash";
    default: throw std::runtime_error("unsupported QEMU key character");
    }
}

void send_text(QmpClient& qmp, std::string_view text) {
    for (const char character : text) {
        qmp.hmp("sendkey " + key_for(character) + " 10");
        std::this_thread::sleep_for(8ms);
    }
}

void send_command(QmpClient& qmp, std::string_view command) {
    send_text(qmp, command);
    qmp.hmp("sendkey ret 10");
}

std::string qemu_io(QmpClient& qmp, std::string_view command,
                    std::chrono::milliseconds timeout = 60s) {
    return qmp.hmp("qemu-io runtime-debug \"" + std::string(command) + "\"", timeout);
}

void require_empty(const std::filesystem::path& path, const char* label) {
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size != 0U) throw std::runtime_error(std::string(label) + " is not empty: " + path.string());
}

void fault_boot(const Options& options, const std::filesystem::path& runtime,
                const std::string& name, std::uint32_t ordinal, std::string_view guest_command) {
    if (!ordinal) throw std::runtime_error("breakpoint ordinal must be positive");
    const auto serial = options.output / ("serial-" + name + "-fault.log");
    const auto qmp_socket = options.output / ("qmp-" + name + "-fault.sock");
    const auto stderr_path = options.output / ("qemu-" + name + "-fault.stderr");
    const auto evidence_path = options.output / ("qmp-" + name + "-break.json");
    const std::string tag = "zenpkg-" + name;
    RunningQemu running = launch_qemu(options, runtime, serial, qmp_socket, stderr_path, true);
    std::string status;
    try {
        wait_boot(serial);
        const std::string armed = qemu_io(*running.qmp, "break " + std::string(kBreakEvent) + " " + tag);
        if (armed.find("Could not") != std::string::npos || armed.find("not found") != std::string::npos)
            throw std::runtime_error("cannot arm blkdebug breakpoint: " + armed);
        send_command(*running.qmp, guest_command);
        for (std::uint32_t hit = 1U; hit <= ordinal; ++hit) {
            const std::string waited = qemu_io(*running.qmp, "wait_break " + tag, 60s);
            if (waited.find("Could not") != std::string::npos)
                throw std::runtime_error("cannot wait for blkdebug breakpoint: " + waited);
            if (hit < ordinal) {
                const std::string resumed = qemu_io(*running.qmp, "resume " + tag);
                if (resumed.find("Could not") != std::string::npos)
                    throw std::runtime_error("cannot resume blkdebug breakpoint: " + resumed);
            }
        }
        status = running.qmp->command("query-status");
        const std::string evidence =
            "{\n"
            "  \"event\": \"" + std::string(kBreakEvent) + "\",\n"
            "  \"guest_command\": \"" + json_escape(guest_command) + "\",\n"
            "  \"hit_count\": " + std::to_string(ordinal) + ",\n"
            "  \"ordinal\": " + std::to_string(ordinal) + ",\n"
            "  \"query_status_raw\": \"" + json_escape(status) + "\",\n"
            "  \"tag\": \"" + json_escape(tag) + "\"\n"
            "}\n";
        write_text(evidence_path, evidence);
        stop_qemu(running);
    } catch (...) {
        stop_qemu_noexcept(running);
        throw;
    }
    if (contains(serial, "ZENPKG_CACHE_FETCH_COMMIT_OK"))
        throw std::runtime_error("fault scenario completed before breakpoint crash: " + name);
    require_empty(stderr_path, "fault QEMU stderr");
}

void recovery_boot(const Options& options, const std::filesystem::path& runtime,
                   const std::string& name) {
    const auto serial = options.output / ("serial-" + name + "-recovery.log");
    const auto qmp_socket = options.output / ("qmp-" + name + "-recovery.sock");
    const auto stderr_path = options.output / ("qemu-" + name + "-recovery.stderr");
    RunningQemu running = launch_qemu(options, runtime, serial, qmp_socket, stderr_path, false);
    try {
        wait_boot(serial);
        send_command(*running.qmp, "pkg transport resume hello-native");
        const auto deadline = Clock::now() + 60s;
        while (Clock::now() < deadline) {
            if (contains(serial, "ZENPKG_CACHE_FETCH_COMMIT_OK name=hello-native version=0.2.0") ||
                contains(serial, "ZENPKG_CACHE_HIT name=hello-native version=0.2.0")) break;
            std::this_thread::sleep_for(50ms);
        }
        if (!contains(serial, "ZENPKG_CACHE_FETCH_COMMIT_OK name=hello-native version=0.2.0") &&
            !contains(serial, "ZENPKG_CACHE_HIT name=hello-native version=0.2.0"))
            throw std::runtime_error("recovery did not produce verified cache object: " + name);
        send_command(*running.qmp, "pkg cache verify");
        wait_for_text(serial, "ZENPKG_CACHE_VERIFY_OK objects=1 partials=0");
        send_command(*running.qmp, "fsck");
        wait_for_text(serial, "ZENOVFS_FSCK_OK");
        stop_qemu(running);
    } catch (...) {
        stop_qemu_noexcept(running);
        throw;
    }
    const std::string text = read_text(serial);
    for (const char* marker : {"PANIC", "ASSERT", "DOUBLE FAULT", "ZENPKG_CACHE_INIT_REJECTED"}) {
        if (text.find(marker) != std::string::npos)
            throw std::runtime_error("recovery " + name + " contains forbidden marker: " + marker);
    }
    require_empty(stderr_path, "recovery QEMU stderr");
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        auto require_value = [&](const char* name) -> std::string {
            if (index + 1 >= argc) throw std::runtime_error(std::string("missing value for ") + name);
            return argv[++index];
        };
        if (argument == "--boot") options.boot = require_value("--boot");
        else if (argument == "--fixtures") options.fixtures = require_value("--fixtures");
        else if (argument == "--out") options.output = require_value("--out");
        else if (argument == "--qemu") options.qemu = require_value("--qemu");
        else throw std::runtime_error("unknown argument: " + argument);
    }
    if (options.boot.empty() || options.fixtures.empty() || options.output.empty())
        throw std::runtime_error("usage: zenpkg-blkdebug-qemu-test --boot IMAGE --fixtures DIR --out DIR [--qemu PATH]");
    return options;
}

struct Scenario {
    const char* name;
    const char* fixture;
    std::uint32_t ordinal;
};

int run(int argc, char** argv) {
    const Options options = parse_options(argc, argv);
    std::filesystem::create_directories(options.output);
    const std::vector<Scenario> scenarios{
        {"chunk-first-write", "resume.img", 1U},
        {"chunk-second-write", "resume.img", 2U},
        {"chunk-metadata-sync", "resume.img", 8U},
        {"rename-first-write", "ready.img", 1U},
    };
    std::string summary;
    for (const Scenario& scenario : scenarios) {
        const auto source = options.fixtures / scenario.fixture;
        const auto runtime = options.output / ("runtime-" + std::string(scenario.name) + ".img");
        std::error_code error;
        std::filesystem::copy_file(source, runtime, std::filesystem::copy_options::overwrite_existing, error);
        if (error) throw std::runtime_error("cannot copy runtime fixture: " + error.message());
        fault_boot(options, runtime, scenario.name, scenario.ordinal, "pkg transport resume hello-native");
        recovery_boot(options, runtime, scenario.name);
        summary += "ZENPKG_BLKDEBUG_BREAKPOINT_SCENARIO_OK name=" + std::string(scenario.name) +
            " event=" + kBreakEvent + " ordinal=" + std::to_string(scenario.ordinal) + "\n";
    }
    write_text(options.output / "summary.log", summary);
    std::cout << summary;
    std::cout << "ZENPKG_BLKDEBUG_LIVE_CRASHES_OK scenarios=" << scenarios.size()
              << " qemu-boots=" << scenarios.size() * 2U << '\n';
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "zenpkg-blkdebug-qemu-test: " << error.what() << '\n';
        return 1;
    }
}
