#pragma once

#include "core.hxx"

namespace lex {

struct Token {
    enum Lex {
        Identifier = 0,
        Keyword = 1,
        Separator = 2,
        Operator = 3,
        Literal = 4,
        Preprocessor = 5,
        None = -1
    } lex;
    std::string name;
    size_t line;
    size_t column;
};

Token tokenize(const std::string& str, size_t line, size_t column);

[[__gnu__::__always_inline__]] inline bool isSeparator(char c);

// Reads a file's full contents into memory. Shared by lex() and by
// Preprocessor for resolving #include targets.
std::expected<std::string, fs::filesystem_error> readFile(Path filepath);

// Takes ownership of its input (rather than a reference) so the resulting
// generator is self-contained and safe to store, pass around, or recurse
// into -- needed once #include starts nesting processLex calls.
void processLex(std::string text, std::vector<Token>& tokens);

std::expected<std::vector<Token>, fs::filesystem_error> lex(Path filepath);
}   // namespace lex