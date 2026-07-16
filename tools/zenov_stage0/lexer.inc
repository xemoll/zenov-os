namespace {

struct Error : std::runtime_error { using std::runtime_error::runtime_error; };

enum class Kind { identifier, string_lit, number, punct, end };

struct Token {
    Kind kind{};
    std::string text;
    int line{1};
    int column{1};
};

struct Call {
    std::string name;
    std::vector<std::string> string_args;
    std::vector<std::int64_t> number_args;
    std::vector<bool> is_string;
    int line{1};
};

struct Config {
    std::string system_name;
    std::string version;
    std::string prompt;
    std::uint8_t foreground{10};
    std::uint8_t background{0};
    std::uint32_t shell_line_capacity{256};
    std::uint32_t shell_history_capacity{64};
    std::vector<std::string> boot_messages;
    std::vector<std::pair<std::string, std::string>> commands;
    std::vector<std::pair<std::string, std::string>> files;
};

class Lexer {
public:
    explicit Lexer(std::string source) : source_(std::move(source)) {}

    std::vector<Token> run() {
        std::vector<Token> out;
        for (;;) {
            skip_space_and_comments();
            if (eof()) {
                out.push_back({Kind::end, "", line_, column_});
                return out;
            }
            const int line = line_;
            const int column = column_;
            const char ch = peek();
            if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
                std::string text;
                while (!eof()) {
                    const char c = peek();
                    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') break;
                    text.push_back(take());
                }
                out.push_back({Kind::identifier, text, line, column});
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(ch))) {
                std::string text;
                text.push_back(take());
                if (text == "0" && !eof() && (peek() == 'x' || peek() == 'X')) {
                    text.push_back(take());
                    while (!eof() && std::isxdigit(static_cast<unsigned char>(peek()))) text.push_back(take());
                } else {
                    while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) text.push_back(take());
                }
                out.push_back({Kind::number, text, line, column});
                continue;
            }
            if (ch == '"') {
                take();
                std::string text;
                bool closed = false;
                while (!eof()) {
                    char c = take();
                    if (c == '"') { closed = true; break; }
                    if (c == '\n' || c == '\r') fail(line, column, "newline in string literal");
                    if (c == '\\') {
                        if (eof()) fail(line, column, "unterminated escape");
                        const char e = take();
                        switch (e) {
                            case 'n': text.push_back('\n'); break;
                            case 'r': text.push_back('\r'); break;
                            case 't': text.push_back('\t'); break;
                            case '0': text.push_back('\0'); break;
                            case '\\': text.push_back('\\'); break;
                            case '"': text.push_back('"'); break;
                            default: fail(line_, column_, std::string("unsupported escape \\") + e);
                        }
                    } else {
                        text.push_back(c);
                    }
                }
                if (!closed) fail(line, column, "unterminated string literal");
                out.push_back({Kind::string_lit, text, line, column});
                continue;
            }
            if (ch == '(' || ch == ')' || ch == '{' || ch == '}' || ch == ',' || ch == ';') {
                out.push_back({Kind::punct, std::string(1, take()), line, column});
                continue;
            }
            fail(line, column, std::string("unexpected character '") + ch + "'");
        }
    }

private:
    std::string source_;
    std::size_t pos_{0};
    int line_{1};
    int column_{1};

    bool eof() const { return pos_ >= source_.size(); }
    char peek(std::size_t ahead = 0) const { return pos_ + ahead < source_.size() ? source_[pos_ + ahead] : '\0'; }
    char take() {
        const char c = source_[pos_++];
        if (c == '\n') { ++line_; column_ = 1; } else { ++column_; }
        return c;
    }
    [[noreturn]] static void fail(int line, int column, const std::string& message) {
        throw Error(std::to_string(line) + ":" + std::to_string(column) + ": " + message);
    }
    void skip_space_and_comments() {
        for (;;) {
            while (!eof() && std::isspace(static_cast<unsigned char>(peek()))) take();
            if (peek() == '#') {
                while (!eof() && take() != '\n') {}
                continue;
            }
            if (peek() == '/' && peek(1) == '/') {
                take(); take();
                while (!eof() && take() != '\n') {}
                continue;
            }
            return;
        }
    }
};
