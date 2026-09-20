#pragma once

#include <string>
#include "libslic3r/SLAResult.hpp"

namespace Slic3r::Biz::PrintHost::Sla {

void store_goo(const std::string& file_path, const Biz::Slicing::SLAResultData& data);

} // namespace Slic3r::Biz::PrintHost::Sla