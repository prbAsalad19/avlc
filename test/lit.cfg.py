import lit.formats
import os

config.name = "avlc"
config.test_format = lit.formats.ShTest(False)
config.suffixes = [".avl"]

# Percorso di questa cfg — sempre disponibile
test_src = os.path.dirname(os.path.abspath(__file__))

# avlc_bin: preferisci il param passato da CMake, fallback relativo
avlc_bin = lit_config.params.get(
    "avlc_bin",
    os.path.join(test_src, "..", "build", "bin", "avlc")
)

filecheck_bin = lit_config.params.get(
    "filecheck_bin",
    "/usr/bin/FileCheck"
)

config.substitutions.append(("%avlc",      avlc_bin))
config.substitutions.append(("%FileCheck", filecheck_bin))

config.excludes = ["basic.avl"]

config.test_source_root = test_src
config.test_exec_root   = test_src