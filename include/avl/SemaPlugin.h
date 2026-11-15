#pragma once

#include "avl/ema.h"
#include <string_view>

namespace avl {

struct SemaResult {
    bool hasErrors  = false;
    int  errorCount = 0;
    int  warnCount  = 0;
};

/// Run the Clang-backed AVL semantic plugin on the pre-processed C++ source.
///
/// A CompilerInstance is spun up in-memory from \p prePass.cppSource
/// (prepended with the AVL preamble that declares __avl_* names).
/// All diagnostics emitted by Clang are intercepted by AVLDiagConsumer,
/// which maps C++ byte-offsets back to .avl positions via the source map,
/// then prints in Clang-compatible format:
///
///   file.avl:line:col: error: message
///
/// \p prePass     Output of runPrePass() for the same translation unit.
/// \p avlFilename Filename used in diagnostic output (the original .avl path).
[[nodiscard]]
SemaResult runSema(const PrePassResult& prePass,
                   std::string_view     avlFilename);

} // namespace avl