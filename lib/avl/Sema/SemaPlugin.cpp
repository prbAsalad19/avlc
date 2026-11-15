#include "avl/SemaPlugin.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <string>

namespace avl {
namespace {

// ── Virtual filename ─────────────────────────────────────────────────────────
// Used as the in-memory "file" that Clang opens; must match the name we pass
// to addRemappedFile and to CreateFromArgs.
static constexpr const char* kVirtualFile = "__avl__.cpp";

// ════════════════════════════════════════════════════════════════════════════
// AVL preamble
//
// Injected before the generated C++ so Clang can resolve all __avl_* names
// produced by the pre-pass.  The preamble is never shown to the user and its
// byte/line count is tracked so the diagnostic consumer can subtract it when
// remapping C++ offsets back to AVL positions.
// ════════════════════════════════════════════════════════════════════════════
static const std::string& avlPreamble() {
    // Multi-return structs up to 8 slots.  Going beyond 8 is a Sema error
    // ("too many return slots") that will be enforced by Pass 2d.
    static const std::string kP = R"cpp(
// ---- AVL compiler internal preamble (do not edit) ----
#include <cstddef>

template<typename T0>
struct __avl_ret_1 { T0 v0; };
template<typename T0, typename T1>
struct __avl_ret_2 { T0 v0; T1 v1; };
template<typename T0, typename T1, typename T2>
struct __avl_ret_3 { T0 v0; T1 v1; T2 v2; };
template<typename T0, typename T1, typename T2, typename T3>
struct __avl_ret_4 { T0 v0; T1 v1; T2 v2; T3 v3; };
template<typename T0, typename T1, typename T2, typename T3, typename T4>
struct __avl_ret_5 { T0 v0; T1 v1; T2 v2; T3 v3; T4 v4; };
template<typename T0, typename T1, typename T2,
         typename T3, typename T4, typename T5>
struct __avl_ret_6 { T0 v0; T1 v1; T2 v2; T3 v3; T4 v4; T5 v5; };
template<typename T0, typename T1, typename T2, typename T3,
         typename T4, typename T5, typename T6>
struct __avl_ret_7 { T0 v0; T1 v1; T2 v2; T3 v3; T4 v4; T5 v5; T6 v6; };
template<typename T0, typename T1, typename T2, typename T3,
         typename T4, typename T5, typename T6, typename T7>
struct __avl_ret_8 { T0 v0; T1 v1; T2 v2; T3 v3; T4 v4; T5 v5; T6 v6; T7 v7; };

// Zero-cost RAII marker emitted by the unsafe{} transformation
struct __avl_unsafe_scope {};

// priority is an annotation only; it has no C++ runtime meaning
#define __avl_priority   /* avl: priority slot annotation */

// [[avl_heap]] is an unknown C++ attribute — Clang warns, we suppress with -w
// (The pre-pass emits [[avl_heap]] on structs; the Sema plugin inspects the
//  attribute node to enforce heap-allocation rules.)

// Raw pointer intrinsics: identity functions at the C++ level.
// CodeGen will lower them to the real LLVM intrinsics in Phase 5.
template<typename T> [[nodiscard]] T* __avl_raw_ptr(T* p)  { return p; }
template<typename T>               T* __avl_into_raw(T* p) { return p; }

// ---- end AVL preamble ----
)cpp";
    return kP;
}

static uint32_t countNewlines(std::string_view s) {
    uint32_t n = 0;
    for (char c : s) if (c == '\n') ++n;
    return n;
}

// ════════════════════════════════════════════════════════════════════════════
// AVLDiagConsumer
//
// Intercepts every Clang diagnostic, maps the C++ byte offset back to the
// originating AVL position via the source map, and re-emits in the standard
// Clang format:
//
//   file.avl:line:col: level: message
//
// Diagnostics whose offset falls inside the preamble are internal avlc
// artifacts and are silently suppressed (errors printed as "internal").
// ════════════════════════════════════════════════════════════════════════════
class AVLDiagConsumer : public clang::DiagnosticConsumer {
public:
    AVLDiagConsumer(const SourceMap& map,
                    std::string_view avlFile,
                    uint32_t         preByte,   // size of preamble in bytes
                    uint32_t         preLine)   // newline count in preamble
        : map_(map), avlFile_(avlFile), preByte_(preByte), preLine_(preLine) {}

    void HandleDiagnostic(clang::DiagnosticsEngine::Level lvl,
                          const clang::Diagnostic& info) override {
        // Let the base class update NumWarnings / NumErrors
        clang::DiagnosticConsumer::HandleDiagnostic(lvl, info);

        if      (lvl >= clang::DiagnosticsEngine::Error)   ++errors_;
        else if (lvl == clang::DiagnosticsEngine::Warning)  ++warns_;

        const char* tag;
        switch (lvl) {
            case clang::DiagnosticsEngine::Note:    tag = "note";        break;
            case clang::DiagnosticsEngine::Warning: tag = "warning";     break;
            case clang::DiagnosticsEngine::Error:   tag = "error";       break;
            case clang::DiagnosticsEngine::Fatal:   tag = "fatal error"; break;
            default: return;   // Ignored / Remark — not interesting yet
        }

        // Format the message text
        llvm::SmallString<256> msg;
        info.FormatDiagnostic(msg);

        // Diagnostics with no location (e.g. "too many errors")
        if (!info.getLocation().isValid() || !info.hasSourceManager()) {
            llvm::errs() << avlFile_ << ": " << tag << ": " << msg << '\n';
            return;
        }

        const clang::SourceManager& sm = info.getSourceManager();
        // Use the expansion location to see through macro substitutions
        clang::SourceLocation loc = sm.getExpansionLoc(info.getLocation());
        uint32_t off = sm.getFileOffset(loc);

        // ── 1. Inside the preamble — avlc internal ───────────────────────
        if (off < preByte_) {
            if (lvl >= clang::DiagnosticsEngine::Error)
                llvm::errs() << "avlc: internal (preamble): " << msg << '\n';
            return;
        }

        uint32_t genOff = off - preByte_;  // offset into the generated C++

        // ── 2. Source map hit — precise AVL position ─────────────────────
        if (auto avlPos = map_.avlPosFromCppOffset(genOff)) {
            llvm::errs() << avlFile_
                         << ':' << avlPos->line << ':' << avlPos->col
                         << ": " << tag << ": " << msg << '\n';
            return;
        }

        // ── 3. Source map miss — subtract preamble lines (best-effort) ───
        // This covers generated code that has no direct AVL counterpart,
        // e.g. a __avl_r temporary in a destructuring expansion.
        clang::PresumedLoc pl = sm.getPresumedLoc(loc);
        if (!pl.isInvalid()) {
            uint32_t genLine = (pl.getLine() > preLine_)
                             ?  pl.getLine() - preLine_ : 1u;
            llvm::errs() << avlFile_
                         << ':' << genLine << ':' << pl.getColumn()
                         << ": " << tag << ": " << msg << '\n';
        } else {
            llvm::errs() << avlFile_ << ": " << tag << ": " << msg << '\n';
        }
    }

    int errors() const { return errors_; }
    int warns()  const { return warns_;  }

private:
    const SourceMap& map_;
    std::string_view avlFile_;
    uint32_t         preByte_;
    uint32_t         preLine_;
    int              errors_ = 0;
    int              warns_  = 0;
};

// ════════════════════════════════════════════════════════════════════════════
// AVLASTConsumer
//
// Receives the Clang AST for the generated C++ and dispatches to the
// language-specific semantic sub-passes.  All are stubs in Phase 2a;
// each will be filled in its respective sub-phase (2b–2g).
// ════════════════════════════════════════════════════════════════════════════
class AVLASTConsumer : public clang::ASTConsumer {
public:
    AVLASTConsumer(clang::CompilerInstance& ci,
                   const SourceMap&         map,
                   std::string_view         avlFile)
        : ci_(ci), map_(map), avlFile_(avlFile) {}

    void HandleTranslationUnit(clang::ASTContext& ctx) override {
        (void)ctx;
        // ── 2b  OwnershipVisitor::run(ctx, ci_, map_, avlFile_);
        // ── 2c  CycleDetector::run(ctx, ci_, map_, avlFile_);
        // ── 2d  MultiReturnVerifier::run(ctx, ci_, map_, avlFile_);
        // ── 2e  PackValidator::run(ctx, ci_, map_, avlFile_);
        // ── 2f  UnsafeBoundaryEnforcer::run(ctx, ci_, map_, avlFile_);
        // ── 2g  TemplateConstraintChecker::run(ctx, ci_, map_, avlFile_);
    }

private:
    clang::CompilerInstance& ci_;
    const SourceMap&         map_;
    std::string_view         avlFile_;
};

// ════════════════════════════════════════════════════════════════════════════
// AVLFrontendAction
//
// Subclasses SyntaxOnlyAction to hook our AVLASTConsumer into Clang's
// pipeline while letting Clang handle all the preprocessor / parser work.
// ════════════════════════════════════════════════════════════════════════════
class AVLFrontendAction : public clang::SyntaxOnlyAction {
public:
    AVLFrontendAction(const SourceMap& map, std::string_view avlFile)
        : map_(map), avlFile_(avlFile) {}

    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance& ci,
                      llvm::StringRef          /*file*/) override {
        return std::make_unique<AVLASTConsumer>(ci, map_, avlFile_);
    }

private:
    const SourceMap& map_;
    std::string_view avlFile_;
};

} // anonymous namespace

// ════════════════════════════════════════════════════════════════════════════
// runSema — public entry point
// ════════════════════════════════════════════════════════════════════════════
SemaResult runSema(const PrePassResult& prePass,
                   std::string_view     avlFilename) {
    // ── Preamble metrics ────────────────────────────────────────────────────
    const std::string& pre = avlPreamble();
    const uint32_t preByte = static_cast<uint32_t>(pre.size());
    const uint32_t preLine = countNewlines(pre);

    // Full source: preamble (resolves __avl_* names) + generated C++
    std::string fullSrc = pre + prePass.cppSource;

    // ── Diagnostic consumer ─────────────────────────────────────────────────
    AVLDiagConsumer consumer(prePass.sourceMap, avlFilename, preByte, preLine);

    clang::CompilerInstance CI;

    // ── Build CompilerInvocation ─────────────────────────────────────────────
    // We use a temporary IgnoringDiagConsumer for the arg-parse step because
    // our args are hardcoded constants; any parse failure here is a compiler
    // bug, not user error.
    {
        llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> ids(new clang::DiagnosticIDs);
        clang::DiagnosticOptions parseOpts;
        clang::DiagnosticsEngine parseDE(ids, parseOpts,
            new clang::IgnoringDiagConsumer, /*ShouldOwnClient=*/true);

        auto inv = std::make_shared<clang::CompilerInvocation>();
        const char* args[] = {
            "-x", "c++",
            "-std=c++23",
            "-fsyntax-only",
            "-w",           // suppress Clang style/pedantic warnings;
                            // avlc reports its own diagnostics via AVLDiagConsumer
            kVirtualFile,
        };
        clang::CompilerInvocation::CreateFromArgs(
            *inv, llvm::ArrayRef(args), parseDE);

        // Some Clang snapshots do not provide CompilerInstance::setInvocation().
        // Copy the parsed invocation state directly onto the instance instead.
        CI.getFrontendOpts() = inv->getFrontendOpts();
        CI.getLangOpts() = inv->getLangOpts();
        CI.getTargetOpts() = inv->getTargetOpts();
        CI.getPreprocessorOpts() = inv->getPreprocessorOpts();
        CI.getHeaderSearchOpts() = inv->getHeaderSearchOpts();
        CI.getDiagnosticOpts() = inv->getDiagnosticOpts();
    }

    // ── Install our diagnostic consumer ─────────────────────────────────────
    // Must follow setInvocation so createDiagnostics can read DiagnosticOptions
    // from the invocation.
    CI.createDiagnostics(&consumer, /*ShouldOwnClient=*/false);

    // ── File / source managers ───────────────────────────────────────────────
    CI.createFileManager();
    CI.createSourceManager();

    // ── Remap virtual filename to in-memory buffer ───────────────────────────
    // addRemappedFile takes ownership of the MemoryBuffer.
    CI.getPreprocessorOpts().addRemappedFile(
        kVirtualFile,
        llvm::MemoryBuffer::getMemBufferCopy(fullSrc, kVirtualFile).release());

    // ── Execute ──────────────────────────────────────────────────────────────
    // ExecuteAction drives BeginSourceFile → createPreprocessor → createTarget
    // → parser → ASTConsumer::HandleTranslationUnit → EndSourceFile.
    AVLFrontendAction action(prePass.sourceMap, avlFilename);
    CI.ExecuteAction(action);

    return {consumer.errors() > 0, consumer.errors(), consumer.warns()};
}

} // namespace avl