#pragma once

#include "Slic3r/Biz/ResinProfile/ResinProfileImportInteractor.hpp"

#include <string>
#include <vector>

namespace Slic3r::Biz::ResinProfile {

/**
 * @brief What one resin profile import did, or would have done, as a JSON document.
 *
 * The report is a file a person can read and a script can diff, so the output is stable: the keys
 * come in a fixed order, the results stay in the order the import produced them (a folder import is
 * sorted by the interactor already), the mapping rows are sorted by their source key, and nothing
 * varies between two runs of the same import. In particular there is no timestamp and no path: a
 * result names its file by file name only, so the same profiles imported from two folders give the
 * same report. The document is written compact, without a trailing newline, like the other JSON
 * the command line writes.
 *
 * Every file that was tried has an entry, a failed one included:
 * @code
 * {
 *   "results": [
 *     {
 *       "file": "grey.cfg",           // file name only, never the folder it was read from
 *       "ok": true,
 *       "error": "",                  // why it failed, empty when it did not
 *       "preset_name": "Grey resin",  // the user preset that was, or would be, created
 *       "base_preset": "Generic Fast Resin", // the system resin preset it inherits from
 *       "mapping": [
 *         {
 *           "key": "normalExposureTime",     // the key as it appeared in the file
 *           "target_key": "exposure_time",   // the resin preset key, empty when nothing is written
 *           "source_value": "3.5",           // the value as the file had it, before any conversion
 *           "value": "3.5",                  // the value written to it, empty when none is
 *           "status": "Exact",               // see MappingStatus, written out by to_string()
 *           "note": "Both in seconds, no conversion."
 *         }
 *       ]
 *     }
 *   ]
 * }
 * @endcode
 */
std::string resin_import_report_json(const std::vector<ResinImportResult>& results);

} // namespace Slic3r::Biz::ResinProfile
