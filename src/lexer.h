// Tokenizer shared by the Verilog FSM parser and the SVA parser.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace sva {

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class TokenKind { Identifier, Number, String, Symbol, End };

struct Token {
    TokenKind kind;
    std::string text;
    int line;
};

// Splits Verilog/SVA source into tokens. Comments and whitespace are dropped.
// System functions such as $rose come back as identifiers starting with '$';
// sized literals such as 2'b0x come back as a single Number token.
std::vector<Token> tokenize(const std::string& source);

// A four-state literal reduced to what we need: bit values plus a "care" mask
// whose 0 bits mark x/z/? positions (used by casex/casez matching).
struct Value {
    uint64_t bits = 0;
    uint64_t care = ~0ULL;
    int width = 32;

    static Value known(uint64_t bits, int width);
};

uint64_t width_mask(int width);

// Parses 5, 'd5, 2'b0x, 8'hF_F and similar Verilog number literals.
Value parse_number(const std::string& text);

// Cursor over a token vector with the usual peek/accept/expect helpers.
class TokenStream {
public:
    explicit TokenStream(std::vector<Token> tokens);

    const Token& peek(size_t ahead = 0) const;
    const Token& next();
    bool at_end() const { return peek().kind == TokenKind::End; }

    // Consumes the next token if its text equals `text`.
    bool accept(const std::string& text);
    void expect(const std::string& text);
    std::string expect_identifier();
    int expect_int();

    [[noreturn]] void fail(const std::string& message) const;

private:
    std::vector<Token> tokens_;
    size_t pos_ = 0;
};

}  // namespace sva
