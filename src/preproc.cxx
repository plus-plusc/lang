#include "preproc.hxx"

namespace lex {

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

std::expected<void, PreprocessorError> Preprocessor::handleDefine(const std::vector<Token>& args, Path currentFile) {
    if (args.empty()) {
        return std::unexpected(PreprocessorError{"#define with no macro name", currentFile, 0, 0});
    }
    const Token& nameTok = args[0];
    if (nameTok.name.empty() ||
        !(std::isalpha(static_cast<unsigned char>(nameTok.name.front())) || nameTok.name.front() == '_')) {
        return std::unexpected(PreprocessorError{
            "invalid macro name '" + nameTok.name + "'", currentFile, nameTok.line, nameTok.column
        });
    }

    MacroDef def;
    size_t bodyStart = 1;

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
            return std::unexpected(PreprocessorError{
                "unterminated parameter list in #define " + nameTok.name, currentFile, nameTok.line, nameTok.column
            });
        }
        bodyStart = i + 1;
    }

    def.body.assign(args.begin() + static_cast<std::ptrdiff_t>(bodyStart), args.end());
    macros_[nameTok.name] = std::move(def);
    return {};
}

std::expected<std::vector<Token>, PreprocessorError> Preprocessor::handleInclude(const std::vector<Token>& args, Path currentFile) {
    if (args.size() != 1) {
        return std::unexpected(PreprocessorError{"#include expects exactly one path argument", currentFile, 0, 0});
    }
    const std::string& raw = args[0].name;
    if (raw.size() < 2 ||
        !((raw.front() == '"' && raw.back() == '"') || (raw.front() == '<' && raw.back() == '>'))) {
        return std::unexpected(PreprocessorError{
            "malformed #include argument '" + raw + "'", currentFile, args[0].line, args[0].column
        });
    }
    std::string name = raw.substr(1, raw.size() - 2);

    Path resolved = currentFile.parent_path() / name;
    if (!fs::exists(resolved) || !fs::is_regular_file(resolved)) {
        return std::unexpected(PreprocessorError{
            "included file not found: '" + name + "'", currentFile, args[0].line, args[0].column
        });
    }

    Path canon = fs::weakly_canonical(resolved);
    if (std::ranges::find(includeStack_, canon) != includeStack_.end()) {
        return std::unexpected(PreprocessorError{
            "circular #include of '" + name + "'", currentFile, args[0].line, args[0].column
        });
    }

    auto text = readFile(resolved);
    if (!text) {
        return std::unexpected(PreprocessorError{
            "cannot open included file '" + name + "': " + text.error().what(),
            currentFile, args[0].line, args[0].column
        });
    }

    includeStack_.push_back(canon);
    std::vector<Token> includedTokens;
    processLex(std::move(*text), includedTokens);
    auto result = process(std::move(includedTokens), resolved);
    if (!result) {
        includeStack_.pop_back();
        return std::unexpected(result.error());
    }
    includeStack_.pop_back();
    return result;
}

std::pair<std::vector<std::vector<Token>>, size_t> Preprocessor::gatherArgsFromVector(
    const std::vector<Token>& in, size_t startIndex, [[maybe_unused]] const Token& callTok, [[maybe_unused]] Path currentFile
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
    return { std::move(callArgs), j };
}

std::expected<std::vector<Token>, PreprocessorError> Preprocessor::substituteAndRescan(
    const MacroDef& def, const std::string& name,
    std::vector<std::vector<Token>> callArgs, Path currentFile
) {
    if (callArgs.size() != def.params.size()) {
        return std::unexpected(PreprocessorError{
            "macro '" + name + "' expects " + std::to_string(def.params.size()) +
                " argument(s), got " + std::to_string(callArgs.size()),
            currentFile, 0, 0
        });
    }

    std::vector<std::vector<Token>> expandedArgs;
    expandedArgs.reserve(callArgs.size());
    for (auto& a : callArgs) {
        auto exp = expandTokenList(a, currentFile);
        if (!exp) return std::unexpected(exp.error());
        expandedArgs.push_back(std::move(*exp));
    }

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

std::expected<std::vector<Token>, PreprocessorError> Preprocessor::expandTokenList(const std::vector<Token>& in, Path currentFile) {
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
            if (!expanded) return std::unexpected(expanded.error());
            out.insert(out.end(), expanded->begin(), expanded->end());
            ++i;
            continue;
        }
        if (i + 1 >= in.size() || in[i + 1].name != "(") {
            out.push_back(t);
            ++i;
            continue;
        }
        auto [callArgs, nextIndex] = gatherArgsFromVector(in, i + 2, t, currentFile);
        auto rescanned = substituteAndRescan(def, t.name, std::move(callArgs), currentFile);
        if (!rescanned) return std::unexpected(rescanned.error());
        out.insert(out.end(), rescanned->begin(), rescanned->end());
        i = nextIndex;
    }
    return out;
}

bool Preprocessor::evalCondition(const std::vector<Token>& condTokens, Path currentFile) {
    if (condTokens.empty()) {
        return false;
    }

    size_t pos = 0;
    auto peek = [&]() -> const Token* { return pos < condTokens.size() ? &condTokens[pos] : nullptr; };
    auto advance = [&]() -> const Token* { return pos < condTokens.size() ? &condTokens[pos++] : nullptr; };

    std::function<bool()> parseOr, parseXor, parseAnd, parseNot, parsePrimary;

    parsePrimary = [&]() -> bool {
        const Token* t = peek();
        if (!t) return false;
        if (t->name == "(") {
            advance();
            bool v = parseOr();
            const Token* close = advance();
            if (!close || close->name != ")") return false;
            return v;
        }
        if (t->name == "defined") {
            advance();
            std::string name;
            if (peek() && peek()->name == "(") {
                advance();
                const Token* id = advance();
                if (!id) return false;
                name = id->name;
                const Token* close = advance();
                if (!close || close->name != ")") return false;
            } else {
                const Token* id = advance();
                if (!id) return false;
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
        advance();
        if (auto it = macros_.find(t->name);
            it != macros_.end() && std::ranges::find(expandingStack_, t->name) == expandingStack_.end()) {
            expandingStack_.push_back(t->name);
            auto expanded = expandTokenList(std::vector<Token>{*t}, currentFile);
            expandingStack_.pop_back();
            if (!expanded || expanded->empty()) return false;
            if (expanded->size() == 1 && !(*expanded)[0].name.empty() &&
                std::isdigit(static_cast<unsigned char>((*expanded)[0].name.front()))) {
                long long v = 0;
                try { v = std::stoll((*expanded)[0].name); } catch (...) { v = 0; }
                return v != 0;
            }
            return true;
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

std::expected<std::vector<Token>, PreprocessorError> Preprocessor::process(std::vector<Token> tokens, Path currentFile) {
    std::vector<Token> output;
    size_t idx = 0;

    while (idx < tokens.size()) {
        Token tok = tokens[idx++];

        if (tok.lex == Token::Preprocessor && tok.name == "#") {
            if (idx >= tokens.size()) {
                return std::unexpected(PreprocessorError{"stray '#' at end of file", currentFile, tok.line, tok.column});
            }
            Token directiveTok = tokens[idx++];
            const std::string directive = directiveTok.name;

            std::vector<Token> args;
            while (idx < tokens.size()) {
                Token t = tokens[idx++];
                if (t.lex == Token::Preprocessor && t.name == "\n") break;
                args.push_back(t);
            }

            bool isConditional =
                directive == "if" || directive == "elif" || directive == "else" || directive == "endif";
            if (!active() && !isConditional) {
                continue;
            }

            if (directive == "define") {
                auto result = handleDefine(args, currentFile);
                if (!result) return std::unexpected(result.error());
            } else if (directive == "include") {
                auto result = handleInclude(args, currentFile);
                if (!result) return std::unexpected(result.error());
                output.insert(output.end(), result->begin(), result->end());
            } else if (directive == "if") {
                bool parentActive = active();
                bool result = parentActive && evalCondition(args, currentFile);
                condStack_.push_back(CondFrame{ .branchActive = result, .anyTaken = result });
            } else if (directive == "elif") {
                if (condStack_.empty()) {
                    return std::unexpected(PreprocessorError{"#elif without matching #if", currentFile, directiveTok.line, directiveTok.column});
                }
                bool parentActive = activeExcludingTop();
                auto& f = condStack_.back();
                if (!parentActive || f.anyTaken) {
                    f.branchActive = false;
                } else {
                    bool result = evalCondition(args, currentFile);
                    f.branchActive = result;
                    if (result) f.anyTaken = true;
                }
            } else if (directive == "else") {
                if (condStack_.empty()) {
                    return std::unexpected(PreprocessorError{"#else without matching #if", currentFile, directiveTok.line, directiveTok.column});
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
                    return std::unexpected(PreprocessorError{"#endif without matching #if", currentFile, directiveTok.line, directiveTok.column});
                }
                condStack_.pop_back();
            } else if (directive == "pragma" || directive == "import" || directive == "module" || directive == "export") {
                // ignored
            } else {
                return std::unexpected(PreprocessorError{
                    "unknown preprocessor directive '" + directive + "'", currentFile, directiveTok.line, directiveTok.column
                });
            }
            continue;
        }

        if (active()) {
            if (tok.lex == Token::Identifier) {
                auto found = macros_.find(tok.name);
                bool guarded = std::ranges::find(expandingStack_, tok.name) != expandingStack_.end();
                if (found != macros_.end() && !guarded) {
                    const MacroDef& def = found->second;
                    if (!def.isFunctionLike) {
                        expandingStack_.push_back(tok.name);
                        auto expanded = expandTokenList(def.body, currentFile);
                        expandingStack_.pop_back();
                        if (!expanded) return std::unexpected(expanded.error());
                        output.insert(output.end(), expanded->begin(), expanded->end());
                        continue;
                    }
                    if (idx < tokens.size() && tokens[idx].name == "(") {
                        ++idx;
                        auto [callArgs, nextIndex] = gatherArgsFromVector(tokens, idx, tok, currentFile);
                        auto rescanned = substituteAndRescan(def, tok.name, std::move(callArgs), currentFile);
                        if (!rescanned) return std::unexpected(rescanned.error());
                        output.insert(output.end(), rescanned->begin(), rescanned->end());
                        idx = nextIndex;
                        continue;
                    }
                }
            }
            output.push_back(tok);
        }
    }

    return output;
}

std::expected<std::vector<Token>, PreprocessorError> Preprocessor::run(Path filepath) {
    auto text = readFile(filepath);
    if (!text) {
        return std::unexpected(PreprocessorError{
            "cannot open file: " + std::string(text.error().what()), filepath, 0, 0
        });
    }

    Path canon = fs::weakly_canonical(filepath);
    includeStack_.push_back(canon);

    std::vector<Token> initialTokens;
    processLex(std::move(*text), initialTokens);

    auto result = process(std::move(initialTokens), canon);

    includeStack_.pop_back();
    return result;
}

} // namespace lex
