#include "parser.hxx"
#include <iostream>

int main(int argc, const char** argv) {
    if (argc > 1) {
        auto tokens = lex::lex(argv[1]);
        if (!tokens) {
            std::cerr << "lex error: " << tokens.error().what() << '\n';
            return 1;
        }

        std::cout << "=== Tokens ===\n";
        for (const auto& token : *tokens) {
            std::cout << token.line << ':' << token.column
                      << "\t[" << token.lex << "]\t" << token.name << '\n';
        }

        std::cout << "\n=== Parsing ===\n";
        auto ast = ast::parse(std::move(*tokens));
        if (!ast) {
            std::cerr << "Parse error: " << ast.error().message
                      << " at line " << ast.error().line << '\n';
            return 1;
        }

        std::cout << "Parsing successful!\n";
        std::cout << "Block contains " << ast.value()->statements.size() << " statements\n";

        // Print AST summary
        for (size_t i = 0; i < ast.value()->statements.size(); ++i) {
            const auto& stmt = ast.value()->statements[i];
            if (stmt) {
                std::cout << "  Statement " << i << ": kind=" << static_cast<int>(stmt->kind) << "\n";
            }
        }
    } else {
        std::cerr << "Usage: " << argv[0] << " <file>\n";
        return 1;
    }
    return 0;
}