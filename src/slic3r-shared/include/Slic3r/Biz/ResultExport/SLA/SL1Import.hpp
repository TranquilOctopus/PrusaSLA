#pragma once

#include "Slic3r/Domain/TriangleMesh.hpp"
#include "tl/expected.hpp"

#include <functional>
#include <map>
#include <string>

#include <boost/filesystem/path.hpp>

namespace Slic3r::Biz::PrintHost::Sla {

struct Sl1ImportResult {
    Domain::TriangleMesh mesh;
    // The keys of the archive's ini files, as they were read: config.ini of the
    // printer and prusaslicer.ini of the slicing profile, merged into one map with
    // prusaslicer.ini winning on a shared key. Values are the raw ini strings.
    std::map<std::string, std::string> profile;
};

/// Read a Prusa .sl1 / .sl1s archive and reconstruct the printed model from the
/// layer images it contains. This is the counterpart of store_sl1().
/// Fails with a message describing the problem if the file is not an SL1 archive,
/// if it has no config.ini, no layer images, or a layer image cannot be decoded.
/// Cancelling through stop() is not an error: the returned mesh is then empty.
/// @p progress, when given, is called with the share of the work done from 0 to 1, so a caller
/// running this off the main thread can show a bar. Decoding the layer images is two thirds of
/// it, tracing them into a mesh the rest.
tl::expected<Sl1ImportResult, std::string>
import_sl1_archive(const boost::filesystem::path &path,
                   std::function<bool()>            stop     = {},
                   std::function<void(double)>      progress = {});

} // namespace Slic3r::Biz::PrintHost::Sla
