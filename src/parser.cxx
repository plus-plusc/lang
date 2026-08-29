#include "parser.hxx"
#include <sstream>

namespace ast {

class Parser {
public:
    Parser(std::vector<lex::Token> tokens)
        : tokens_(std::move(tokens)), current_(0) {}

    std::unique_ptr<Block> parse() {
        std::vector<std::unique_ptr<Stmt>> statements;
        while (current_ < tokens_.size()) {
            auto stmt = parseStatement();
            if (stmt) {
                statements.push_back(std::move(stmt));
            }
        }
        return std::make_unique<Block>(std::move(statements));
    }

private:
    const std::vector<lex::Token>& tokens_;
    size_t current_;

    const lex::Token& current() const {
        if (current_ >= tokens_.size()) {
            static lex::Token eof{.lex = lex::Token::None, .name = "", .line = 0, .column = 0};
            return eof;
        }
        return tokens_[current_];
    }

    const lex::Token& peek(size_t offset = 1) const {
        size_t idx = current_ + offset;
        if (idx >= tokens_.size()) {
            static lex::Token eof{.lex = lex::Token::None, .name = "", .line = 0, .column = 0};
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

    bool expect(const std::string& name) {
        if (current().name == name) {
            advance();
            return true;
        }
        throw std::runtime_error("Expected '" + name + "' at line " +
                                std::to_string(current().line) +
                                ", got '" + current().name + "'");
    }

    std::unique_ptr<Stmt> parseStatement() {
        if (current().lex == lex::Token::None) {
            return nullptr;
        }

        // Handle declarations first (var decl or func decl)
        if (match("type") || match("static") || match("inline") ||
            match("constexpr") || match("consteval")) {
            // This is a declaration - need to look ahead
            --current_; // backtrack
            auto decl = parseDeclaration();
            return std::make_unique<DeclStmt>(std::move(decl));
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
                        args.push_back(parseExpression());
                    } while (match(","));
                    expect(")");
                }
                auto call = std::make_unique<Call>(name, std::move(args));
                return std::make_unique<ExprStmt>(std::move(call));
            }

            // Otherwise it's an expression statement
            // For now, just return null - need more expression parsing
            throw std::runtime_error("Unexpected identifier at line " +
                                    std::to_string(current().line));
        }

        throw std::runtime_error("Unexpected token '" + current().name +
                                "' at line " + std::to_string(current().line));
    }

    std::unique_ptr<Decl> parseDeclaration() {
        // Simplified: assume "type name = expr;" or "type name(params) { body }"
        advance(); // consume type keyword (or storage specifier)

        if (current().lex != lex::Token::Identifier) {
            throw std::runtime_error("Expected identifier after type at line " +
                                    std::to_string(current().line));
        }

        std::string name = current().name;
        advance();

        // Check if this is a function declaration
        if (current().name == "(") {
            return parseFuncDecl(name);
        }

        // Variable declaration
        std::string typeStr = "auto"; // simplified
        std::unique_ptr<Expr> initializer;

        if (match("=")) {
            initializer = parseExpression();
        }

        if (match(";")) {
            return std::make_unique<VarDecl>(name, typeStr, std::move(initializer));
        }

        throw std::runtime_error("Expected ';' at line " +
                                std::to_string(current().line));
    }

    std::unique_ptr<FuncDecl> parseFuncDecl(const std::string& name) {
        expect("(");
        std::vector<std::pair<std::string, std::string>> params;

        if (!match(")")) {
            do {
                if (current().lex != lex::Token::Identifier) {
                    throw std::runtime_error("Expected parameter name at line " +
                                            std::to_string(current().line));
                }
                std::string paramName = current().name;
                advance();
                std::string paramType = "auto"; // simplified
                params.emplace_back(paramName, paramType);
            } while (match(","));
            expect(")");
        }

        expect("{");
        std::vector<std::unique_ptr<Stmt>> bodyStmts;
        while (!match("}")) {
            if (auto stmt = parseStatement()) {
                bodyStmts.push_back(std::move(stmt));
            }
            if (current().lex == lex::Token::None) {
                throw std::runtime_error("Unexpected end of file in function body");
            }
        }

        return std::make_unique<FuncDecl>(
            name, "auto", std::move(params),
            std::make_unique<Block>(std::move(bodyStmts))
        );
    }

    std::unique_ptr<Stmt> parseCopy() {
        if (current().lex != lex::Token::Identifier) {
            throw std::runtime_error("Expected identifier after 'copy' at line " +
                                    std::to_string(current().line));
        }
        std::string target = current().name;
        advance();

        // Expect some kind of value expression
        auto value = parseExpression();

        if (match(";")) {
            return std::make_unique<Copy>(target, std::move(value));
        }

        throw std::runtime_error("Expected ';' after copy statement at line " +
                                std::to_string(current().line));
    }

    std::unique_ptr<Stmt> parseMove() {
        if (current().lex != lex::Token::Identifier) {
            throw std::runtime_error("Expected identifier after 'move' at line " +
                                    std::to_string(current().line));
        }
        std::string target = current().name;
        advance();

        auto value = parseExpression();

        if (match(";")) {
            return std::make_unique<Move>(target, std::move(value));
        }

        throw std::runtime_error("Expected ';' after move statement at line " +
                                std::to_string(current().line));
    }

    std::unique_ptr<Stmt> parseReturn() {
        std::unique_ptr<Expr> value;
        if (current().name != ";") {
            value = parseExpression();
        }
        expect(";");
        return std::make_unique<Return>(std::move(value));
    }

    std::unique_ptr<Stmt> parseLoop() {
        expect("{");
        std::vector<std::unique_ptr<Stmt>> bodyStmts;

        while (!match("}")) {
            if (auto stmt = parseStatement()) {
                bodyStmts.push_back(std::move(stmt));
            }
            if (current().lex == lex::Token::None) {
                throw std::runtime_error("Unexpected end of file in loop body");
            }
        }

        auto body = std::make_unique<Block>(std::move(bodyStmts));
        return std::make_unique<Loop>(std::move(body));
    }

    std::unique_ptr<Stmt> parseMatch() {
        auto expr = parseExpression();
        expect("{");

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
                pattern = parseExpression();
            }

            expect("=>");

            // Body can be a single statement or a block
            std::unique_ptr<Stmt> armBody;
            if (current().name == "{") {
                armBody = parseBlock();
            } else {
                armBody = parseStatement();
            }

            arms.push_back({std::move(pattern), std::move(armBody)});

            if (current().lex == lex::Token::None) {
                throw std::runtime_error("Unexpected end of file in match expression");
            }
        }

        // For now, just return the first arm's body as a simplification
        // A full implementation would generate proper match logic in LLVM
        if (!arms.empty()) {
            return std::move(arms[0].body);
        }

        return std::make_unique<Block>(std::vector<std::unique_ptr<Stmt>>{});
    }

    std::unique_ptr<Stmt> parseGoto() {
        if (current().lex != lex::Token::Identifier) {
            throw std::runtime_error("Expected label name after 'goto' at line " +
                                    std::to_string(current().line));
        }
        std::string label = current().name;
        advance();
        expect(";");
        return std::make_unique<Goto>(label);
    }

    std::unique_ptr<Block> parseBlock() {
        expect("{");
        std::vector<std::unique_ptr<Stmt>> statements;

        while (!match("}")) {
            if (auto stmt = parseStatement()) {
                statements.push_back(std::move(stmt));
            }
            if (current().lex == lex::Token::None) {
                throw std::runtime_error("Unexpected end of file in block");
            }
        }

        return std::make_unique<Block>(std::move(statements));
    }

    std::unique_ptr<Expr> parseExpression() {
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
                        args.push_back(parseExpression());
                    } while (match(","));
                    expect(")");
                }
                return std::make_unique<Call>(name, std::move(args));
            }

            return std::make_unique<Identifier>(name);
        }

        throw std::runtime_error("Unexpected token in expression at line " +
                                std::to_string(current().line));
    }
};

// Need to add these to the AST header first - MOVED TO HEADER
std::unique_ptr<Block> parse(std::vector<lex::Token> tokens) {
    Parser parser(std::move(tokens));
    return parser.parse();
}

} // namespace ast
