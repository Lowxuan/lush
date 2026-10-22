#include "lush/core/lexer.h"

#include "lush/core/utf8.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <system_error>
#include <utility>

namespace lush::core {
namespace {

bool isHorizontalWhitespace(char value) noexcept {
    return value == ' ' || value == '\t';
}

bool isDigit(char value) noexcept {
    return value >= '0' && value <= '9';
}

bool isIdentifierStart(char value) noexcept {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || value == '_';
}

bool isIdentifierContinue(char value) noexcept {
    return isIdentifierStart(value) || isDigit(value);
}

char asciiLower(char value) noexcept {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return value;
}

bool equalsIgnoreCase(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (asciiLower(left[i]) != asciiLower(right[i])) {
            return false;
        }
    }
    return true;
}

TokenKind keywordKind(std::string_view text) noexcept {
    if (equalsIgnoreCase(text, "if")) return TokenKind::KwIf;
    if (equalsIgnoreCase(text, "else")) return TokenKind::KwElse;
    if (equalsIgnoreCase(text, "while")) return TokenKind::KwWhile;
    if (equalsIgnoreCase(text, "for")) return TokenKind::KwFor;
    if (equalsIgnoreCase(text, "in")) return TokenKind::KwIn;
    if (equalsIgnoreCase(text, "true")) return TokenKind::KwTrue;
    if (equalsIgnoreCase(text, "false")) return TokenKind::KwFalse;
    if (equalsIgnoreCase(text, "ref")) return TokenKind::KwRef;
    if (equalsIgnoreCase(text, "break")) return TokenKind::KwBreak;
    if (equalsIgnoreCase(text, "continue")) return TokenKind::KwContinue;
    if (equalsIgnoreCase(text, "def")) return TokenKind::KwDef;
    if (equalsIgnoreCase(text, "return")) return TokenKind::KwReturn;
    return TokenKind::Identifier;
}

int hexValue(char value) noexcept {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool hasNonZeroSignificand(std::string_view text) noexcept {
    for (char value : text) {
        if (value == 'e' || value == 'E') {
            break;
        }
        if (value >= '1' && value <= '9') {
            return true;
        }
    }
    return false;
}

} // namespace

Lexer::Lexer(const Source& source) : source_(source) {
    buildLogicalText();
}

void Lexer::buildLogicalText() {
    enum class Mode {
        Normal,
        String,
        Comment,
    };

    const std::string_view text = source_.text();
    logical_.reserve(text.size());
    map_.reserve(text.size());

    lastContinuationOffset_ = LexResult::npos;
    lastContinuationLogicalPos_ = LexResult::npos;
    seenNonNlAfterLastContinuation_ = false;

    auto append = [this](char value, std::size_t originalOffset) {
        logical_.push_back(value);
        map_.push_back(originalOffset);
    };

    Mode mode = Mode::Normal;
    bool escaped = false;

    std::size_t offset = source_.contentStart();
    while (offset < text.size()) {
        const char value = text[offset];

        if (mode == Mode::Comment) {
            append(value, offset);
            if (value == '\r' || value == '\n') {
                mode = Mode::Normal;
            }
            ++offset;
            continue;
        }

        if (mode == Mode::String) {
            append(value, offset);
            if (escaped) {
                escaped = false;
            } else if (value == '\\') {
                escaped = true;
            } else if (value == '"') {
                mode = Mode::Normal;
            }
            ++offset;
            continue;
        }

        if (value == '\\') {
            std::size_t next = offset + 1;
            while (next < text.size() && isHorizontalWhitespace(text[next])) {
                ++next;
            }

            if (next < text.size() && (text[next] == '\r' || text[next] == '\n')) {
                if (text[next] == '\r' && next + 1 < text.size() &&
                    text[next + 1] == '\n') {
                    ++next;
                }

                lastContinuationOffset_ = offset;
                lastContinuationLogicalPos_ = logical_.size();
                seenNonNlAfterLastContinuation_ = false;

                offset = next + 1;
                continue;
            }

            // This backslash cannot continue a line. Copy the rest verbatim so
            // later continuations do not change this first lexical error.
            while (offset < text.size()) {
                append(text[offset], offset);
                ++offset;
            }
            break;
        }

        append(value, offset);
        if (value == '"') {
            mode = Mode::String;
            escaped = false;
        } else if (value == '#') {
            mode = Mode::Comment;
        }
        ++offset;
    }
}

bool Lexer::atEnd() const noexcept {
    return pos_ >= logical_.size();
}

char Lexer::charAt(std::size_t offset) const noexcept {
    return offset < logical_.size() ? logical_[offset] : '\0';
}

Span Lexer::spanOf(std::size_t start, std::size_t end) const noexcept {
    if (start >= end) {
        const std::size_t original =
            start < map_.size() ? map_[start] : source_.size();
        return Span{original, original};
    }

    const std::size_t begin = map_[start];
    const std::size_t finish = map_[end - 1] + 1;
    return Span{begin, finish};
}

std::string Lexer::textOf(std::size_t start, std::size_t end) const {
    return logical_.substr(start, end - start);
}

bool Lexer::wsBeforeAt(std::size_t offset) const noexcept {
    if (offset == 0 || offset > logical_.size()) {
        return false;
    }
    const char previous = logical_[offset - 1];
    return previous == ' ' || previous == '\t';
}

void Lexer::emit(TokenKind kind, std::size_t start, std::size_t end, bool wsBefore,
                 std::string text) {
    if (kind != TokenKind::Newline && kind != TokenKind::EndOfFile &&
        lastContinuationLogicalPos_ != LexResult::npos &&
        (start >= lastContinuationLogicalPos_ || end > lastContinuationLogicalPos_)) {
        seenNonNlAfterLastContinuation_ = true;
    }

    const Span span = spanOf(start, end);
    Token token;
    token.kind = kind;
    token.offset = span.start;
    token.length = span.length();
    token.wsBefore = wsBefore;
    token.text = std::move(text);
    tokens_.push_back(std::move(token));
}

void Lexer::fail(ErrorCode code, std::size_t start, std::size_t end,
                 std::string message, std::string help) {
    diagnostic_ = Diagnostic{code, spanOf(start, end), std::move(message), std::move(help)};
}

void Lexer::scanComment() {
    ++pos_;
    while (!atEnd()) {
        const char value = charAt(pos_);
        if (value == '\r' || value == '\n') {
            return;
        }

        if (static_cast<unsigned char>(value) < 0x80U) {
            ++pos_;
            continue;
        }

        const auto decoded = decodeUtf8(logical_, pos_);
        if (!decoded) {
            fail(ErrorCode::L001, pos_, pos_ + 1, "非法 UTF-8 字节序列");
            return;
        }
        pos_ += decoded->length;
    }
}

void Lexer::scanNewline() {
    const std::size_t start = pos_;
    if (charAt(pos_) == '\r') {
        ++pos_;
        if (!atEnd() && charAt(pos_) == '\n') {
            ++pos_;
        }
    } else {
        ++pos_;
    }

    if (depth_ == 0) {
        emit(TokenKind::Newline, start, pos_, false, "\n");
    }
}

void Lexer::scanIdentifier() {
    const std::size_t start = pos_;
    while (!atEnd() && isIdentifierContinue(charAt(pos_))) {
        ++pos_;
    }

    std::string text = textOf(start, pos_);
    const TokenKind kind = keywordKind(text);
    emit(kind, start, pos_, wsBeforeAt(start), std::move(text));
}

void Lexer::scanNumber() {
    const std::size_t start = pos_;

    if (charAt(pos_) == '.') {
        ++pos_;
        while (!atEnd() && isDigit(charAt(pos_))) {
            ++pos_;
        }
        fail(ErrorCode::L004, start, pos_, "数值字面量格式错误或越界",
             "小数点两侧都必须有数字");
        return;
    }

    while (!atEnd() && isDigit(charAt(pos_))) {
        ++pos_;
    }

    bool isFloat = false;

    if (!atEnd() && charAt(pos_) == '.') {
        isFloat = true;
        ++pos_;
        if (atEnd() || !isDigit(charAt(pos_))) {
            fail(ErrorCode::L004, start, pos_, "数值字面量格式错误或越界",
                 "小数点两侧都必须有数字");
            return;
        }
        while (!atEnd() && isDigit(charAt(pos_))) {
            ++pos_;
        }
    }

    if (!atEnd() && (charAt(pos_) == 'e' || charAt(pos_) == 'E')) {
        isFloat = true;
        ++pos_;
        if (!atEnd() && (charAt(pos_) == '+' || charAt(pos_) == '-')) {
            ++pos_;
        }
        if (atEnd() || !isDigit(charAt(pos_))) {
            fail(ErrorCode::L004, start, pos_, "数值字面量格式错误或越界",
                 "指数必须包含数字");
            return;
        }
        while (!atEnd() && isDigit(charAt(pos_))) {
            ++pos_;
        }
    }

    if (!atEnd() &&
        (isIdentifierContinue(charAt(pos_)) || charAt(pos_) == '.')) {
        fail(ErrorCode::L004, start, pos_ + 1, "数值字面量格式错误或越界",
             "数值后不能紧跟标识符字符或多余的小数点");
        return;
    }

    const std::string_view text = std::string_view(logical_).substr(start, pos_ - start);

    if (isFloat) {
        double value = 0.0;
        const auto result =
            std::from_chars(text.data(), text.data() + text.size(), value,
                            std::chars_format::general);
        const bool consumedAll = result.ptr == text.data() + text.size();
        const bool underflow = value == 0.0 && hasNonZeroSignificand(text);
        if (result.ec != std::errc{} || !consumedAll || !std::isfinite(value) ||
            underflow) {
            fail(ErrorCode::L004, start, pos_, "数值字面量格式错误或越界",
                 "浮点字面量必须能表示为有限 double");
            return;
        }
        emit(TokenKind::Float, start, pos_, wsBeforeAt(start), std::string(text));
        return;
    }

    std::uint64_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        fail(ErrorCode::L004, start, pos_, "数值字面量格式错误或越界");
        return;
    }

    constexpr std::uint64_t kInt64MinMagnitude = std::uint64_t{1} << 63;
    if (value > kInt64MinMagnitude) {
        fail(ErrorCode::L004, start, pos_, "整数越界");
        return;
    }

    const TokenKind kind =
        value == kInt64MinMagnitude ? TokenKind::BoundaryInt : TokenKind::Int;
    emit(kind, start, pos_, wsBeforeAt(start), std::string(text));
}

void Lexer::scanString() {
    const std::size_t start = pos_;
    ++pos_;

    std::string decoded;

    while (true) {
        if (atEnd()) {
            fail(ErrorCode::L002, start, pos_, "字符串未闭合", "补上双引号");
            return;
        }

        const char value = charAt(pos_);

        if (value == '"') {
            ++pos_;
            emit(TokenKind::String, start, pos_, wsBeforeAt(start), std::move(decoded));
            return;
        }

        if (value == '\\') {
            const std::size_t escapeStart = pos_;
            ++pos_;
            if (atEnd()) {
                fail(ErrorCode::L002, start, pos_, "字符串未闭合", "补上双引号");
                return;
            }

            const char escape = charAt(pos_);
            if (escape == '\n' || escape == '\r') {
                fail(ErrorCode::L003, escapeStart, pos_ + 1, "未知转义序列",
                     "字符串内不能使用续行");
                return;
            }

            switch (escape) {
            case '\\':
                decoded.push_back('\\');
                ++pos_;
                break;
            case '"':
                decoded.push_back('"');
                ++pos_;
                break;
            case 'n':
                decoded.push_back('\n');
                ++pos_;
                break;
            case 'r':
                decoded.push_back('\r');
                ++pos_;
                break;
            case 't':
                decoded.push_back('\t');
                ++pos_;
                break;
            case '0':
                decoded.push_back('\0');
                ++pos_;
                break;
            case 'u': {
                if (pos_ + 5 > logical_.size()) {
                    fail(ErrorCode::L003, escapeStart, logical_.size(),
                         "Unicode 转义必须包含 4 位十六进制数字");
                    return;
                }

                char32_t codePoint = 0;
                for (std::size_t i = 1; i <= 4; ++i) {
                    const int digit = hexValue(charAt(pos_ + i));
                    if (digit < 0) {
                        fail(ErrorCode::L003, escapeStart, pos_ + i + 1,
                             "Unicode 转义必须包含 4 位十六进制数字");
                        return;
                    }
                    codePoint = (codePoint << 4) | static_cast<char32_t>(digit);
                }

                const std::size_t afterEscape = pos_ + 5;
                if (!appendUtf8(decoded, codePoint)) {
                    fail(ErrorCode::L003, escapeStart, afterEscape,
                         "Unicode 代理区不能直接编码");
                    return;
                }
                pos_ = afterEscape;
                break;
            }
            default:
                fail(ErrorCode::L003, escapeStart, pos_ + 1, "未知转义序列");
                return;
            }
            continue;
        }

        if (value == '\n' || value == '\r') {
            fail(ErrorCode::L002, start, pos_ + 1, "字符串未闭合",
                 "字符串不能包含裸换行");
            return;
        }

        if (static_cast<unsigned char>(value) >= 0x80U) {
            const auto decodedCodePoint = decodeUtf8(logical_, pos_);
            if (!decodedCodePoint) {
                fail(ErrorCode::L001, pos_, pos_ + 1, "非法 UTF-8 字节序列");
                return;
            }
            decoded.append(logical_.data() + pos_, decodedCodePoint->length);
            pos_ += decodedCodePoint->length;
            continue;
        }

        decoded.push_back(value);
        ++pos_;
    }
}

void Lexer::scanBackslash() {
    const std::size_t start = pos_;
    ++pos_;

    while (!atEnd() && isHorizontalWhitespace(charAt(pos_))) {
        ++pos_;
    }

    if (atEnd()) {
        lastContinuationOffset_ = start < map_.size() ? map_[start] : source_.size();
        lastContinuationLogicalPos_ = start;
        seenNonNlAfterLastContinuation_ = false;
        fail(ErrorCode::L005, start, pos_, "续行符位于 EOF");
        return;
    }

    fail(ErrorCode::L006, start, pos_ + 1, "孤立的续行符",
         "续行符后必须紧跟换行");
}

void Lexer::scanIllegal() {
    const std::size_t start = pos_;
    const char value = charAt(pos_);

    if (static_cast<unsigned char>(value) < 0x80U) {
        fail(ErrorCode::L001, start, start + 1, "非法字符");
        return;
    }

    const auto decoded = decodeUtf8(logical_, pos_);
    if (!decoded) {
        fail(ErrorCode::L001, start, start + 1, "非法 UTF-8 字节序列");
        return;
    }

    fail(ErrorCode::L001, start, start + decoded->length,
         "非法字符：非 ASCII 字符只允许出现在字符串或注释中");
}

bool Lexer::scanOperator() {
    const std::size_t start = pos_;
    enum class Match {
        None,
        Single,
        Double,
    };

    Match match = Match::None;
    TokenKind kind = TokenKind::EndOfFile;

    auto doubleMatch = [this](char first, char second) {
        return pos_ + 1 < logical_.size() && logical_[pos_] == first &&
               logical_[pos_ + 1] == second;
    };

    if (doubleMatch('=', '=')) {
        match = Match::Double;
        kind = TokenKind::Equal;
    } else if (doubleMatch('!', '=')) {
        match = Match::Double;
        kind = TokenKind::NotEqual;
    } else if (doubleMatch('<', '=')) {
        match = Match::Double;
        kind = TokenKind::LessEqual;
    } else if (doubleMatch('>', '=')) {
        match = Match::Double;
        kind = TokenKind::GreaterEqual;
    } else if (doubleMatch('&', '&')) {
        match = Match::Double;
        kind = TokenKind::AmpAmp;
    } else if (doubleMatch('|', '|')) {
        match = Match::Double;
        kind = TokenKind::PipePipe;
    } else if (doubleMatch('+', '+')) {
        match = Match::Double;
        kind = TokenKind::PlusPlus;
    } else if (doubleMatch('-', '-')) {
        match = Match::Double;
        kind = TokenKind::MinusMinus;
    } else {
        switch (charAt(pos_)) {
        case '(':
            match = Match::Single;
            kind = TokenKind::LeftParen;
            break;
        case ')':
            match = Match::Single;
            kind = TokenKind::RightParen;
            break;
        case '[':
            match = Match::Single;
            kind = TokenKind::LeftBracket;
            break;
        case ']':
            match = Match::Single;
            kind = TokenKind::RightBracket;
            break;
        case '{':
            match = Match::Single;
            kind = TokenKind::LeftBrace;
            break;
        case '}':
            match = Match::Single;
            kind = TokenKind::RightBrace;
            break;
        case ',':
            match = Match::Single;
            kind = TokenKind::Comma;
            break;
        case ';':
            match = Match::Single;
            kind = TokenKind::Semicolon;
            break;
        case ':':
            match = Match::Single;
            kind = TokenKind::Colon;
            break;
        case '?':
            match = Match::Single;
            kind = TokenKind::Question;
            break;
        case '.':
            match = Match::Single;
            kind = TokenKind::Dot;
            break;
        case '=':
            match = Match::Single;
            kind = TokenKind::Assign;
            break;
        case '<':
            match = Match::Single;
            kind = TokenKind::Less;
            break;
        case '>':
            match = Match::Single;
            kind = TokenKind::Greater;
            break;
        case '+':
            match = Match::Single;
            kind = TokenKind::Plus;
            break;
        case '-':
            match = Match::Single;
            kind = TokenKind::Minus;
            break;
        case '*':
            match = Match::Single;
            kind = TokenKind::Star;
            break;
        case '/':
            match = Match::Single;
            kind = TokenKind::Slash;
            break;
        case '%':
            match = Match::Single;
            kind = TokenKind::Percent;
            break;
        case '!':
            match = Match::Single;
            kind = TokenKind::Bang;
            break;
        default:
            return false;
        }
    }

    if (match == Match::None) {
        return false;
    }

    pos_ += match == Match::Double ? 2 : 1;

    if (kind == TokenKind::LeftParen || kind == TokenKind::LeftBracket) {
        ++depth_;
    } else if (kind == TokenKind::RightParen || kind == TokenKind::RightBracket) {
        if (depth_ > 0) {
            --depth_;
        }
    }

    emit(kind, start, pos_, wsBeforeAt(start), textOf(start, pos_));
    return true;
}

LexResult Lexer::tokenize() {
    tokens_.clear();
    diagnostic_.reset();
    pos_ = 0;
    depth_ = 0;
    seenNonNlAfterLastContinuation_ = false;

    while (!atEnd() && !diagnostic_) {
        const char value = charAt(pos_);

        if (isHorizontalWhitespace(value)) {
            ++pos_;
            continue;
        }

        if (value == '#') {
            scanComment();
            continue;
        }

        if (value == '\r' || value == '\n') {
            scanNewline();
            continue;
        }

        if (value == '"') {
            scanString();
            continue;
        }

        if (isDigit(value)) {
            scanNumber();
            continue;
        }

        if (value == '.' && isDigit(charAt(pos_ + 1))) {
            scanNumber();
            continue;
        }

        if (isIdentifierStart(value)) {
            scanIdentifier();
            continue;
        }

        if (value == '\\') {
            scanBackslash();
            continue;
        }

        if (scanOperator()) {
            continue;
        }

        scanIllegal();
    }

    if (!diagnostic_) {
        emit(TokenKind::EndOfFile, logical_.size(), logical_.size(), false, {});
    }

    const bool endsWithContinuation =
        lastContinuationOffset_ != LexResult::npos && !seenNonNlAfterLastContinuation_;

    return LexResult{std::move(tokens_), std::move(diagnostic_), endsWithContinuation,
                     lastContinuationOffset_, source_.size()};
}

} // namespace lush::core
