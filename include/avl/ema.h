#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>
#include <string>

namespace avl {

// ============================================================
// Source positions
// ============================================================

/// A position in a source file — both AVL and C++ sides use this.
struct SourcePos {
    uint32_t line;    // 1-based
    uint32_t col;     // 1-based
    uint32_t offset;  // 0-based byte offset from start of buffer

    bool operator==(const SourcePos&) const noexcept = default;
};

/// A contiguous span in a source file.
struct SourceSpan {
    SourcePos begin;
    SourcePos end;   // exclusive (points one past the last character)

    [[nodiscard]] bool isEmpty() const noexcept {
        return begin.offset == end.offset;
    }
};

// ============================================================
// Source map entry
// ============================================================

/// One entry records the correspondence between a range in the
/// generated C++ buffer and the originating span in the AVL source.
///
/// A single AVL token (or multi-token construct) can expand into
/// multiple C++ tokens, so entries may overlap on the AVL side.
///
/// Layout (by example):
///
///   AVL:  {x, y, void} = call(a, b);          ← one line, one entry begins
///   C++:  { auto __r = call(a,b);              ← cppSpan.begin
///             x = __r.v0;
///             y = __r.v1; }                    ← cppSpan.end
struct SourceMapEntry {
    SourceSpan avlSpan;   // position in the original .avl file
    SourceSpan cppSpan;   // position in the generated C++ buffer

    /// True when this entry was produced by a syntactic expansion
    /// (multi-line), false for 1:1 token replacements.
    bool isSynthetic = false;
};

// ============================================================
// Source map
// ============================================================

/// The complete source map for one translation unit.
/// Entries are stored in ascending order of cppSpan.begin.offset.
class SourceMap {
public:
    SourceMap() = default;

    // ── Building ─────────────────────────────────────────────

    /// Append an entry. Caller must maintain ascending cpp-offset order.
    void append(SourceMapEntry entry) {
        entries_.push_back(std::move(entry));
    }

    /// Convenience: record a 1-to-1 token replacement
    /// (same logical position, different text).
    void appendTokenReplacement(SourcePos avlPos,
                                SourcePos cppPos,
                                uint32_t  avlLen,
                                uint32_t  cppLen) {
        SourceMapEntry e;
        e.avlSpan  = {avlPos,  {avlPos.line,  avlPos.col  + avlLen,  avlPos.offset  + avlLen}};
        e.cppSpan  = {cppPos,  {cppPos.line,  cppPos.col  + cppLen,  cppPos.offset  + cppLen}};
        e.isSynthetic = false;
        entries_.push_back(e);
    }

    // ── Querying ─────────────────────────────────────────────

    /// Given a byte offset in the generated C++ buffer, return the
    /// corresponding AVL SourcePos, if any entry covers that offset.
    [[nodiscard]]
    std::optional<SourcePos> avlPosFromCppOffset(uint32_t cppOffset) const noexcept;

    /// Given an AVL SourcePos, return the first C++ SourcePos that
    /// corresponds to it (useful for IDE go-to-def in the reverse direction).
    [[nodiscard]]
    std::optional<SourcePos> cppPosFromAvlPos(SourcePos avlPos) const noexcept;

    // ── Accessors ────────────────────────────────────────────

    [[nodiscard]] const std::vector<SourceMapEntry>& entries() const noexcept {
        return entries_;
    }

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] size_t size() const noexcept { return entries_.size(); }

    /// Dump a human-readable representation — used by --dump-sourcemap.
    void dump(std::string_view avlFilename,
              std::string_view cppFilename) const;

private:
    std::vector<SourceMapEntry> entries_;
};

// ============================================================
// PrePassResult — output of the pre-pass
// ============================================================

/// Everything the pre-pass produces for one translation unit.
struct PrePassResult {
    std::string cppSource;   // the full generated C++ buffer (owned)
    SourceMap   sourceMap;   // AVL ↔ C++ correspondences
};

} // namespace avl