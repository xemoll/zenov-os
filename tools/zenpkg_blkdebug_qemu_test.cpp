#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

std::string errno_text(const char* operation) {
    return std::string(operation) + ": " + std::strerror(errno);
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

class Client {
public:
    Client(const std::string& path, std::chrono::seconds timeout) : timeout_(timeout) {
        if (path.size() >= sizeof(sockaddr_un::sun_path)) throw std::runtime_error("QMP socket path too long");
        fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ < 0) throw std::runtime_error(errno_text("socket"));
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1U);
        const auto deadline = Clock::now() + timeout_;
        while (::connect(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            if (errno != ENOENT && errno != ECONNREFUSED) throw std::runtime_error(errno_text("connect"));
            if (Clock::now() >= deadline) throw std::runtime_error("QMP socket did not become ready");
            std::this_thread::sleep_for(25ms);
        }
        const std::string greeting = read_line(deadline);
        if (greeting.find("\"QMP\"") == std::string::npos) throw std::runtime_error("invalid QMP greeting");
        (void)command("qmp_capabilities", {});
    }

    ~Client() { if (fd_ >= 0) ::close(fd_); }
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    std::string hmp(std::string_view command_line) {
        return command("human-monitor-command", "{\"command-line\":\"" + json_escape(command_line) + "\"}");
    }

private:
    int fd_ = -1;
    std::uint64_t next_id_ = 1U;
    std::string buffer_;
    std::chrono::seconds timeout_;

    static bool response_has_id(const std::string& line, std::uint64_t expected) {
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

    void send_all(std::string_view bytes) {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const ssize_t sent = ::send(fd_, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error(errno_text("send"));
            }
            if (sent == 0) throw std::runtime_error("QMP connection closed while writing");
            offset += static_cast<std::size_t>(sent);
        }
    }

    std::string read_line(Clock::time_point deadline) {
        while (true) {
            const std::size_t newline = buffer_.find('\n');
            if (newline != std::string::npos) {
                std::string line = buffer_.substr(0, newline);
                buffer_.erase(0, newline + 1U);
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
                if (!line.empty()) return line;
                continue;
            }
            const auto now = Clock::now();
            if (now >= deadline) throw std::runtime_error("timed out waiting for QMP response");
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            pollfd descriptor{fd_, POLLIN, 0};
            const int wait_ms = static_cast<int>(std::min<std::int64_t>(remaining.count(), 1000));
            const int status = ::poll(&descriptor, 1, wait_ms);
            if (status < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error(errno_text("poll"));
            }
            if (status == 0) continue;
            if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) throw std::runtime_error("QMP connection closed");
            char chunk[16384];
            const ssize_t received = ::recv(fd_, chunk, sizeof(chunk), 0);
            if (received < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error(errno_text("recv"));
            }
            if (received == 0) throw std::runtime_error("QMP connection closed");
            buffer_.append(chunk, static_cast<std::size_t>(received));
        }
    }

    std::string command(std::string_view execute, const std::string& arguments) {
        const std::uint64_t id = next_id_++;
        std::string request = "{\"execute\":\"" + json_escape(execute) + "\"";
        if (!arguments.empty()) request += ",\"arguments\":" + arguments;
        request += ",\"id\":" + std::to_string(id) + "}\r\n";
        send_all(request);
        const auto deadline = Clock::now() + timeout_;
        while (true) {
            const std::string line = read_line(deadline);
            if (!response_has_id(line, id)) continue;
            if (line.find("\"error\"") != std::string::npos) throw std::runtime_error("QMP command failed: " + line);
            return line;
        }
    }
};

int parse_timeout(const char* text) {
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value < 1 || value > 600) throw std::runtime_error("invalid timeout");
    return static_cast<int>(value);
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
            if (json_escape("a\n\"\\") != "a\\n\\\"\\\\") throw std::runtime_error("JSON escaping self-test failed");
            std::cout << "ZENPKG_QMP_HMP_CLIENT_SELF_TEST_OK\n";
            return 0;
        }
        if (argc < 3 || argc > 4) {
            std::cerr << "usage: zenpkg-qmp-hmp-client SOCKET HMP_COMMAND [TIMEOUT_SECONDS]\n";
            return 2;
        }
        const int timeout = argc == 4 ? parse_timeout(argv[3]) : 30;
        Client client(argv[1], std::chrono::seconds(timeout));
        std::cout << client.hmp(argv[2]) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "zenpkg-qmp-hmp-client: " << error.what() << '\n';
        return 1;
    }
}
