#!/bin/sh
# Usage: import.sh ROM PROJECT_DIRECTORY PROJECT_NAME
set -eu
: "${GHIDRA_HOME:?Set GHIDRA_HOME to the Ghidra installation directory}"
KIT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$2"
exec "$GHIDRA_HOME/support/analyzeHeadless" "$2" "$3" -import "$1" -overwrite \
  -noanalysis -loader BinaryLoader -loader-length 0x100 -processor 'x86:LE:16:Real Mode' \
  -scriptPath "$KIT/ghidra" -preScript WsMemoryMap.java "$1"
