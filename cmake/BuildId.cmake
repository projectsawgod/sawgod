# Writes OUT (build_id.inc): this build's identity, LABEL plus the source revision when the checkout is a git
# repository (a source package without .git uses LABEL alone). Runs on every build; the file is rewritten
# only when the identity changes, so an unchanged revision recompiles nothing.
execute_process(COMMAND git -C "${SOURCE_DIR}" rev-parse --short=12 HEAD
    OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE failed)
set(id "${LABEL}")
if(NOT failed AND revision)
    set(id "${LABEL}+${revision}")
endif()
file(CONFIGURE OUTPUT "${OUT}" CONTENT "\"${id}\"\n" @ONLY)
