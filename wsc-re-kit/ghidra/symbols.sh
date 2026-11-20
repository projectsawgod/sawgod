#!/bin/sh
# Usage: symbols.sh SYMBOLS PROJECT_DIRECTORY PROJECT_NAME
set -eu
: "${GHIDRA_HOME:?Set GHIDRA_HOME to the Ghidra installation directory}"
KIT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec "$GHIDRA_HOME/support/analyzeHeadless" "$2" "$3" -process -noanalysis \
  -scriptPath "$KIT/ghidra" -postScript ImportSymbols.java "$1"
