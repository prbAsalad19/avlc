#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace avl {

// ============================================================
// Token kinds
// ============================================================
enum class TokenKind : uint8_t {
    // ── Literals ────────────────────────────────────────────
    Identifier,
    IntLiteral,
    FloatLiteral,
    StringLiteral,

    // ── AVL keywords ────────────────────────────────────────
    KwPack,       // pack
    KwUnsafe,     // unsafe
    KwPriority,   // priority
    KwRawPtr,     // raw_ptr
    KwIntoRaw,    // into_raw

    // ── C++ keywords the pre-pass cares about ───────────────
    KwStruct,     // struct
    KwClass,      // class
    KwVoid,       // void

    // ── Preprocessor directives (# fused with keyword) ──────
    HashInclude,  // #include
    HashHeap,     // #heap
    HashPragma,   // #pragma
    HashIfExpr,   // #ifexpr
    Hash,         // # standalone (fallback)

    // ── Double-char punctuation ──────────────────────────────
    LDoubleBracket,  // [[
    RDoubleBracket,  // ]]

    // ── Single-char punctuation ──────────────────────────────
    LBrace,    // {
    RBrace,    // }
    LParen,    // (
    RParen,    // )
    LBracket,  // [
    RBracket,  // ]
    Lt,        // <
    Gt,        // >
    Comma,     // ,
    Semi,      // ;
    Colon,     // :
    Amp,       // &
    Star,      // *
    Assign,    // =
    Dot,       // .

    // ── Trivia (preserved for source map fidelity) ───────────
    Whitespace,
    Newline,
    LineComment,
    BlockComment,

    // ── Sentinels ────────────────────────────────────────────
    Eof,
    Unknown,
};

// ============================================================
// Token
// ============================================================
struct Token {
    TokenKind        kind;
    std::string_view text;    // view into the original source buffer — zero-copy
    uint32_t         line;    // 1-based
    uint32_t         col;     // 1-based, points to the first character
    uint32_t         offset;
};

// ============================================================
// Public API
// ============================================================

/// Tokenize a complete AVL source file.
///
/// \p src must remain alive for the entire lifetime of the returned
/// vector, because every Token::text is a string_view into \p src.
///
/// Trivia (whitespace, newlines, comments) is included in the output
/// so that the pre-pass can faithfully reconstruct the source map.
[[nodiscard]]
std::vector<Token> tokenize(std::string_view src);

/// Human-readable name for a TokenKind — useful in diagnostics and tests.
[[nodiscard]]
std::string_view tokenKindName(TokenKind kind) noexcept;

} // namespace avl