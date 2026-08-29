#include "lex.hxx"
#include <iostream>

int main(int argc, const char** argv) {
    if (argc > 1) {
        auto x = lex::lex(argv[1]);
        if (x) {
            for (auto&& token : *x) {
                std::cout << token.line << ':' << token.column
                          << "\t[" << token.lex << "]\t" << token.name << '\n';
            }
        } else {
            std::cerr << "lex error: " << x.error().what() << '\n';
        }
    }
    return 0;
}