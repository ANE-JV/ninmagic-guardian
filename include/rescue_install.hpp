#pragma once
#include <string>
#include "guardian_core.h"
namespace nmguardian {
// Confirms the three fixed embedded rescue resources. Different existing files
// are retained in numbered private generations before replacement. A sealed
// journal resumes interrupted replacements; the independent INI is promoted last.
// Call only after the user's confirmation of rescue preparation.
bool prepareRescue(gc_fs fs,const std::string&assetRoot,std::string&message);
unsigned loadOptions(gc_fs fs);
bool saveOptions(gc_fs fs,unsigned options,std::string&error);
}
