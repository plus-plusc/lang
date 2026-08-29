#include "preproc.hxx"

namespace lex {

namespace {

// Internal-only: thrown anywhere deep in the recursive expansion/inclusion
// logic and caught once at the top of Preprocessor::run(), so error paths
// don't have to be threaded through every generator and helper by hand.
struct PPException : std::exception {
    PreprocessorError err;
    explicit PPException(PreprocessorError e) : err(std::move(e)) {}
    const char* what() const noexcept override { return err.message.c_str(); }
};

} // namespace

bool Preprocessor::active() const {
    for (auto& f : condStack_) { if (!f.branchActive) { return false; } }
    return true;
}

bool Preprocessor::activeExcludingTop() const {
    for (size_t i = 0; i + 1 < condStack_.size(); ++i) {
        if (!condStack_[i].branchActive) { return false; }
    }
    return true;
}

void Preprocessor::handleDefine(const std::vector<Token>& args, Path currentFile) {
    if (args.empty()) {
        throw PPException(PreprocessorError{"#define with no macro name", currentFile, 0, 0});
    }
    const Token& nameTok = args[0];
    if (nameTok.name.empty() ||
        !(std::isalpha(static_cast<unsigned char>(nameTok.name.front())) || nameTok.name.front() == '_')) {
        throw PPException(PreprocessorError{
            "invalid macro name '" + nameTok.name + "'", currentFile, nameTok.line, nameTok.column
        });
    }

    MacroDef def;
    size_t bodyStart = 1;

    // Function-like iff '(' immediately follows the name with no gap --
    // this is why token *start* position matters (see lex.cxx).
    if (args.size() > 1 && args[1].name == "(" &&
        args[1].line == nameTok.line &&
        args[1].column == nameTok.column + nameTok.name.size()) {
        def.isFunctionLike = true;
        size_t i = 2;
        while (i < args.size() && args[i].name != ")") {
            if (args[i].name != ",") { def.params.push_back(args[i].name); }
            ++i;
        }
        if (i >= args.size()) {
            throw PPException(PreprocessorError{
                "unterminated parameter list in #define " + nameTok.name, currentFile, nameTok.line, nameTok.column
            });
        }
        bodyStart = i + 1; // past ')'
    }

    def.body.assign(args.begin() + static_cast<std::ptrdiff_t>(bodyStart), args.end());
    macros_[nameTok.name] = std::move(def);
}

std::generator<Token> Preprocessor::handleInclude(const std::vector<Token>& args, Path currentFile) {
    if (args.size() != 1) {
        throw PPException(PreprocessorError{"#include expects exactly one path argument", currentFile, 0, 0});
    }
    const std::string& raw = args[0].name;
    if (raw.size() < 2 ||
        !((raw.front() == '"' && raw.back() == '"') || (raw.front() == '<' && raw.back() == '>'))) {
        throw PPException(PreprocessorError{
            "malformed #include argument '" + raw + "'", currentFile, args[0].line, args[0].column
        });
    }
    std::string name = raw.substr(1, raw.size() - 2);

    // Simple resolution for now: relative to the including file's
    // directory, same for both "..." and <...> forms. A real search-path
    // list (-I style) is a later concern.
    Path resolved = currentFile.parent_path() / name;
    if (!fs::exists(resolved) || !fs::is_regular_file(resolved)) {
        throw PPException(PreprocessorError{
            "included file not found: '" + name + "'", currentFile, args[0].line, args[0].column
        });
    }

    Path canon = fs::weakly_canonical(resolved);
    if (std::ranges::find(includeStack_, canon) != includeStack_.end()) {
        throw PPException(PreprocessorError{
            "circular #include of '" + name + "'", currentFile, args[0].line, args[0].column
        });
    }

    auto text = readFile(resolved);
    if (!text) {
        throw PPException(PreprocessorError{
            "cannot open included file '" + name + "': " + text.error().what(),
            currentFile, args[0].line, args[0].column
        });
    }

    includeStack_.push_back(canon);
    for (auto&& tok : process(processLex(std::move(*text)), resolved)) {
        co_yield tok;
    }
    includeStack_.pop_back();
}

std::pair<std::vector<std::vector<Token>>, size_t> Preprocessor::gatherArgsFromVector(
    const std::vector<Token>& in, size_t startIndex, const Token& callTok, Path currentFile
) {
    std::vector<std::vector<Token>> callArgs;
    std::vector<Token> current;
    int depth = 1;
    size_t j = startIndex;
    for (; j < in.size() && depth > 0; ++j) {
        const Token& tt = in[j];
        if (tt.name == "(") { ++depth; current.push_back(tt); continue; }
        if (tt.name == ")") {
            --depth;
            if (depth == 0) {
                if (!current.empty() || !callArgs.empty()) { callArgs.push_back(std::move(current)); }
                ++j;
                break;
            }
            current.push_back(tt);
            continue;
        }
        if (tt.name == "," && depth == 1) {
            callArgs.push_back(std::move(current));
            current.clear();
            continue;
        }
        current.push_back(tt);
    }
    if (depth != 0) {
        throw PPException(PreprocessorError{
            "unterminated macro call to '" + callTok.name + "'", currentFile, callTok.line, callTok.column
        });
    }
    return { std::move(callArgs), j };
}

std::vector<Token> Preprocessor::substituteAndRescan(
    const MacroDef& def, const std::string& name,
    std::vector<std::vector<Token>> callArgs, Path currentFile
) {
    if (callArgs.size() != def.params.size()) {
        throw PPException(PreprocessorError{
            "macro '" + name + "' expects " + std::to_string(def.params.size()) +
                " argument(s), got " + std::to_string(callArgs.size()),
            currentFile, 0, 0
        });
    }

    // Arguments are macro-expanded before substitution (standard behavior).
    std::vector<std::vector<Token>> expandedArgs;
    expandedArgs.reserve(callArgs.size());
    for (auto& a : callArgs) { expandedArgs.push_back(expandTokenList(a, currentFile)); }

    std::vector<Token> substituted;
    for (auto& bt : def.body) {
        if (bt.lex == Token::Identifier) {
            auto pit = std::ranges::find(def.params, bt.name);
            if (pit != def.params.end()) {
                size_t idx = static_cast<size_t>(std::distance(def.params.begin(), pit));
                for (auto& s : expandedArgs[idx]) { substituted.push_back(s); }
                continue;
            }
        }
        substituted.push_back(bt);
    }

    expandingStack_.push_back(name);
    auto rescanned = expandTokenList(substituted, currentFile);
    expandingStack_.pop_back();
    return rescanned;
}

std::vector<Token> Preprocessor::expandTokenList(const std::vector<Token>& in, Path currentFile) {
    std::vector<Token> out;
    size_t i = 0;
    while (i < in.size()) {
        const Token& t = in[i];
        auto found = macros_.find(t.name);
        bool guarded = std::ranges::find(expandingStack_, t.name) != expandingStack_.end();
        if (found == macros_.end() || guarded) {
            out.push_back(t);
            ++i;
            continue;
        }
        const MacroDef& def = found->second;
        if (!def.isFunctionLike) {
            expandingStack_.push_back(t.name);
            auto expanded = expandTokenList(def.body, currentFile);
            expandingStack_.pop_back();
            out.insert(out.end(), expanded.begin(), expanded.end());
            ++i;
            continue;
        }
        // Function-like, but only actually invoked if '(' immediately
        // follows within this same token list -- otherwise it's just a
        // bare mention and is left alone.
        if (i + 1 >= in.size() || in[i + 1].name != "(") {
            out.push_back(t);
            ++i;
            continue;
        }
        auto [callArgs, nextIndex] = gatherArgsFromVector(in, i + 2, t, currentFile);
        auto rescanned = substituteAndRescan(def, t.name, std::move(callArgs), currentFile);
        out.insert(out.end(), rescanned.begin(), rescanned.end());
        i = nextIndex;
    }
    return out;
}

bool Preprocessor::evalCondition(const std::vector<Token>& condTokens, Path currentFile) {
    if (condTokens.empty()) {
        throw PPException(PreprocessorError{"#if/#elif with empty condition", currentFile, 0, 0});
    }

    size_t pos = 0;
    auto peek = [&]() -> const Token* { return pos < condTokens.size() ? &condTokens[pos] : nullptr; };
    auto advance = [&]() -> const Token* { return pos < condTokens.size() ? &condTokens[pos++] : nullptr; };

    std::function<bool()> parseOr, parseXor, parseAnd, parseNot, parsePrimary;

    parsePrimary = [&]() -> bool {
        const Token* t = peek();
        if (!t) {
            throw PPException(PreprocessorError{"unexpected end of condition", currentFile, 0, 0});
        }
        if (t->name == "(") {
            advance();
            bool v = parseOr();
            const Token* close = advance();
            if (!close || close->name != ")") {
                throw PPException(PreprocessorError{"expected ')' in condition", currentFile, t->line, t->column});
            }
            return v;
        }
        if (t->name == "defined") {
            // Note: 'defined's operand is deliberately NOT macro-expanded
            // here -- we want to know if the name itself is #define'd, not
            // what it expands to.
            advance();
            std::string name;
            if (peek() && peek()->name == "(") {
                advance();
                const Token* id = advance();
                if (!id) {
                    throw PPException(PreprocessorError{"expected identifier after 'defined('", currentFile, t->line, t->column});
                }
                name = id->name;
                const Token* close = advance();
                if (!close || close->name != ")") {
                    throw PPException(PreprocessorError{"expected ')' after 'defined(...'", currentFile, t->line, t->column});
                }
            } else {
                const Token* id = advance();
                if (!id) {
                    throw PPException(PreprocessorError{"expected identifier after 'defined'", currentFile, t->line, t->column});
                }
                name = id->name;
            }
            return macros_.contains(name);
        }
        if (!t->name.empty() && std::isdigit(static_cast<unsigned char>(t->name.front()))) {
            advance();
            long long v = 0;
            try { v = std::stoll(t->name); } catch (...) { v = 0; }
            return v != 0;
        }
        // Bare identifier: expand it if it's a macro, otherwise it's
        // undefined and counts as 0/false (standard preprocessor rule).
        advance();
        if (auto it = macros_.find(t->name);
            it != macros_.end() && std::ranges::find(expandingStack_, t->name) == expandingStack_.end()) {
            expandingStack_.push_back(t->name);
            auto expanded = expandTokenList(std::vector<Token>{*t}, currentFile);
            expandingStack_.pop_back();
            if (expanded.empty()) { return false; }
            if (expanded.size() == 1 && !expanded[0].name.empty() &&
                std::isdigit(static_cast<unsigned char>(expanded[0].name.front()))) {
                long long v = 0;
                try { v = std::stoll(expanded[0].name); } catch (...) { v = 0; }
                return v != 0;
            }
            return true; // expands to something non-numeric/non-empty -> truthy
        }
        return false;
    };
    parseNot = [&]() -> bool {
        if (peek() && peek()->name == "not") { advance(); return !parseNot(); }
        return parsePrimary();
    };
    parseAnd = [&]() -> bool {
        bool v = parseNot();
        while (peek() && peek()->name == "and") { advance(); bool rhs = parseNot(); v = v && rhs; }
        return v;
    };
    parseXor = [&]() -> bool {
        bool v = parseAnd();
        while (peek() && peek()->name == "xor") { advance(); bool rhs = parseAnd(); v = (v != rhs); }
        return v;
    };
    parseOr = [&]() -> bool {
        bool v = parseXor();
        while (peek() && peek()->name == "or") { advance(); bool rhs = parseXor(); v = v || rhs; }
        return v;
    };

    return parseOr();
}

std::generator<Token> Preprocessor::process(std::generator<Token> tokens, Path currentFile) {
    auto it = tokens.begin();
    auto end = tokens.end();

    while (it != end) {
        Token tok = *it;

        if (tok.lex == Token::Preprocessor && tok.name == "#") {
            ++it;
            if (it == end) {
                throw PPException(PreprocessorError{"stray '#' at end of file", currentFile, tok.line, tok.column});
            }
            Token directiveTok = *it;
            const std::string directive = directiveTok.name;
            ++it;

            std::vector<Token> args;
            while (it != end) {
                Token t = *it;
                if (t.lex == Token::Preprocessor && t.name == "\n") { ++it; break; }
                args.push_back(t);
                ++it;
            }

            bool isConditional =
                directive == "if" || directive == "elif" || directive == "else" || directive == "endif";
            if (!active() && !isConditional) {
                continue; // whole directive ignored while inside an inactive branch
            }

            if (directive == "define") {
                handleDefine(args, currentFile);
            } else if (directive == "include") {
                co_yield std::ranges::elements_of(handleInclude(args, currentFile));
            } else if (directive == "if") {
                bool parentActive = active();
                bool result = parentActive && evalCondition(args, currentFile);
                condStack_.push_back(CondFrame{ .branchActive = result, .anyTaken = result });
            } else if (directive == "elif") {
                if (condStack_.empty()) {
                    throw PPException(PreprocessorError{"#elif without matching #if", currentFile, directiveTok.line, directiveTok.column});
                }
                bool parentActive = activeExcludingTop();
                auto& f = condStack_.back();
                if (!parentActive || f.anyTaken) {
                    f.branchActive = false;
                } else {
                    bool result = evalCondition(args, currentFile);
                    f.branchActive = result;
                    if (result) { f.anyTaken = true; }
                }
            } else if (directive == "else") {
                if (condStack_.empty()) {
                    throw PPException(PreprocessorError{"#else without matching #if", currentFile, directiveTok.line, directiveTok.column});
                }
                bool parentActive = activeExcludingTop();
                auto& f = condStack_.back();
                if (!parentActive || f.anyTaken) {
                    f.branchActive = false;
                } else {
                    f.branchActive = true;
                    f.anyTaken = true;
                }
            } else if (directive == "endif") {
                if (condStack_.empty()) {
                    throw PPException(PreprocessorError{"#endif without matching #if", currentFile, directiveTok.line, directiveTok.column});
                }
                condStack_.pop_back();
            } else if (directive == "pragma") {
                // Out of scope for now -- ignored.
            } else if (directive == "import" || directive == "module" || directive == "export") {
                // Modules explicitly excluded from this pass.
            } else {
                throw PPException(PreprocessorError{
                    "unknown preprocessor directive '" + directive + "'", currentFile, directiveTok.line, directiveTok.column
                });
            }
            continue;
        }

        if (!active()) { ++it; continue; }

        auto found = macros_.find(tok.name);
        bool guarded = found != macros_.end() &&
            std::ranges::find(expandingStack_, tok.name) != expandingStack_.end();
        if (found != macros_.end() && !guarded) {
            const MacroDef& def = found->second;
            ++it;

            if (!def.isFunctionLike) {
                expandingStack_.push_back(tok.name);
                auto expanded = expandTokenList(def.body, currentFile);
                expandingStack_.pop_back();
                for (auto& e : expanded) { co_yield e; }
                continue;
            }

            if (it == end || (*it).name != "(") {
                // Bare mention of a function-like macro (no call) -- emit as-is.
                co_yield tok;
                continue;
            }
            ++it; // consume '('
            std::vector<std::vector<Token>> callArgs;
            std::vector<Token> current;
            int depth = 1;
            while (it != end && depth > 0) {
                Token t = *it;
                if (t.name == "(") { ++depth; current.push_back(t); ++it; continue; }
                if (t.name == ")") {
                    --depth;
                    ++it;
                    if (depth == 0) {
                        if (!current.empty() || !callArgs.empty()) { callArgs.push_back(std::move(current)); }
                        break;
                    }
                    current.push_back(t);
                    continue;
                }
                if (t.name == "," && depth == 1) {
                    callArgs.push_back(std::move(current));
                    current.clear();
                    ++it;
                    continue;
                }
                current.push_back(t);
                ++it;
            }
            if (depth != 0) {
                throw PPException(PreprocessorError{
                    "unterminated macro call to '" + tok.name + "'", currentFile, tok.line, tok.column
                });
            }
            auto expanded = substituteAndRescan(def, tok.name, std::move(callArgs), currentFile);
            for (auto& e : expanded) { co_yield e; }
            continue;
        }

        co_yield tok;
        ++it;
    }
}

std::expected<std::vector<Token>, PreprocessorError> Preprocessor::run(Path filepath) {
    try {
        auto text = readFile(filepath);
        if (!text) {
            return std::unexpected(PreprocessorError{
                std::string{"could not read file: "} + std::string{text.error().what()}, filepath, 0, 0
            });
        }
        includeStack_.push_back(fs::weakly_canonical(filepath));

        std::vector<Token> result;
        for (auto&& tok : process(processLex(std::move(*text)), filepath)) {
            result.push_back(std::move(tok));
        }
        if (!condStack_.empty()) {
            return std::unexpected(PreprocessorError{"unterminated #if (missing #endif)", filepath, 0, 0});
        }
        return result;
    } catch (const PPException& e) {
        return std::unexpected(e.err);
    }
}

} // namespace lex