#include "avl/Lexer.h"
#include "avl/PrePass.h"
#include "avl/ema.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

// ============================================================
// File I/O
// ============================================================

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "avlc: error: cannot open '%s'\n", path.c_str());
        std::exit(1);
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ============================================================
// CLI parsing
// ============================================================

struct Options {
    std::string inputFile;
    bool        dumpCpp       = false;
    bool        dumpSourceMap = false;
    bool        dumpTokens    = false;
};

static void printUsage() {
    std::fputs(
        "Usage: avlc <file.avl> [options]\n"
        "\n"
        "Options:\n"
        "  --dump-cpp          Print the generated C++ source\n"
        "  --dump-sourcemap    Print the AVL ↔ C++ source map\n"
        "  --dump-tokens       Print the token stream (debug)\n"
        "\n",
        stderr);
}

static Options parseArgs(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        std::exit(1);
    }

    Options opts;
    opts.inputFile = argv[1];

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if      (arg == "--dump-cpp")       opts.dumpCpp       = true;
        else if (arg == "--dump-sourcemap") opts.dumpSourceMap = true;
        else if (arg == "--dump-tokens")    opts.dumpTokens    = true;
        else {
            std::fprintf(stderr, "avlc: error: unknown option '%s'\n", arg.c_str());
            printUsage();
            std::exit(1);
        }
    }

    // Default: if no dump flag is set, act as --dump-cpp
    if (!opts.dumpCpp && !opts.dumpSourceMap && !opts.dumpTokens)
        opts.dumpCpp = true;

    return opts;
}

// ============================================================
// Token dump (--dump-tokens)
// ============================================================

static void dumpTokens(const std::vector<avl::Token>& tokens) {
    std::printf("%-6s  %-20s  %s\n", "LINE", "KIND", "TEXT");
    std::printf("%-6s  %-20s  %s\n", "----", "----", "----");
    for (const auto& tok : tokens) {
        // Skip trivia in dump for readability
        if (tok.kind == avl::TokenKind::Whitespace ||
            tok.kind == avl::TokenKind::Newline)
            continue;
        std::printf("%4u:%-2u  %-20.*s  |%.*s|\n",
            tok.line, tok.col,
            20, avl::tokenKindName(tok.kind).data(),
            static_cast<int>(tok.text.size()), tok.text.data());
    }
}

// ============================================================
// main
// ============================================================

int main(int argc, char* argv[]) {
    Options opts = parseArgs(argc, argv);

    // ── 1. Read source ──────────────────────────────────────
    std::string src = readFile(opts.inputFile);

    // ── 2. Tokenize ─────────────────────────────────────────
    std::vector<avl::Token> tokens = avl::tokenize(src);

    if (opts.dumpTokens) {
        dumpTokens(tokens);
        // Can be combined with other flags — fall through
    }

    // ── 3. Pre-pass ─────────────────────────────────────────
    avl::PrePassResult result = avl::runPrePass(tokens, src, opts.inputFile);

    // ── 4. Output ───────────────────────────────────────────
    if (opts.dumpCpp) {
        std::fputs(result.cppSource.c_str(), stdout);
        // Ensure the output ends with a newline
        if (!result.cppSource.empty() && result.cppSource.back() != '\n')
            std::fputc('\n', stdout);
    }

    if (opts.dumpSourceMap) {
        result.sourceMap.dump(opts.inputFile, "<generated>");
    }

    return 0;
}