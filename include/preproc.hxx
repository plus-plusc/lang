#pragma once

#include "lex.hxx"

namespace lex {

// A single macro definition introduced by #define. Non-variadic only.
struct MacroDef {
    bool isFunctionLike = false;
    std::vector<std::string> params;   // empty for object-like macros
    std::vector<Token> body;           // replacement token list
};

// Anything that can go wrong while preprocessing: a missing/unreadable
// #include target, a circular #include, an unbalanced #if/#elif/#else/
// #endif, a malformed #define, a macro called with the wrong number of
// arguments, etc.
struct PreprocessorError {
    std::string message;
    Path file;
    size_t line = 0;
    size_t column = 0;
};

// Runs the full lex + preprocess pipeline starting from `filepath`:
// tokenizes, expands object-like and function-like macros, splices in
// #include targets, and strips/resolves #if-#elif-#else-#endif branches.
// Produces the token stream a parser should actually see.
//
// Explicitly out of scope for now (per current project scope): modules
// (import/module/export), #pragma, variadic macros, and any
// stringize/token-paste operators.
class Preprocessor {
public:
    std::expected<std::vector<Token>, PreprocessorError> run(Path filepath);

private:
    std::unordered_map<std::string, MacroDef> macros_;

    // Macro names currently mid-expansion, so self-referential macros
    // (directly or through a chain) stop instead of recursing forever.
    std::vector<std::string> expandingStack_;

    // Canonicalized paths of files currently being included, to catch
    // circular #include chains.
    std::vector<Path> includeStack_;

    // One frame per currently-open #if..#endif chain.
    struct CondFrame {
        bool branchActive; // is the CURRENT branch (if/elif/else) live right now
        bool anyTaken;      // has any branch in this chain already been taken
    };
    std::vector<CondFrame> condStack_;

    // True iff every enclosing conditional frame is on a live branch, i.e.
    // tokens encountered right now should actually be emitted.
    bool active() const;
    // Same, but ignoring the innermost frame -- used when deciding whether
    // an #elif/#else itself is reachable at all.
    bool activeExcludingTop() const;

    // Consumes a raw token stream (as produced by processLex over one
    // file's text) and yields the preprocessed stream: directives are
    // interpreted and consumed, macro invocations are expanded inline, and
    // #include recurses into process() for the included file.
    std::generator<Token> process(std::generator<Token> tokens, Path currentFile);

    void handleDefine(const std::vector<Token>& args, Path currentFile);
    std::generator<Token> handleInclude(const std::vector<Token>& args, Path currentFile);
    bool evalCondition(const std::vector<Token>& condTokens, Path currentFile);

    // Gathers a parenthesized, comma-separated argument list starting at
    // in[startIndex] (just past the opening '('). Returns the arguments
    // and the index just past the matching ')'.
    std::pair<std::vector<std::vector<Token>>, size_t> gatherArgsFromVector(
        const std::vector<Token>& in, size_t startIndex, const Token& callTok, Path currentFile
    );

    // Substitutes `def`'s parameters with `callArgs` (each pre-expanded),
    // then rescans the result so any macro invocations produced by the
    // substitution are themselves expanded.
    std::vector<Token> substituteAndRescan(
        const MacroDef& def, const std::string& name,
        std::vector<std::vector<Token>> callArgs, Path currentFile
    );

    // Rescans a flat token list (a macro body, a macro argument, or a
    // #if condition), expanding any macro invocations found within it.
    std::vector<Token> expandTokenList(const std::vector<Token>& in, Path currentFile);
};

} // namespace lex