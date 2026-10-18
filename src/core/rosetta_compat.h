#pragma once
#include <sys/types.h>

namespace Core::Rosetta {

// Returns true if the process is running under Rosetta 2 translation.
bool IsRosetta();

// Applies conservative memory limits when Rosetta is detected.
// Returns false if the current build cannot run under Rosetta.
bool ApplyCompatLayout();

} // namespace Core::Rosetta