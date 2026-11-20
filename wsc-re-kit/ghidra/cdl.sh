#!/bin/sh
# Usage: cdl.sh ROM CDL PROJECT_DIRECTORY PROJECT_NAME
set -eu
: "${GHIDRA_HOME:?Set GHIDRA_HOME to the Ghidra installation directory}"
KIT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$3"
NORMAL=$(mktemp "$3/cdl.XXXXXX")
trap 'rm -f "$NORMAL"' EXIT HUP INT TERM
"${PYTHON:-python3}" "$KIT/tools/cdl.py" "$1" "$2" --out "$NORMAL"
"$GHIDRA_HOME/support/analyzeHeadless" "$3" "$4" -process -noanalysis \
  -scriptPath "$KIT/ghidra" -postScript ApplyCdl.java "$NORMAL"
