#include "lexer.h"

#include <cctype>
#include <cstring>

namespace sva {
namespace {

// Longest symbols first so that "|->" wins over "|" and "##" over "#".
const char* const kMultiCharSymbols[] = {"|->", "|=>", "===", "!==", "##", "==", "!=",
                                         "<=",  ">=",  "&&",  "||",  "<<", ">>"};

bool is_ident_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }

bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$';
}

bool is_base_char(char c) { return c != '\0' && std::strchr("bBoOdDhH", c) != nullptr; }

bool is_literal_digit(char c) {
    return std::isxdigit(static_cast<unsigned char>(c)) || (c != '\0' && std::strchr("xXzZ?_", c));
}

}  // namespace

std::vector<Token> tokenize(const std::string& src) {
    std::vector<Token> tokens;
    const size_t n = src.size();
    auto at = [&](size_t i) { return i < n ? src[i] : '\0'; };
    int line = 1;
    size_t i = 0;

    while (i < n) {
        const char c = src[i];
        if (c == '\n') {
            ++line;
            ++i;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
            continue;
        }
        if (c == '/' && at(i + 1) == '/') {
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        if (c == '/' && at(i + 1) == '*') {
            i += 2;
            while (i < n && !(src[i] == '*' && at(i + 1) == '/')) {
                if (src[i] == '\n') ++line;
                ++i;
            }
            i += 2;
            continue;
        }

        const size_t start = i;
        if (c == '"') {
            ++i;
            while (i < n && src[i] != '"') ++i;
            ++i;
            tokens.push_back({TokenKind::String, src.substr(start, i - start), line});
            continue;
        }
        if (is_ident_start(c) || (c == '$' && is_ident_start(at(i + 1)))) {
            ++i;
            while (i < n && is_ident_char(src[i])) ++i;
            tokens.push_back({TokenKind::Identifier, src.substr(start, i - start), line});
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || (c == '\'' && is_base_char(at(i + 1)))) {
            while (i < n && (std::isdigit(static_cast<unsigned char>(src[i])) || src[i] == '_')) ++i;
            if (at(i) == '\'') {
                ++i;
                if (at(i) == 's' || at(i) == 'S') ++i;
                if (!is_base_char(at(i))) {
                    throw ParseError("line " + std::to_string(line) + ": bad number literal");
                }
                ++i;
                while (i < n && is_literal_digit(src[i])) ++i;
            }
            tokens.push_back({TokenKind::Number, src.substr(start, i - start), line});
            continue;
        }

        std::string symbol(1, c);
        for (const char* multi : kMultiCharSymbols) {
            if (src.compare(i, std::strlen(multi), multi) == 0) {
                symbol = multi;
                break;
            }
        }
        i += symbol.size();
        tokens.push_back({TokenKind::Symbol, symbol, line});
    }
    tokens.push_back({TokenKind::End, "", line});
    return tokens;
}

uint64_t width_mask(int width) { return width >= 64 ? ~0ULL : ((1ULL << width) - 1); }

Value Value::known(uint64_t bits, int width) {
    return Value{bits & width_mask(width), width_mask(width), width};
}

Value parse_number(const std::string& text) {
    std::string digits;
    const size_t quote = text.find('\'');
    const std::string body = quote == std::string::npos ? text : text.substr(quote + 1);
    for (char ch : body) {
        if (ch != '_') digits += ch;
    }
    if (quote == std::string::npos) {
        return Value::known(std::stoull(digits), 32);
    }

    const int width = quote > 0 ? std::stoi(text.substr(0, quote)) : 32;
    if (width <= 0 || width > 64) throw ParseError("literal wider than 64 bits: " + text);
    size_t p = 0;
    if (digits[p] == 's' || digits[p] == 'S') ++p;
    const char base = static_cast<char>(std::tolower(static_cast<unsigned char>(digits[p++])));
    digits = digits.substr(p);
    if (digits.empty()) throw ParseError("empty literal: " + text);

    if (base == 'd') return Value::known(std::stoull(digits), width);

    const int bits_per_digit = base == 'b' ? 1 : base == 'o' ? 3 : 4;
    Value v{0, 0, width};
    for (char ch : digits) {
        v.bits <<= bits_per_digit;
        v.care <<= bits_per_digit;
        if (std::strchr("xXzZ?", ch)) continue;  // unknown digit: value 0, care 0
        const int digit = std::isdigit(static_cast<unsigned char>(ch))
                              ? ch - '0'
                              : std::tolower(static_cast<unsigned char>(ch)) - 'a' + 10;
        if (digit >= (1 << bits_per_digit)) throw ParseError("bad digit in literal: " + text);
        v.bits |= static_cast<uint64_t>(digit);
        v.care |= (1ULL << bits_per_digit) - 1;
    }
    // Digits above the literal's own length are known zeros.
    const uint64_t given = width_mask(static_cast<int>(digits.size()) * bits_per_digit);
    v.care |= ~given;
    v.bits &= width_mask(width);
    v.care &= width_mask(width);
    return v;
}

TokenStream::TokenStream(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token& TokenStream::peek(size_t ahead) const {
    const size_t idx = pos_ + ahead;
    return idx < tokens_.size() ? tokens_[idx] : tokens_.back();
}

const Token& TokenStream::next() {
    const Token& tok = peek();
    if (pos_ < tokens_.size() - 1) ++pos_;
    return tok;
}

bool TokenStream::accept(const std::string& text) {
    if (peek().kind == TokenKind::String || peek().text != text) return false;
    next();
    return true;
}

void TokenStream::expect(const std::string& text) {
    if (!accept(text)) fail("expected '" + text + "' but found '" + peek().text + "'");
}

std::string TokenStream::expect_identifier() {
    if (peek().kind != TokenKind::Identifier) fail("expected identifier, found '" + peek().text + "'");
    return next().text;
}

int TokenStream::expect_int() {
    if (peek().kind != TokenKind::Number) fail("expected number, found '" + peek().text + "'");
    return static_cast<int>(parse_number(next().text).bits);
}

void TokenStream::fail(const std::string& message) const {
    throw ParseError("line " + std::to_string(peek().line) + ": " + message);
}

}  // namespace sva
