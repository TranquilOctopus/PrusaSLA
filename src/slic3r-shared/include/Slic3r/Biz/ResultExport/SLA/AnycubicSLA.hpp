#pragma once

#include <string>
#include "libslic3r/SLAResult.hpp"

namespace Slic3r::Biz::PrintHost::Sla {

// The Photon Workshop container of the Photon Mono M5 family. One writer serves all three: the
// machine name and the format version are the only fields known to differ, the rest of the
// container and every resolution come from the printer profile. doc/sla-fork/formats/pm5.md has the
// layout, and the pm5s and pm7 sections there say which of the fields are unverified.
enum class PmWorkshopFormat
{
    pm5,  // Anycubic Photon Mono M5, the one variant a real file has been read from
    pm5s, // Anycubic Photon Mono M5s, unverified
    pm7,  // Anycubic Photon Mono M7 Pro, unverified
};

void store_anycubic(const std::string& file_path, const Biz::Slicing::SLAResultData& data);
void store_pm_workshop(const std::string& file_path, const Biz::Slicing::SLAResultData& data, PmWorkshopFormat format);

} // namespace Slic3r::Biz::PrintHost::Sla