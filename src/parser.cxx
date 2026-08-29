#include "parser.hxx"

namespace ast {

class Parser {
public:
    Parser(std::vector<lex::Token> tokens)
        : tokens_(std::move(tokens)), current_(0) {}

    std::expected<std::unique_ptr<Block>, ParseError> parse() {
        std::vector<std::unique_ptr<Stmt>> statements;
        while (current_ < tokens_.size()) {
            auto stmt = parseStatement();
            if (!stmt) return std::unexpected(stmt.error());
            if (*stmt) {
                statements.push_back(std::move(*stmt));
            }
        }
        return std::make_unique<Block>(std::move(statements));
    }

private:
    std::vector<lex::Token> tokens_;
    size_t current_;

    const lex::Token& current() const {
        if (current_ >= tokens_.size()) {
            static lex::Token eof{lex::Token::None, "", 0, 0};
            return eof;
        }
        return tokens_[current_];
    }

    const lex::Token& peek(size_t offset = 1) const {
        size_t idx = current_ + offset;
        if (idx >= tokens_.size()) {
            static lex::Token eof{lex::Token::None, "", 0, 0};
            return eof;
        }
        return tokens_[idx];
    }

    void advance() {
        if (current_ < tokens_.size()) {
            ++current_;
        }
    }

    bool match(const std::string& name) {
        if (current().name == name) {
            advance();
            return true;
        }
        return false;
    }

    bool match(lex::Token::Lex type) {
        if (current().lex == type) {
            advance();
            return true;
        }
        return false;
    }

    std::expected<void, ParseError> expect(const std::string& name) {
        if (current().name == name) {
            advance();
            return {};
        }
        return std::unexpected(ParseError{
            "Expected '" + name + "' at line " +
            std::to_string(current().line) +
            ", got '" + current().name + "'",
            current().line,
            current().column
        });
    }

    std::expected<std::unique_ptr<Stmt>, ParseError> parseStatement() {
        if (current().lex == lex::Token::None) {
            return nullptr;
        }

        // Handle declarations first (var decl or func decl)
        if (match("type") || match("static") || match("inline") ||
            match("constexpr") || match("consteval") || match("void") ||
            match("auto") || match("ptr") || match("mem") || match("class")) {
            // This is a declaration - need to look ahead
            --current_; // backtrack
            auto decl = parseDeclaration();
            if (!decl) return std::unexpected(decl.error());
            return std::make_unique<DeclStmt>(std::move(*decl));
        }

        if (match("copy")) {
            return parseCopy();
        }
        if (match("move")) {
            return parseMove();
        }
        if (match("return")) {
            return parseReturn();
        }
        if (match("loop")) {
            return parseLoop();
        }
        if (match("match")) {
            return parseMatch();
        }
        if (match("goto")) {
            return parseGoto();
        }
        if (match("{")) {
            return parseBlock();
        }

        // Check for label (identifier followed by colon)
        if (current().lex == lex::Token::Identifier && peek().name == ":") {
            std::string labelName = current().name;
            advance(); // consume identifier
            advance(); // consume colon
            return std::make_unique<Label>(labelName);
        }

        // Expression statement or declaration
        if (current().lex == lex::Token::Identifier) {
            std::string name = current().name;
            advance();

            // Check if this is a function call
            if (current().name == "(") {
                advance(); // consume '('
                std::vector<std::unique_ptr<Expr>> args;
                if (!match(")")) {
                    do {
                        auto arg = parseExpression();
                        if (!arg) return std::unexpected(arg.error());
                        args.push_back(std::move(*arg));
                    } while (match(","));
                    auto res = expect(")");
                    if (!res) return std::unexpected(res.error());
                }
                auto call = std::make_unique<Call>(name, std::move(args));
                return std::make_unique<ExprStmt>(std::move(call));
            }

            // Otherwise it's an expression statement
            // For now, just return null - need more expression parsing
            return std::unexpected(ParseError{
                "Unexpected identifier at line " + std::to_string(current().line),
                current().line,
                current().column
            });
        }

        return std::unexpected(ParseError{
            "Unexpected token '" + current().name + "' at line " + std::to_string(current().line),
            current().line,
            current().column
        });
    }

    std::expected<std::unique_ptr<Decl>, ParseError> parseDeclaration() {
        // Simplified: assume "type name = expr;" or "type name(params) { body }"
        advance(); // consume type keyword (or storage specifier)

        if (current().lex != lex::Token::Identifier) {
            return std::unexpected(ParseError{
                "Expected identifier after type at line " + std::to_string(current().line),
                current().line,
                current().column
            });
        }

        std::string typeName = current().name;
        advance();

        // Check if this is a function declaration - look for '(' after identifier
        if (current().name == "(") {
            return parseFuncDecl(typeName);
        }

        // Check for semicolon immediately (e.g., "class int;")
        if (match(";")) {
            // This is a forward declaration or empty declaration
            return std::make_unique<VarDecl>(typeName, "", nullptr);
        }

        // Variable declaration - expect another identifier for the variable name
        if (current().lex != lex::Token::Identifier) {
            return std::unexpected(ParseError{
                "Expected variable name after type at line " + std::to_string(current().line),
                current().line,
                current().column
            });
        }

        std::string name = current().name;
        advance();

        std::string typeStr = typeName;
        std::unique_ptr<Expr> initializer;

        if (match("=")) {
            auto init = parseExpression();
            if (!init) return std::unexpected(init.error());
            initializer = std::move(*init);
        }

        if (match(";")) {
            return std::make_unique<VarDecl>(name, typeStr, std::move(initializer));
        }

        return std::unexpected(ParseError{
            "Expected ';' at line " + std::to_string(current().line),
            current().line,
            current().column
        });
    }

    std::expected<std::unique_ptr<FuncDecl>, ParseError> parseFuncDecl(const std::string& name) {
        auto res = expect("(");
        if (!res) return std::unexpected(res.error());
        std::vector<std::pair<std::string, std::string>> params;

        if (!match(")")) {
            do {
                if (current().lex != lex::Token::Identifier) {
                    return std::unexpected(ParseError{
                        "Expected parameter name at line " + std::to_string(current().line),
                        current().line,
                        current().column
                    });
                }
                std::string paramName = current().name;
                advance();
                std::string paramType = "auto"; // simplified
                params.emplace_back(paramName, paramType);
            } while (match(","));
            res = expect(")");
            if (!res) return std::unexpected(res.error());
        }

        res = expect("{");
        if (!res) return std::unexpected(res.error());
        std::vector<std::unique_ptr<Stmt>> bodyStmts;
        while (!match("}")) {
            auto stmt = parseStatement();
            if (!stmt) return std::unexpected(stmt.error());
            bodyStmts.push_back(std::move(*stmt));
            if (current().lex == lex::Token::None) {
                return std::unexpected(ParseError{
                    "Unexpected end of file in function body",
                    current().line,
                    current().column
                });
            }
        }

        return std::make_unique<FuncDecl>(
            name, "auto", std::move(params),
            std::make_unique<Block>(std::move(bodyStmts))
        );
    }

    std::expected<std::unique_ptr<Stmt>, ParseError> parseCopy() {
        // copy(type name, value); or copy(name, value);
        std::string typeStr;
        std::string target;

        if (current().lex != lex::Token::Identifier) {
            return std::unexpected(ParseError{
                "Expected identifier after 'copy' at line " + std::to_string(current().line),
                current().line,
                current().column
            });
        }

        std::string first = current().name;
        advance();

        // Check if next token is also an identifier (type name pattern)
        if (current().lex == lex::Token::Identifier && first != "(") {
            // This is "type name" pattern
            typeStr = first;
            target = current().name;
            advance();
        } else {
            // Just "name" pattern
            target = first;
            typeStr = "auto";
        }

        // Expect comma
        if (!match(",")) {
            return std::unexpected(ParseError{
                "Expected ',' after variable name in 'copy' at line " + std::to_string(current().line),
                current().line,
                current().column
            });
        }

        // Expect some kind of value expression
        auto value = parseExpression();
        if (!value) return std::unexpected(value.error());

        if (match(";")) {
            return std::make_unique<Copy>(target, std::move(*value));
        }

        return std::unexpected(ParseError{
            "Expected ';' after copy statement at line " + std::to_string(current().line),
            current().line,
            current().column
        });
    }

    std::expected<std::unique_ptr<Stmt>, ParseError> parseMove() {
        // move(type name, value); or move(name, value);
        std::string typeStr;
        std::string target;

        if (current().lex != lex::Token::Identifier) {
            return std::unexpected(ParseError{
                "Expected identifier after 'move' at line " + std::to_string(current().line),
                current().line,
                current().column
            });
        }

        std::string first = current().name;
        advance();

        // Check if next token is also an identifier (type name pattern)
        if (current().lex == lex::Token::Identifier && first != "(") {
            // This is "type name" pattern
            typeStr = first;
            target = current().name;
            advance();
        } else {
            // Just "name" pattern
            target = first;
            typeStr = "auto";
        }

        // Expect comma
        if (!match(",")) {
            return std::unexpected(ParseError{
                "Expected ',' after variable name in 'move' at line " + std::to_string(current().line),
                current().line,
                current().column
            });
        }

        auto value = parseExpression();
        if (!value) return std::unexpected(value.error());

        if (match(";")) {
            return std::make_unique<Move>(target, std::move(*value));
        }

        return std::unexpected(ParseError{
            "Expected ';' after move statement at line " + std::to_string(current().line),
            current().line,
            current().column
        });
    }

    std::expected<std::unique_ptr<Stmt>, ParseError> parseReturn() {
        std::unique_ptr<Expr> value;
        if (current().name != ";") {
            auto val = parseExpression();
            if (!val) return std::unexpected(val.error());
            value = std::move(*val);
        }
        auto res = expect(";");
        if (!res) return std::unexpected(res.error());
        return std::make_unique<Return>(std::move(value));
    }

    std::expected<std::unique_ptr<Stmt>, ParseError> parseLoop() {
        // loop (condition) { body } - condition is optional
        std::unique_ptr<Expr> condition;
        if (current().name == "(") {
            advance(); // consume '('
            auto cond = parseExpression();
            if (!cond) return std::unexpected(cond.error());
            condition = std::move(*cond);
            auto res = expect(")");
            if (!res) return std::unexpected(res.error());
        }

        auto res = expect("{");
        if (!res) return std::unexpected(res.error());
        std::vector<std::unique_ptr<Stmt>> bodyStmts;

        while (!match("}")) {
            auto stmt = parseStatement();
            if (!stmt) return std::unexpected(stmt.error());
            bodyStmts.push_back(std::move(*stmt));
            if (current().lex == lex::Token::None) {
                return std::unexpected(ParseError{
                    "Unexpected end of file in loop body",
                    current().line,
                    current().column
                });
            }
        }

        auto body = std::make_unique<Block>(std::move(bodyStmts));
        // Store condition as an expression wrapped in a statement if present
        std::unique_ptr<Stmt> condStmt;
        if (condition) {
            condStmt = std::make_unique<ExprStmt>(std::move(condition));
        }
        return std::make_unique<Loop>(std::move(body), std::move(condStmt));
    }

    std::expected<std::unique_ptr<Stmt>, ParseError> parseMatch() {
        // match (expr) { pattern: body, ... }
        auto res = expect("(");
        if (!res) return std::unexpected(res.error());
        auto expr = parseExpression();
        if (!expr) return std::unexpected(expr.error());
        res = expect(")");
        if (!res) return std::unexpected(res.error());
        res = expect("{");
        if (!res) return std::unexpected(res.error());

        struct MatchArm {
            std::unique_ptr<Expr> pattern;
            std::unique_ptr<Stmt> body;
        };
        std::vector<MatchArm> arms;

        while (!match("}")) {
            // Pattern (could be literal, identifier, or default)
            std::unique_ptr<Expr> pattern;
            if (match("default")) {
                pattern = std::make_unique<Identifier>("default");
            } else {
                auto pat = parseExpression();
                if (!pat) return std::unexpected(pat.error());
                pattern = std::move(*pat);
            }

            res = expect(":");
            if (!res) return std::unexpected(res.error());

            // Body can be a single statement or a block
            std::unique_ptr<Stmt> armBody;
            if (current().name == "{") {
                auto blk = parseBlock();
                if (!blk) return std::unexpected(blk.error());
                armBody = std::move(*blk);
            } else {
                auto stmt = parseStatement();
                if (!stmt) return std::unexpected(stmt.error());
                armBody = std::move(*stmt);
            }

            arms.push_back({std::move(pattern), std::move(armBody)});

            if (current().lex == lex::Token::None) {
                return std::unexpected(ParseError{
                    "Unexpected end of file in match expression",
                    current().line,
                    current().column
                });
            }
        }

        // For now, just return the first arm's body as a simplification
        // A full implementation would generate proper match logic in LLVM
        if (!arms.empty()) {
            return std::move(arms[0].body);
        }

        return std::make_unique<Block>(std::vector<std::unique_ptr<Stmt>>{});
    }

    std::expected<std::unique_ptr<Stmt>, ParseError> parseGoto() {
        if (current().lex != lex::Token::Identifier) {
            return std::unexpected(ParseError{
                "Expected label name after 'goto' at line " + std::to_string(current().line),
                current().line,
                current().column
            });
        }
        std::string label = current().name;
        advance();
        auto res = expect(";");
        if (!res) return std::unexpected(res.error());
        return std::make_unique<Goto>(label);
    }

    std::expected<std::unique_ptr<Block>, ParseError> parseBlock() {
        auto res = expect("{");
        if (!res) return std::unexpected(res.error());
        std::vector<std::unique_ptr<Stmt>> statements;

        while (!match("}")) {
            auto stmt = parseStatement();
            if (!stmt) return std::unexpected(stmt.error());
            statements.push_back(std::move(*stmt));
            if (current().lex == lex::Token::None) {
                return std::unexpected(ParseError{
                    "Unexpected end of file in block",
                    current().line,
                    current().column
                });
            }
        }

        return std::make_unique<Block>(std::move(statements));
    }

    std::expected<std::unique_ptr<Expr>, ParseError> parseExpression() {
        // Simplified expression parsing - handles integers and identifiers
        if (current().lex == lex::Token::Literal) {
            std::string lit = current().name;
            advance();
            // Remove quotes if present
            if ((lit.front() == '\'' && lit.back() == '\'') ||
                (lit.front() == '"' && lit.back() == '"')) {
                lit = lit.substr(1, lit.size() - 2);
            }
            long long value = std::stoll(lit);
            return std::make_unique<IntegerLiteral>(value);
        }

        if (current().lex == lex::Token::Identifier) {
            std::string name = current().name;
            advance();

            // Check for function call
            if (current().name == "(") {
                advance(); // consume '('
                std::vector<std::unique_ptr<Expr>> args;
                if (!match(")")) {
                    do {
                        auto arg = parseExpression();
                        if (!arg) return std::unexpected(arg.error());
                        args.push_back(std::move(*arg));
                    } while (match(","));
                    auto res = expect(")");
                    if (!res) return std::unexpected(res.error());
                }
                return std::make_unique<Call>(name, std::move(args));
            }

            return std::make_unique<Identifier>(name);
        }

        return std::unexpected(ParseError{
            "Unexpected token in expression at line " + std::to_string(current().line),
            current().line,
            current().column
        });
    }
};

// Need to add these to the AST header first - MOVED TO HEADER
std::expected<std::unique_ptr<Block>, ParseError> parse(std::vector<lex::Token> tokens) {
    Parser parser{tokens};
    return parser.parse();
}

} // namespace ast
