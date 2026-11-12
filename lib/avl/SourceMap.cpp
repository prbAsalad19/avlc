#include "avl/ema.h"

#include <algorithm>
#include <cstdio>

namespace avl {

// ============================================================
// SourceMap::avlPosFromCppOffset
// ============================================================
// Entries are sorted by cppSpan.begin.offset ascending.
// We binary-search for the last entry whose begin <= cppOffset,
// then verify that cppOffset < end.

std::optional<SourcePos>
SourceMap::avlPosFromCppOffset(uint32_t cppOffset) const noexcept {
    if (entries_.empty()) return std::nullopt;

    // Upper-bound on begin.offset, then step back one.
    auto it = std::upper_bound(
        entries_.begin(), entries_.end(), cppOffset,
        [](uint32_t off, const SourceMapEntry& e) {
            return off < e.cppSpan.begin.offset;
        });

    if (it == entries_.begin()) return std::nullopt;
    --it;

    const SourceMapEntry& e = *it;
    if (cppOffset >= e.cppSpan.end.offset) return std::nullopt;

    // For synthetic (multi-line) entries we return the AVL begin position.
    // For 1:1 replacements we can compute the column offset precisely.
    if (e.isSynthetic) {
        return e.avlSpan.begin;
    }

    uint32_t delta   = cppOffset - e.cppSpan.begin.offset;
    uint32_t avlOff  = e.avlSpan.begin.offset + delta;
    uint32_t avlCol  = e.avlSpan.begin.col    + delta;
    return SourcePos{e.avlSpan.begin.line, avlCol, avlOff};
}

// ============================================================
// SourceMap::cppPosFromAvlPos
// ============================================================
// Linear scan — this path is only used by the LSP (go-to-def),
// not in the hot diagnostic path, so O(n) is acceptable.

std::optional<SourcePos>
SourceMap::cppPosFromAvlPos(SourcePos avlPos) const noexcept {
    for (const auto& e : entries_) {
        if (avlPos.offset >= e.avlSpan.begin.offset &&
            avlPos.offset <  e.avlSpan.end.offset) {
            if (e.isSynthetic) return e.cppSpan.begin;
            uint32_t delta  = avlPos.offset - e.avlSpan.begin.offset;
            uint32_t cppOff = e.cppSpan.begin.offset + delta;
            uint32_t cppCol = e.cppSpan.begin.col    + delta;
            return SourcePos{e.cppSpan.begin.line, cppCol, cppOff};
        }
    }
    return std::nullopt;
}

// ============================================================
// SourceMap::dump
// ============================================================

void SourceMap::dump(std::string_view avlFilename,
                     std::string_view cppFilename) const {
    std::printf("Source map: %.*s → %.*s  (%zu entries)\n",
        static_cast<int>(avlFilename.size()), avlFilename.data(),
        static_cast<int>(cppFilename.size()), cppFilename.data(),
        entries_.size());

    for (const auto& e : entries_) {
        std::printf("  avl %u:%u–%u:%u  →  cpp %u:%u–%u:%u%s\n",
            e.avlSpan.begin.line, e.avlSpan.begin.col,
            e.avlSpan.end.line,   e.avlSpan.end.col,
            e.cppSpan.begin.line, e.cppSpan.begin.col,
            e.cppSpan.end.line,   e.cppSpan.end.col,
            e.isSynthetic ? "  [synthetic]" : "");
    }
}

} // namespace avl