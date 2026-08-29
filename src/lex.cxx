#include "lex.hxx"

namespace lex {

// General-purpose keywords, drawn from Keywords-and-their-semantics.md
// (Linkage / Constant-evaluation / Threading / Default-keywords sections).
// Deliberately EXCLUDES:
//   - preprocessorKeywords below: per the README, ++C has no general
//     if/else at all -- if/elif/else/endif only mean anything directly
//     after '#', so they're recognized inside processPreprocessor instead.
//   - wordOperators below: and/or/not/xor read like keywords but behave
//     like operators, so tokenize() reports them as Token::Operator.
constexpr std::array<std::string_view, 34> keywords{
    "copy", "move", "match", "loop", "cast", "asm",
    "static", "inline", "yield", "await", "return", "auto", "type", "goto",
    "void", "class", "mem", "error", "warning", "ptr", "namespace", "default",
    "reflect_on_my_stupid_actions", "template", "check",
    "constexpr", "consteval", "static_assert", "embed",
    "thread_local", "thread", "async",
    "extern", "export"
};

// Directive names -- only meaningful directly after '#'. Handled by
// processPreprocessor, not by the general isKeyword() below.
constexpr std::array<std::string_view, 9> preprocessorKeywords{
    "include", "if", "elif", "else", "endif", "pragma", "define", "import", "module"
};

// Word-spelled operators: same idea as +/-/&&/||, just spelled out.
// Classified as Token::Operator rather than Token::Keyword.
constexpr std::array<std::string_view, 4> wordOperators{
    "and", "or", "not", "xor"
};

bool isKeyword(const std::string& str) {
    return std::ranges::find(keywords, std::string_view{str}) != keywords.end();
}
bool isPreprocessorKeyword(const std::string& str) {
    return std::ranges::find(preprocessorKeywords, std::string_view{str}) != preprocessorKeywords.end();
}
bool isOperator(const std::string& str) {
    // TODO: extend with symbolic operators (+, -, *, ==, <, cast<...>, etc.)
    // once ++C's symbolic-operator set is pinned down -- deliberately left
    // as a stub per your call to decide that later.
    return std::ranges::find(wordOperators, std::string_view{str}) != wordOperators.end();
}

[[__gnu__::__always_inline__]] inline bool isSeparator(char c) {
    return c == ' ' ||
        c == '\n' || 
        c == '\t' || 
        c == ';' || 
        c == '(' || 
        c == ')' || 
        c == '{' ||
        c == '}';
}

// TODO

Token tokenize(const std::string& str, size_t line, size_t column) {
    return Token {
        .lex = [s = std::as_const(str)]() -> Token::Lex {
            if (auto c = s.front();
                (c == s.back() && (c == '\'' || c == '"')) ||
                (std::isdigit(c))) {
                return Token::Literal;
            }
            if (isKeyword(s)) { return Token::Keyword; }
            if (isSeparator(s.front())) { return Token::Separator; }
            if (isOperator(s)) { return Token::Operator; }
            return Token::Identifier;
        }(),
        .name = str,
        .line = line,
        .column = column
    };
}

// Takes over character-by-character iteration the moment a '#' is seen at the
// start of a token. Owns `it`/`posX`/`posY` for the duration of the directive
// line (advancing them itself, including line-continuation via a trailing
// backslash) and hands control back to processLex once the directive ends.
std::generator<Token> processPreprocessor(
    std::string::const_iterator& it,
    std::string::const_iterator end,
    size_t& posX,
    size_t& posY
) {
    co_yield Token{ .lex = Token::Preprocessor, .name = "#", .line = posY, .column = posX };
    ++it; ++posX;

    std::string currStr{};
    bool firstWord = true;
    size_t tokStartX = posX, tokStartY = posY;

    // Only the very first word on the directive line (the directive name
    // itself, e.g. "include"/"if"/"define") gets tagged Token::Preprocessor;
    // everything after it (macro names, args, included paths, ...) goes
    // through the normal tokenize() classification.
    auto flush = [&]() -> Token {
        Token tok = (firstWord && isPreprocessorKeyword(currStr))
            ? Token{ .lex = Token::Preprocessor, .name = currStr, .line = tokStartY, .column = tokStartX }
            : tokenize(currStr, tokStartY, tokStartX);
        firstWord = false;
        currStr.clear();
        return tok;
    };

    while (it != end) {
        char c = *it;

        if (c == '\\' && std::next(it) != end && *std::next(it) == '\n') {
            // Line continuation: swallow the backslash+newline and keep going
            // as if the directive text were contiguous.
            ++it; ++posX;
            ++it; posX = 1; ++posY;
            continue;
        }

        if (c == '\n') {
            if (!currStr.empty()) { co_yield flush(); }
            // End-of-directive marker: lets a downstream preprocessor stage
            // find the boundary of this directive's tokens regardless of
            // line-continuation quirks (a real '\n' never becomes a token
            // otherwise, so this is unambiguous).
            co_yield Token{ .lex = Token::Preprocessor, .name = "\n", .line = posY, .column = posX };
            // Leave `it` on the newline; processLex's outer loop consumes it
            // and advances the line counter, so we don't double count.
            co_return;
        }

        if (isSeparator(c) && !currStr.empty()) {
            co_yield flush();
        } else if (!isSeparator(c)) {
            if (currStr.empty()) { tokStartX = posX; tokStartY = posY; }
            currStr += c;
        }
        ++posX; ++it;
    }
    if (!currStr.empty()) { co_yield flush(); }
    co_yield Token{ .lex = Token::Preprocessor, .name = "\n", .line = posY, .column = posX };
}

std::generator<Token> processLex(std::string text) {
    std::string currStr{};
    size_t posX{1};
    size_t posY{1};
    size_t tokStartX{1};
    size_t tokStartY{1};
    auto it = text.cbegin();
    while (it != text.cend()) {
        char c = *it;

        if (c == '\n') {
            posX = 1; ++posY; ++it;
            continue;
        }

        if (c == '#' && currStr.empty()) {
            // Hand control over to the preprocessor handler; it advances
            // `it`/`posX`/`posY` itself and yields whatever tokens it produces.
            co_yield std::ranges::elements_of(processPreprocessor(it, text.cend(), posX, posY));
            continue;
        }

        if (isSeparator(c) && !currStr.empty()) {
            co_yield tokenize(currStr, tokStartY, tokStartX);
            currStr.clear();
        } else if (!isSeparator(c)) {
            if (currStr.empty()) { tokStartX = posX; tokStartY = posY; }
            currStr += c;
        }
        ++posX; ++it;
    }
    if (!currStr.empty()) { co_yield tokenize(currStr, tokStartY, tokStartX); }
    co_return;
}

std::expected<std::string, fs::filesystem_error> readFile(Path filepath) {
    if (!fs::exists(filepath) || !fs::is_regular_file(filepath)) {
        return std::unexpected(fs::filesystem_error(
            "File does not exist or is not a regular file",
            filepath,
            std::error_code{}
        ));
    }
    std::ifstream file{filepath};
    if (!file.is_open()) {
        return std::unexpected(fs::filesystem_error(
            "Could not open file",
            filepath,
            std::error_code{}
        ));
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::expected<std::vector<Token>, fs::filesystem_error> lex(Path filepath) {
    auto text = readFile(filepath);
    if (!text) { return std::unexpected(text.error()); }

    std::vector<Token> res;
    for (auto&& token : processLex(std::move(*text))) {
        res.push_back(std::move(token));
    }
    return res;
}

} // namespace lex