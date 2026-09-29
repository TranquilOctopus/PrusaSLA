#pragma once

#include <string>
#include "Slic3r/Domain/Project.hpp"
#include "libslic3r/SlicingStatus.hpp"

namespace Slic3r::App {
// is_sla picks the SLA wording ('build plate') instead of the FFF one ('bed')
std::string to_display_string(Biz::Slicing::ErrorCode code, bool is_sla = false);
std::string to_display_string(
    Biz::Slicing::Error error,
    const Domain::Project& project,
    bool is_sla = false
);
std::string to_display_string(
    Biz::Slicing::Warning warning,
    const Domain::Project& project,
    bool is_sla = false
);
std::string to_display_string(Biz::Slicing::ProgressInfo info);
} // namespace Slic3r::App
