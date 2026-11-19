#include "engine/version.h"

namespace sawgod {

const char *const kVersionString = SAWGOD_VERSION_STRING;
const char *const kBuildId =
#include "build_id.inc"
    ;

} // namespace sawgod
