#include "avl/Lexer.h"

#include <cassert>
#include <cctype>
#include <unordered_map>

namespace avl {

// ============================================================
// Keyword table  (perfect-hash candidate in a future hot path)
// ============================================================
namespace {

const std::unordered_map<std::string_view, TokenKind> kKeywords = {
    // AVL-specific
    {"pack",     TokenKind::KwPack},
    {"unsafe",   TokenKind::KwUnsafe},
    {"priority", TokenKind::KwPriority},
    {"raw_ptr",  TokenKind::KwRawPtr},
    {"into_raw", TokenKind::KwIntoRaw},
    // C++ keywords the pre-pass must recognise
    {"struct",   TokenKind::KwStruct},
    {"class",    TokenKind::KwClass},
    {"void",     TokenKind::KwVoid},
};

// ============================================================
// Lexer (internal class — not exposed in the header)
// ============================================================
class Lexer {
public:
    explicit Lexer(std::string_view src) noexcept
        : src_(src) {}

    std::vector<Token> run() {
        std::vector<Token> out;
        out.reserve(src_.size() / 4); // rough heuristic

        while (pos_ < src_.size()) {
            out.push_back(nextToken());
        }

        // Always end with an explicit Eof sentinel
        out.push_back(Token{TokenKind::Eof, src_.substr(pos_, 0), line_, col_});
        return out;
    }

private:
    std::string_view src_;
    size_t   pos_  = 0;
    uint32_t line_ = 1;
    uint32_t col_  = 1;

    // ── Primitives ──────────────────────────────────────────

    char peek(size_t offset = 0) const noexcept {
        size_t i = pos_ + offset;
        return (i < src_.size()) ? src_[i] : '\0';
    }

    // Advance one character, updating line/col tracking.
    char advance() noexcept {
        char c = src_[pos_++];
        if (c == '\n') { ++line_; col_ = 1; }
        else           { ++col_; }
        return c;
    }

    void advanceN(size_t n) noexcept {
        for (size_t i = 0; i < n; ++i) advance();
    }

    // ── Token factory ───────────────────────────────────────

    // Build a token starting at \p start (absolute index into src_).
    // The caller saves startLine/startCol before consuming any characters.
    Token make(TokenKind kind, size_t start,
            uint32_t startLine, uint32_t startCol) const noexcept {
        return Token{
            kind,
            src_.substr(start, pos_ - start),
            startLine,
            startCol,
            static_cast<uint32_t>(start)   // ← offset
        };
    }

    // ── Core dispatch ───────────────────────────────────────

    Token nextToken() {
        const size_t   start     = pos_;
        const uint32_t startLine = line_;
        const uint32_t startCol  = col_;
        const char     c         = peek();

        // ── Whitespace (excluding newlines, kept separate) ──
        if (c == ' ' || c == '\t' || c == '\r') {
            while (peek() == ' ' || peek() == '\t' || peek() == '\r')
                advance();
            return make(TokenKind::Whitespace, start, startLine, startCol);
        }

        // ── Newline ─────────────────────────────────────────
        if (c == '\n') {
            advance();
            return make(TokenKind::Newline, start, startLine, startCol);
        }

        // ── Line comment //... ───────────────────────────────
        if (c == '/' && peek(1) == '/') {
            while (pos_ < src_.size() && peek() != '\n')
                advance();
            return make(TokenKind::LineComment, start, startLine, startCol);
        }

        // ── Block comment /*...*/ ────────────────────────────
        if (c == '/' && peek(1) == '*') {
            advanceN(2);
            while (pos_ < src_.size()) {
                if (peek() == '*' && peek(1) == '/') { advanceN(2); break; }
                advance();
            }
            return make(TokenKind::BlockComment, start, startLine, startCol);
        }

        // ── Preprocessor directives (#heap, #include, …) ────
        if (c == '#') {
            // We check the characters immediately after '#' without advancing,
            // then commit only if the boundary character is non-identifier.
            auto tryHashKw = [&](std::string_view kw, TokenKind k) -> bool {
                // kw is the part AFTER '#', e.g. "heap"
                if (src_.substr(pos_ + 1, kw.size()) != kw)
                    return false;
                // Boundary check: next char after the keyword must not be [A-Za-z0-9_]
                char boundary = (pos_ + 1 + kw.size() < src_.size())
                                ? src_[pos_ + 1 + kw.size()] : '\0';
                if (std::isalnum(static_cast<unsigned char>(boundary)) || boundary == '_')
                    return false;
                advanceN(1 + kw.size());
                return true;
            };

            if (tryHashKw("include", TokenKind::HashInclude))
                return make(TokenKind::HashInclude, start, startLine, startCol);
            if (tryHashKw("heap",    TokenKind::HashHeap))
                return make(TokenKind::HashHeap,    start, startLine, startCol);
            if (tryHashKw("pragma",  TokenKind::HashPragma))
                return make(TokenKind::HashPragma,  start, startLine, startCol);
            if (tryHashKw("ifexpr",  TokenKind::HashIfExpr))
                return make(TokenKind::HashIfExpr,  start, startLine, startCol);

            // Bare '#'
            advance();
            return make(TokenKind::Hash, start, startLine, startCol);
        }

        // ── [[ and ]] ────────────────────────────────────────
        if (c == '[' && peek(1) == '[') { advanceN(2); return make(TokenKind::LDoubleBracket, start, startLine, startCol); }
        if (c == ']' && peek(1) == ']') { advanceN(2); return make(TokenKind::RDoubleBracket, start, startLine, startCol); }

        // ── Single-char punctuation ──────────────────────────
        {
            TokenKind k = TokenKind::Unknown;
            switch (c) {
                case '{': k = TokenKind::LBrace;    break;
                case '}': k = TokenKind::RBrace;    break;
                case '(': k = TokenKind::LParen;    break;
                case ')': k = TokenKind::RParen;    break;
                case '[': k = TokenKind::LBracket;  break;
                case ']': k = TokenKind::RBracket;  break;
                case '<': k = TokenKind::Lt;        break;
                case '>': k = TokenKind::Gt;        break;
                case ',': k = TokenKind::Comma;     break;
                case ';': k = TokenKind::Semi;      break;
                case ':': k = TokenKind::Colon;     break;
                case '&': k = TokenKind::Amp;       break;
                case '*': k = TokenKind::Star;      break;
                case '=': k = TokenKind::Assign;    break;
                case '.': k = TokenKind::Dot;       break;
                default: break;
            }
            if (k != TokenKind::Unknown) {
                advance();
                return make(k, start, startLine, startCol);
            }
        }

        // ── String literal "..." ─────────────────────────────
        if (c == '"') {
            advance(); // opening "
            while (pos_ < src_.size() && peek() != '"') {
                if (peek() == '\\') advance(); // skip escape prefix
                advance();
            }
            if (pos_ < src_.size()) advance(); // closing "
            return make(TokenKind::StringLiteral, start, startLine, startCol);
        }

        // ── Numeric literal ──────────────────────────────────
        if (std::isdigit(static_cast<unsigned char>(c))) {
            while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
            bool isFloat = false;
            if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
                isFloat = true;
                advance(); // '.'
                while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
            }
            // Optional f/F suffix
            if (peek() == 'f' || peek() == 'F') advance();
            return make(isFloat ? TokenKind::FloatLiteral : TokenKind::IntLiteral,
                        start, startLine, startCol);
        }

        // ── Identifier or keyword ────────────────────────────
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')
                advance();
            std::string_view text = src_.substr(start, pos_ - start);
            auto it = kKeywords.find(text);
            TokenKind k = (it != kKeywords.end()) ? it->second : TokenKind::Identifier;
            return make(k, start, startLine, startCol);
        }

        // ── Unknown character ────────────────────────────────
        advance();
        return make(TokenKind::Unknown, start, startLine, startCol);
    }
};

} // anonymous namespace

// ============================================================
// Public functions
// ============================================================

std::vector<Token> tokenize(std::string_view src) {
    return Lexer(src).run();
}

std::string_view tokenKindName(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::Identifier:      return "Identifier";
        case TokenKind::IntLiteral:      return "IntLiteral";
        case TokenKind::FloatLiteral:    return "FloatLiteral";
        case TokenKind::StringLiteral:   return "StringLiteral";
        case TokenKind::KwPack:          return "pack";
        case TokenKind::KwUnsafe:        return "unsafe";
        case TokenKind::KwPriority:      return "priority";
        case TokenKind::KwRawPtr:        return "raw_ptr";
        case TokenKind::KwIntoRaw:       return "into_raw";
        case TokenKind::KwStruct:        return "struct";
        case TokenKind::KwClass:         return "class";
        case TokenKind::KwVoid:          return "void";
        case TokenKind::HashInclude:     return "#include";
        case TokenKind::HashHeap:        return "#heap";
        case TokenKind::HashPragma:      return "#pragma";
        case TokenKind::HashIfExpr:      return "#ifexpr";
        case TokenKind::Hash:            return "#";
        case TokenKind::LDoubleBracket:  return "[[";
        case TokenKind::RDoubleBracket:  return "]]";
        case TokenKind::LBrace:          return "{";
        case TokenKind::RBrace:          return "}";
        case TokenKind::LParen:          return "(";
        case TokenKind::RParen:          return ")";
        case TokenKind::LBracket:        return "[";
        case TokenKind::RBracket:        return "]";
        case TokenKind::Lt:              return "<";
        case TokenKind::Gt:              return ">";
        case TokenKind::Comma:           return ",";
        case TokenKind::Semi:            return ";";
        case TokenKind::Colon:           return ":";
        case TokenKind::Amp:             return "&";
        case TokenKind::Star:            return "*";
        case TokenKind::Assign:          return "=";
        case TokenKind::Dot:             return ".";
        case TokenKind::Whitespace:      return "Whitespace";
        case TokenKind::Newline:         return "Newline";
        case TokenKind::LineComment:     return "//...";
        case TokenKind::BlockComment:    return "/*...*/";
        case TokenKind::Eof:             return "EOF";
        case TokenKind::Unknown:         return "Unknown";
    }
    return "?";
}

} // namespace avl