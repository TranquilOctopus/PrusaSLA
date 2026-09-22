// Copyright (c) Prusa Research 2022 Tomáš Mészáros @tamasmeszaros
//
// PrusaSlicer is released under the terms of the AGPLv3 or higher

#ifndef ZIPPERARCHIVEIMPORT_HPP
#define ZIPPERARCHIVEIMPORT_HPP

#include <vector>
#include <string>
#include <cstdint>

#include <boost/property_tree/ptree.hpp>

#include "Slic3r/Biz/Config/Legacy/PrintConfig.hpp"

namespace Slic3r::Biz::Format::Sla {

// Bring legacy config types into Slic3r namespace for compatibility
using Slic3rLegacy::DynamicPrintConfig;
using Slic3rLegacy::ConfigSubstitutions;

// Buffer for arbitrary files inside a zipper archive.
struct EntryBuffer
{
    std::vector<uint8_t> buf;
    std::string          fname;
};

// Structure holding the data read from a zipper archive.
struct ZipperArchive
{
    boost::property_tree::ptree profile, config;
    std::vector<EntryBuffer>    entries;
};

// Names of the files containing metadata inside the archive.
const constexpr char *CONFIG_FNAME  = "config.ini";
const constexpr char *PROFILE_FNAME = "prusaslicer.ini";

// Read an archive that was written using the Zipper class.
// The includes parameter is a set of file name substrings that the entries
// must contain to be included in ZipperArchive.
// The excludes parameter may contain substrings that filenames must not
// contain.
// Every file in the archive is read into ZipperArchive::entries
// except the files CONFIG_FNAME, and PROFILE_FNAME which are read into
// ZipperArchive::config and ZipperArchive::profile structures.
ZipperArchive read_zipper_archive(const std::string &zipfname,
                                  const std::vector<std::string> &includes,
                                  const std::vector<std::string> &excludes);

// Extract the print profile form the archive into 'out'.
// Returns a profile that has correct parameters to use for model reconstruction
// even if the needed parameters were not fully found in the archive's metadata.
// The inout argument shall be a usable fallback profile if the archive
// has totally corrupted metadata.
std::pair<DynamicPrintConfig, ConfigSubstitutions> extract_profile(
    const ZipperArchive &arch, DynamicPrintConfig &inout);

} // namespace Slic3r::Biz::Format::Sla

#endif // ZIPPERARCHIVEIMPORT_HPP