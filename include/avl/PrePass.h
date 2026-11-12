#pragma once

#include "avl/ema.h"
#include "avl/Lexer.h"

#include <string_view>
#include <vector>

namespace avl {

/// Run the token-level AVL pre-pass.
///
/// \p tokens   Token stream from tokenize(src) — must include trivia tokens.
/// \p src      Original AVL source buffer (tokens hold views into it).
/// \p filename Used only in diagnostic messages.
///
/// Returns the generated C++ source and the AVL↔C++ source map.
[[nodiscard]]
PrePassResult runPrePass(const std::vector<Token>& tokens,
                         std::string_view           src,
                         std::string_view           filename = "<input>");

} // namespace avl