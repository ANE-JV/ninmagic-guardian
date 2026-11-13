#pragma once
#include "guardian_core.h"
#include <string>

namespace nmguardian {
// Keep this object alive while its callbacks are in use. root is "sdmc:" on
// Switch or an absolute fixture directory on the host; it is never a NAND mount.
struct NativeFs {
    std::string root;
    gc_fs callbacks();
};
struct Runtime {
    bool atmosphere=false,identityValid=false;
    std::string firmware,model,identity,detail;
};
Runtime inspectRuntime();
}
