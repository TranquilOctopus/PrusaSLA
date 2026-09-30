#pragma once

#include <string>
#include "libslic3r/SLAResult.hpp"

namespace Slic3r::Biz::PrintHost::Sla {

// Chitubox .ctb, the unencrypted v2/v3 container only. The encrypted v4/v5 files are not written
// and not read: see doc/sla-fork/formats/ctb.md.
void store_ctb(const std::string& file_path, const Biz::Slicing::SLAResultData& data);

} // namespace Slic3r::Biz::PrintHost::Sla
