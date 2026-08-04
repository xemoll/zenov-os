#include "core/expression.hpp"

#include <cmath>
#include <optional>
#include <utility>

namespace zenapps {
namespace {

class Parser {
public:
    explicit Parser(QString text) : text_(std::move(text)) {}

    ExpressionResult parse() {
        skipSpaces();
        const std::optional<double> value = parseExpression();
        skipSpaces();
        if (!value.has_value()) {
            return failure_.isEmpty() ? fail("Invalid expression") : fail(failure_);
        }
        if (position_ != text_.size()) {
            return fail(QString("Unexpected token at position %1").arg(position_ + 1));
        }
        if (!std::isfinite(*value)) {
            return fail("Result is not finite");
        }
        ExpressionResult result;
        result.ok = true;
        result.value = *value;
        result.formatted = QString::number(*value, 'g', 12);
        return result;
    }

private:
    std::optional<double> parseExpression() {
        auto left = parseTerm();
        if (!left.has_value()) {
            return std::nullopt;
        }
        while (true) {
            skipSpaces();
            if (consume('+')) {
                const auto right = parseTerm();
                if (!right.has_value()) return std::nullopt;
                *left += *right;
            } else if (consume('-')) {
                const auto right = parseTerm();
                if (!right.has_value()) return std::nullopt;
                *left -= *right;
            } else {
                break;
            }
        }
        return left;
    }

    std::optional<double> parseTerm() {
        auto left = parseUnary();
        if (!left.has_value()) {
            return std::nullopt;
        }
        while (true) {
            skipSpaces();
            if (consume('*')) {
                const auto right = parseUnary();
                if (!right.has_value()) return std::nullopt;
                *left *= *right;
            } else if (consume('/')) {
                const auto right = parseUnary();
                if (!right.has_value()) return std::nullopt;
                if (std::abs(*right) < 1e-15) {
                    failure_ = "Division by zero";
                    return std::nullopt;
                }
                *left /= *right;
            } else if (consume('%')) {
                const auto right = parseUnary();
                if (!right.has_value()) return std::nullopt;
                if (std::abs(*right) < 1e-15) {
                    failure_ = "Remainder by zero";
                    return std::nullopt;
                }
                *left = std::fmod(*left, *right);
            } else {
                break;
            }
        }
        return left;
    }

    std::optional<double> parseUnary() {
        skipSpaces();
        if (consume('+')) return parseUnary();
        if (consume('-')) {
            const auto value = parseUnary();
            return value.has_value() ? std::optional<double>(-*value) : std::nullopt;
        }
        return parsePrimary();
    }

    std::optional<double> parsePrimary() {
        skipSpaces();
        if (consume('(')) {
            auto value = parseExpression();
            skipSpaces();
            if (!consume(')')) {
                failure_ = "Missing closing parenthesis";
                return std::nullopt;
            }
            return value;
        }

        const qsizetype start = position_;
        bool sawDigit = false;
        bool sawDot = false;
        while (position_ < text_.size()) {
            const QChar character = text_.at(position_);
            if (character.isDigit()) {
                sawDigit = true;
                ++position_;
            } else if (character == '.' && !sawDot) {
                sawDot = true;
                ++position_;
            } else {
                break;
            }
        }
        if (!sawDigit) {
            failure_ = QString("Expected a number at position %1").arg(position_ + 1);
            return std::nullopt;
        }

        bool ok = false;
        const double value = text_.mid(start, position_ - start).toDouble(&ok);
        if (!ok || !std::isfinite(value)) {
            failure_ = "Invalid number";
            return std::nullopt;
        }
        return value;
    }

    bool consume(QChar expected) {
        if (position_ < text_.size() && text_.at(position_) == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void skipSpaces() {
        while (position_ < text_.size() && text_.at(position_).isSpace()) {
            ++position_;
        }
    }

    static ExpressionResult fail(const QString& message) {
        ExpressionResult result;
        result.error = message;
        return result;
    }

    QString text_;
    qsizetype position_ = 0;
    QString failure_;
};

}  // namespace

ExpressionResult evaluateExpression(const QString& expression) {
    return Parser(expression).parse();
}

}  // namespace zenapps
