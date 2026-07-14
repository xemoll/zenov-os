class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    std::vector<Call> parse() {
        expect(Kind::identifier, "fn");
        expect(Kind::identifier, "main");
        expect(Kind::punct, "(");
        expect(Kind::punct, ")");
        expect(Kind::punct, "{");
        std::vector<Call> calls;
        while (!is(Kind::punct, "}")) calls.push_back(parse_call());
        expect(Kind::punct, "}");
        expect(Kind::end, "");
        return calls;
    }

private:
    std::vector<Token> tokens_;
    std::size_t index_{0};
    const Token& current() const { return tokens_.at(index_); }
    bool is(Kind kind, const std::string& text = "") const {
        return current().kind == kind && (text.empty() || current().text == text);
    }
    Token expect(Kind kind, const std::string& text) {
        const Token token = current();
        if (token.kind != kind || (!text.empty() && token.text != text)) {
            const std::string wanted = text.empty() ? "token" : text;
            throw Error(std::to_string(token.line) + ":" + std::to_string(token.column) +
                        ": expected " + wanted + ", got '" + token.text + "'");
        }
        ++index_;
        return token;
    }
    Call parse_call() {
        const Token name = expect(Kind::identifier, "");
        expect(Kind::punct, "(");
        Call call;
        call.name = name.text;
        call.line = name.line;
        if (!is(Kind::punct, ")")) {
            for (;;) {
                if (is(Kind::string_lit)) {
                    call.string_args.push_back(current().text);
                    call.number_args.push_back(0);
                    call.is_string.push_back(true);
                    ++index_;
                } else if (is(Kind::number)) {
                    const Token token = current();
                    ++index_;
                    std::size_t consumed = 0;
                    const auto value = std::stoll(token.text, &consumed, 0);
                    if (consumed != token.text.size()) throw Error("invalid number at line " + std::to_string(token.line));
                    call.string_args.emplace_back();
                    call.number_args.push_back(value);
                    call.is_string.push_back(false);
                } else {
                    throw Error("line " + std::to_string(current().line) + ": expected literal argument");
                }
                if (!is(Kind::punct, ",")) break;
                ++index_;
            }
        }
        expect(Kind::punct, ")");
        expect(Kind::punct, ";");
        return call;
    }
};

[[noreturn]] void validation_error(const Call& call, const std::string& message) {
    throw Error("line " + std::to_string(call.line) + ": " + message);
}

