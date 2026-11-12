import lit.formats
import os

config.name = "avlc"
config.test_format = lit.formats.ShTest(True)
config.suffixes = [".avl"]

# Trova avlc nel build tree
# llvm-lit viene invocato come: llvm-lit test/ dal build dir
avlc_bin = os.path.join(config.test_exec_root, "..", "bin", "avlc")
config.substitutions.append(("%avlc", avlc_bin))

# FileCheck è fornito da LLVM
config.substitutions.append(("%filecheck", "FileCheck"))

config.test_exec_root = os.path.dirname(__file__)