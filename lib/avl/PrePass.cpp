#include "avl/PrePass.h"

#include <cassert>
#include <string>
#include <vector>

namespace avl {
namespace {

// ============================================================
// Sentinel returned when indexing past end of token stream
// ============================================================
static const Token kEofSentinel{TokenKind::Eof, {}, 0, 0, 0};

// ============================================================
// Transformer
// ============================================================
class Transformer {
public:
    Transformer(const std::vector<Token>& tokens, std::string_view src)
        : tokens_(tokens), src_(src) {}

    PrePassResult run() {
        while (!atEnd()) {
            bool handled = false;

            switch (current().kind) {
                case TokenKind::LDoubleBracket: handled = tryDoubleBracket();   break;
                case TokenKind::Lt:             handled = tryMultiReturn();     break;
                case TokenKind::LBrace:         handled = tryDestructuring();   break;
                case TokenKind::KwPack:         handled = tryPack();            break;
                case TokenKind::HashHeap:       handled = tryHeapAnnotation();  break;
                case TokenKind::KwUnsafe:       handled = tryUnsafe();          break;
                case TokenKind::KwPriority:     handled = tryPriority();        break;
                case TokenKind::KwRawPtr:
                case TokenKind::KwIntoRaw:      handled = tryRawIntrinsics();   break;
                default: break;
            }

            if (!handled) passThrough();
        }

        return {std::move(out_), std::move(map_)};
    }

private:
    const std::vector<Token>& tokens_;
    std::string_view           src_;
    size_t                     pos_ = 0;

    std::string out_;
    SourceMap   map_;

    // Output cursor — tracks where we are in the generated C++ buffer
    uint32_t outLine_   = 1;
    uint32_t outCol_    = 1;
    uint32_t outOffset_ = 0;

    // ── Token stream ─────────────────────────────────────────

    bool atEnd() const {
        return pos_ >= tokens_.size() ||
               tokens_[pos_].kind == TokenKind::Eof;
    }

    const Token& current() const { return at(pos_); }

    const Token& at(size_t i) const {
        return (i < tokens_.size()) ? tokens_[i] : kEofSentinel;
    }

    static bool isTrivia(TokenKind k) {
        return k == TokenKind::Whitespace ||
               k == TokenKind::Newline    ||
               k == TokenKind::LineComment ||
               k == TokenKind::BlockComment;
    }

    // Index of the first non-trivia token at or after index i
    size_t skipTrivia(size_t i) const {
        while (i < tokens_.size() && isTrivia(tokens_[i].kind)) ++i;
        return i;
    }

    // ── Output ───────────────────────────────────────────────

    SourcePos outPos() const { return {outLine_, outCol_, outOffset_}; }

    void emitRaw(std::string_view text) {
        out_ += text;
        for (unsigned char c : text) {
            ++outOffset_;
            if (c == '\n') { ++outLine_; outCol_ = 1; }
            else           { ++outCol_; }
        }
    }

    // Pass token through verbatim, recording a 1:1 source map entry.
    // Trivia tokens are passed through without a map entry — too noisy,
    // and they never appear as Clang diagnostic sites.
    void passThrough() {
        const Token& tok = current();

        if (!isTrivia(tok.kind)) {
            uint32_t len = static_cast<uint32_t>(tok.text.size());
            SourceMapEntry e;
            e.avlSpan    = {{tok.line, tok.col, tok.offset},
                            {tok.line, tok.col + len, tok.offset + len}};
            e.cppSpan    = {outPos(),
                            {outLine_, outCol_ + len, outOffset_ + len}};
            e.isSynthetic = false;
            map_.append(e);
        }

        emitRaw(tok.text);
        ++pos_;
    }

    // ── Synthetic emission ───────────────────────────────────

    // Emit `replacement` for the AVL token range [fromIdx, toIdx).
    // Records a synthetic source map entry and advances pos_ to toIdx.
    void emitSynthetic(size_t fromIdx, size_t toIdx,
                       std::string_view replacement) {
        const Token& first = at(fromIdx);
        // last token is the one just before toIdx — could be toIdx-1
        // but walk back over trivia to find the last meaningful end
        size_t lastIdx = (toIdx > 0) ? toIdx - 1 : fromIdx;
        const Token& last = at(lastIdx);

        SourceMapEntry e;
        e.avlSpan = {
            {first.line, first.col, first.offset},
            {last.line,
             last.col  + static_cast<uint32_t>(last.text.size()),
             last.offset + static_cast<uint32_t>(last.text.size())}
        };
        e.cppSpan.begin = outPos();
        e.isSynthetic   = true;

        emitRaw(replacement);

        e.cppSpan.end = outPos();
        map_.append(e);

        pos_ = toIdx;
    }

    // Build a plain string from the text of tokens [from, to) — trivia included.
    std::string tokensText(size_t from, size_t to) const {
        std::string s;
        for (size_t i = from; i < to && i < tokens_.size(); ++i)
            s += tokens_[i].text;
        return s;
    }

    // ── Transformation 1 ─────────────────────────────────────
    // [[T, U]]  →  template<typename T, typename U>
    // [[T, int N]]  →  template<typename T, int N>   (non-type param)

    bool tryDoubleBracket() {
        assert(current().kind == TokenKind::LDoubleBracket);

        // Find matching ]]
        size_t openIdx = pos_;
        size_t closeIdx = tokens_.size();
        for (size_t i = openIdx + 1; i < tokens_.size(); ++i) {
            if (tokens_[i].kind == TokenKind::RDoubleBracket) {
                closeIdx = i;
                break;
            }
            if (tokens_[i].kind == TokenKind::Eof) return false;
        }
        if (closeIdx == tokens_.size()) return false;

        // Collect params split by top-level commas
        std::vector<std::string> params;
        std::string cur;
        for (size_t i = openIdx + 1; i < closeIdx; ++i) {
            const Token& t = tokens_[i];
            if (isTrivia(t.kind)) {
                if (!cur.empty()) cur += ' ';
                continue;
            }
            if (t.kind == TokenKind::Comma) {
                // trim trailing space
                while (!cur.empty() && cur.back() == ' ') cur.pop_back();
                if (!cur.empty()) params.push_back(cur);
                cur.clear();
            } else {
                cur += t.text;
            }
        }
        while (!cur.empty() && cur.back() == ' ') cur.pop_back();
        if (!cur.empty()) params.push_back(cur);

        // Build template<...>
        // Heuristic: a single identifier → typename parameter;
        //            two tokens (e.g. "int N") → non-type parameter (kept as-is).
        std::string replacement = "template<";
        for (size_t p = 0; p < params.size(); ++p) {
            if (p > 0) replacement += ", ";
            bool hasSpace = (params[p].find(' ') != std::string::npos);
            if (!hasSpace) replacement += "typename ";
            replacement += params[p];
        }
        replacement += '>';

        emitSynthetic(openIdx, closeIdx + 1, replacement);
        return true;
    }

    // ── Transformation 2 ─────────────────────────────────────
    // <T1, T2&, Err> func(...)  →  __avl_ret_N<T1,T2,Err> func(...)

    // Returns true when the < at pos_ looks like a multi-return type list,
    // not a comparison operator. The discriminator is structural:
    //   <type_tokens> identifier (
    bool looksLikeMultiReturnDecl() const {
        // Find the matching > (depth-aware)
        size_t gtIdx = tokens_.size();
        {
            int depth = 1;
            for (size_t i = pos_ + 1; i < tokens_.size(); ++i) {
                auto k = tokens_[i].kind;
                if      (k == TokenKind::Lt)   ++depth;
                else if (k == TokenKind::Gt)   { if (--depth == 0) { gtIdx = i; break; } }
                // These can't appear inside a return-type list
                else if (k == TokenKind::Semi  ||
                         k == TokenKind::LBrace ||
                         k == TokenKind::Eof)   return false;
            }
        }
        if (gtIdx == tokens_.size()) return false;

        // Verify every non-trivia token inside <...> is type-like
        for (size_t i = pos_ + 1; i < gtIdx; ++i) {
            if (isTrivia(tokens_[i].kind)) continue;
            switch (tokens_[i].kind) {
                case TokenKind::Identifier:
                case TokenKind::KwVoid:
                case TokenKind::Star:
                case TokenKind::Amp:
                case TokenKind::Comma:
                case TokenKind::Colon:   // std::string, etc.
                    break;
                default:
                    return false;
            }
        }

        // After > must be: Identifier (
        size_t nameIdx  = skipTrivia(gtIdx + 1);
        if (at(nameIdx).kind != TokenKind::Identifier) return false;
        size_t parenIdx = skipTrivia(nameIdx + 1);
        return at(parenIdx).kind == TokenKind::LParen;
    }

    bool tryMultiReturn() {
        assert(current().kind == TokenKind::Lt);
        if (!looksLikeMultiReturnDecl()) return false;

        size_t openIdx = pos_;

        // Find closing >
        size_t gtIdx = tokens_.size();
        {
            int depth = 1;
            for (size_t i = openIdx + 1; i < tokens_.size(); ++i) {
                if      (tokens_[i].kind == TokenKind::Lt) ++depth;
                else if (tokens_[i].kind == TokenKind::Gt) { if (--depth == 0) { gtIdx = i; break; } }
            }
        }

        // Collect return-type strings (split by top-level commas)
        std::vector<std::string> retTypes;
        {
            std::string cur;
            int depth = 0;
            for (size_t i = openIdx + 1; i < gtIdx; ++i) {
                const Token& t = tokens_[i];
                if (isTrivia(t.kind)) continue;
                if      (t.kind == TokenKind::Lt) { ++depth; cur += t.text; }
                else if (t.kind == TokenKind::Gt) { --depth; cur += t.text; }
                else if (t.kind == TokenKind::Comma && depth == 0) {
                    if (!cur.empty()) retTypes.push_back(cur);
                    cur.clear();
                } else {
                    cur += t.text;
                }
            }
            if (!cur.empty()) retTypes.push_back(cur);
        }

        // __avl_ret_N<T1,T2,...>
        std::string replacement =
            "__avl_ret_" + std::to_string(retTypes.size()) + '<';
        for (size_t r = 0; r < retTypes.size(); ++r) {
            if (r > 0) replacement += ',';
            replacement += retTypes[r];
        }
        replacement += '>';

        emitSynthetic(openIdx, gtIdx + 1, replacement);
        return true;
    }

    // ── Transformation 3 ─────────────────────────────────────
    // {x, y, void} = call(a, b);
    //   →  { auto __avl_r = call(a,b); x = __avl_r.v0; y = __avl_r.v1; }

    // The discriminator: { identifier/void [, identifier/void]* } =
    bool looksLikeDestructuring() const {
        size_t i = skipTrivia(pos_ + 1);
        if (i >= tokens_.size()) return false;

        // Must start with identifier or void
        if (at(i).kind != TokenKind::Identifier &&
            at(i).kind != TokenKind::KwVoid) return false;

        // Parse comma-separated identifier/void list
        while (i < tokens_.size()) {
            auto k = at(i).kind;
            if (k == TokenKind::Identifier || k == TokenKind::KwVoid) {
                i = skipTrivia(i + 1);
            } else {
                break;
            }
            if (at(i).kind == TokenKind::Comma) i = skipTrivia(i + 1);
            else break;
        }

        // Must land on }
        if (at(i).kind != TokenKind::RBrace) return false;

        // After } must be =
        return at(skipTrivia(i + 1)).kind == TokenKind::Assign;
    }

    bool tryDestructuring() {
        assert(current().kind == TokenKind::LBrace);
        if (!looksLikeDestructuring()) return false;

        size_t startIdx = pos_;

        // Collect target names
        std::vector<std::string> targets;
        size_t i = skipTrivia(pos_ + 1);
        while (i < tokens_.size()) {
            auto k = at(i).kind;
            if (k == TokenKind::Identifier) {
                targets.emplace_back(at(i).text);
                i = skipTrivia(i + 1);
            } else if (k == TokenKind::KwVoid) {
                targets.emplace_back("void");
                i = skipTrivia(i + 1);
            } else {
                break;
            }
            if (at(i).kind == TokenKind::Comma) i = skipTrivia(i + 1);
            else break;
        }

        // Skip } and =
        // i is at }
        size_t assignIdx = skipTrivia(i + 1);   // lands on =

        // Collect call expression until ; (depth-aware)
        size_t exprStart = assignIdx + 1;        // first token after =
        size_t semiIdx   = exprStart;
        int depth = 0;
        while (semiIdx < tokens_.size()) {
            auto k = tokens_[semiIdx].kind;
            if (k == TokenKind::LParen   ||
                k == TokenKind::LBrace   ||
                k == TokenKind::LBracket) ++depth;
            else if (k == TokenKind::RParen   ||
                     k == TokenKind::RBrace   ||
                     k == TokenKind::RBracket) --depth;
            else if (k == TokenKind::Semi && depth == 0) break;
            else if (k == TokenKind::Eof) return false;
            ++semiIdx;
        }

        size_t exprStartNonTrivia = skipTrivia(exprStart);
        std::string callExpr = tokensText(exprStartNonTrivia, semiIdx);

        // Build expansion
        std::string expansion = "{ auto __avl_r = " + callExpr + ";\n";
        for (int slot = 0; slot < (int)targets.size(); ++slot) {
            if (targets[slot] != "void")
                expansion += "  " + targets[slot] +
                             " = __avl_r.v" + std::to_string(slot) + ";\n";
        }
        expansion += '}';

        emitSynthetic(startIdx, semiIdx + 1, expansion);
        return true;
    }

    // ── Transformation 4 ─────────────────────────────────────
    // pack Vec3 { float x, y, z; };
    //   →  struct Vec3 { union { struct{float x;...}; float __data[3]; };
    //                    constexpr static std::size_t len=3; };

    bool tryPack() {
        assert(current().kind == TokenKind::KwPack);

        size_t packIdx  = pos_;
        size_t nameIdx  = skipTrivia(packIdx + 1);
        if (at(nameIdx).kind != TokenKind::Identifier) return false;
        std::string name{at(nameIdx).text};

        size_t lbraceIdx = skipTrivia(nameIdx + 1);
        if (at(lbraceIdx).kind != TokenKind::LBrace) return false;

        // The type comes right after {
        size_t typeIdx = skipTrivia(lbraceIdx + 1);
        if (at(typeIdx).kind != TokenKind::Identifier) return false;
        std::string fieldType{at(typeIdx).text};

        // Collect field names, comma-separated, terminated by ;
        std::vector<std::string> fields;
        size_t i = skipTrivia(typeIdx + 1);
        while (i < tokens_.size() && at(i).kind != TokenKind::Semi) {
            if (at(i).kind == TokenKind::Identifier)
                fields.emplace_back(at(i).text);
            // skip comma or anything else between identifiers
            i = skipTrivia(i + 1);
            if (at(i).kind == TokenKind::Comma) i = skipTrivia(i + 1);
        }
        if (at(i).kind != TokenKind::Semi) return false;
        size_t innerSemiIdx = i;

        size_t rbraceIdx = skipTrivia(innerSemiIdx + 1);
        if (at(rbraceIdx).kind != TokenKind::RBrace) return false;

        // Consume optional ; after }
        size_t endIdx = rbraceIdx + 1;
        size_t maybeOuterSemi = skipTrivia(endIdx);
        if (at(maybeOuterSemi).kind == TokenKind::Semi)
            endIdx = maybeOuterSemi + 1;

        if (fields.size() < 2) return false;   // Sema rule: min 2 fields
        size_t N = fields.size();

        // Build struct
        std::string expansion = "struct " + name + " {\n  union {\n    struct { ";
        for (const auto& f : fields)
            expansion += fieldType + ' ' + f + "; ";
        expansion += "};\n    ";
        expansion += fieldType + " __data[" + std::to_string(N) + "];\n";
        expansion += "  };\n  constexpr static std::size_t len = ";
        expansion += std::to_string(N) + ";\n}";

        emitSynthetic(packIdx, endIdx, expansion);
        return true;
    }

    // ── Transformation 5 ─────────────────────────────────────
    // #heap struct S  →  [[avl_heap]] struct S
    // #heap pack P    →  [[avl_heap]] pack P   (tryPack handles the rest)

    bool tryHeapAnnotation() {
        assert(current().kind == TokenKind::HashHeap);

        size_t nextIdx = skipTrivia(pos_ + 1);
        auto   nextK   = at(nextIdx).kind;
        if (nextK != TokenKind::KwStruct &&
            nextK != TokenKind::KwClass  &&
            nextK != TokenKind::KwPack) return false;

        // Emit [[avl_heap]] in place of #heap (and any trivia before the keyword)
        const Token& heapTok = current();
        uint32_t len = static_cast<uint32_t>(heapTok.text.size());

        SourceMapEntry e;
        e.avlSpan    = {{heapTok.line, heapTok.col, heapTok.offset},
                        {heapTok.line, heapTok.col + len, heapTok.offset + len}};
        e.cppSpan.begin = outPos();
        e.isSynthetic   = false;

        emitRaw("[[avl_heap]] ");

        e.cppSpan.end = outPos();
        map_.append(e);

        // Land on struct/class/pack — the main loop will handle it next
        pos_ = nextIdx;
        return true;
    }

    // ── Transformation 6 ─────────────────────────────────────
    // unsafe { ...  →  { __avl_unsafe_scope __avl_us; ...

    bool tryUnsafe() {
        assert(current().kind == TokenKind::KwUnsafe);

        size_t unsafeIdx = pos_;
        size_t lbraceIdx = skipTrivia(unsafeIdx + 1);
        if (at(lbraceIdx).kind != TokenKind::LBrace) return false;

        const Token& unsafeTok  = at(unsafeIdx);
        const Token& lbraceTok  = at(lbraceIdx);

        SourceMapEntry e;
        e.avlSpan = {
            {unsafeTok.line, unsafeTok.col, unsafeTok.offset},
            {lbraceTok.line,
             lbraceTok.col  + static_cast<uint32_t>(lbraceTok.text.size()),
             lbraceTok.offset + static_cast<uint32_t>(lbraceTok.text.size())}
        };
        e.cppSpan.begin = outPos();
        e.isSynthetic   = true;

        emitRaw("{ __avl_unsafe_scope __avl_us;");

        e.cppSpan.end = outPos();
        map_.append(e);

        // Skip 'unsafe', any trivia, and the '{' — body tokens follow
        pos_ = lbraceIdx + 1;
        return true;
    }

    // ── Transformation 7 ─────────────────────────────────────
    // priority  →  __avl_priority

    bool tryPriority() {
        assert(current().kind == TokenKind::KwPriority);
        const Token& tok = current();
        uint32_t len = static_cast<uint32_t>(tok.text.size());

        SourceMapEntry e;
        e.avlSpan    = {{tok.line, tok.col, tok.offset},
                        {tok.line, tok.col + len, tok.offset + len}};
        e.cppSpan.begin = outPos();
        e.isSynthetic   = false;

        emitRaw("__avl_priority");

        e.cppSpan.end = outPos();
        map_.append(e);

        ++pos_;
        return true;
    }

    // ── Transformation 8 ─────────────────────────────────────
    // raw_ptr(x)  →  __avl_raw_ptr(x)
    // into_raw(x) →  __avl_into_raw(x)

    bool tryRawIntrinsics() {
        const Token& tok = current();
        bool isRaw = (tok.kind == TokenKind::KwRawPtr);
        uint32_t len = static_cast<uint32_t>(tok.text.size());

        SourceMapEntry e;
        e.avlSpan    = {{tok.line, tok.col, tok.offset},
                        {tok.line, tok.col + len, tok.offset + len}};
        e.cppSpan.begin = outPos();
        e.isSynthetic   = false;

        emitRaw(isRaw ? "__avl_raw_ptr" : "__avl_into_raw");

        e.cppSpan.end = outPos();
        map_.append(e);

        ++pos_;
        return true;
    }
};

} // anonymous namespace

// ============================================================
// Public entry point
// ============================================================

PrePassResult runPrePass(const std::vector<Token>& tokens,
                         std::string_view           src,
                         std::string_view           filename) {
    (void)filename;   // used only for future diagnostics
    return Transformer(tokens, src).run();
}

} // namespace avl