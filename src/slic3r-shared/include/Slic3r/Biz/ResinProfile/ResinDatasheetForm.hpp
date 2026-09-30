#pragma once

#include "Slic3r/Biz/ResinProfile/ForeignResinProfile.hpp"

#include <string>

namespace Slic3r::Biz::ResinProfile {

/**
 * @brief What a resin datasheet states, as typed into the "New resin from datasheet" form (M3.11).
 *
 * A vendor datasheet is a table of numbers, not a file: this is what the user types off it. The six
 * settings nearly every datasheet gives are here, in the units the datasheets use (mm, seconds,
 * seconds per layer). The rest are optional, and are asked for because the SLA material settings
 * have a key for each of them: the light-off delay, the price and size of a bottle, and the layer
 * separation of a printer that lifts the build plate - a lift distance, a lift speed and a retract
 * speed - plus the transition layer count a tilt printer fades the exposure over. A field left
 * empty is a setting the datasheet does not state, exactly like a key a reader does not find, so
 * the mapper is left to decide what to do about it.
 *
 * The speeds are in mm/min, the unit the .cfg of a foreign slicer states them in and the unit the
 * datasheets of a generic MSLA printer use, so the mapper converts them the same way it converts
 * the ones read from a file. The transition layer count is a count of layers, not a measurement.
 *
 * The values are the text of the fields, unvalidated: validation is validate_datasheet(), which is
 * a pure function of what is typed, so a field is never parsed twice by two different rules.
 */
struct ResinDatasheet
{
    /// The name of the resin, e.g. "Zero 2 Deep Aqua". Required: it becomes the preset name.
    std::string resin_name;
    /// The vendor the datasheet is from, e.g. "Anycubic". Optional, and shown in the review only.
    std::string vendor;
    /// Layer height in mm. Required.
    std::string layer_height_mm;
    /// Exposure of a normal layer in seconds. Required.
    std::string normal_exposure_s;
    /// Exposure of the bottom layers in seconds. Required.
    std::string bottom_exposure_s;
    /// How many bottom layers the datasheet calls for. Required, and a whole number.
    std::string bottom_layer_count;
    /// Light-off delay in seconds. Optional: a datasheet that states none leaves it empty.
    std::string light_off_delay_s;
    /// Price of one bottle, in the currency the datasheet is written in. Optional.
    std::string price_per_bottle;
    /// How many ml one bottle holds. Optional; without it a 1 litre bottle is assumed, the same
    /// assumption the mapper makes for a profile that states no bottle size.
    std::string bottle_volume_ml;
    /// How far the build plate is lifted to separate a layer, in mm. Optional.
    std::string lift_distance_mm;
    /// How fast it is lifted, in mm/min. Optional.
    std::string lift_speed_mm_min;
    /// How fast it drops back into the resin, in mm/min. Optional.
    std::string retract_speed_mm_min;
    /// Over how many layers the exposure is faded, a whole number. Optional; a tilt printer has no
    /// block of bottom layers and fades instead.
    std::string transition_layer_count;
};

/**
 * @brief Why the form cannot be turned into a resin profile, empty when it can.
 *
 * A number that is missing or not a number is refused, a name that is blank is refused because it
 * is what the preset is saved under, a count has to be a whole number, and a value outside the
 * <min, max> its own resin setting declares is refused with a message that names the field and the
 * limit, so a datasheet is never written and shown in the review table as something the setting
 * does not take. A limit is read from the option the value is written to, in the unit that option
 * holds, so a speed asked in mm/min is compared as the mm/s it becomes. The messages are user
 * visible and already translated.
 *
 * Pure: it reads the form and the option definitions and nothing else, so what the form accepts can
 * be tested without a window, and the dialog shows exactly what the test checked.
 */
std::string validate_datasheet(const ResinDatasheet& datasheet);

/**
 * @brief The foreign resin profile the form describes, as a reader would have handed one over.
 *
 * The values are written under the keys of the mapping table of the M3.6 mapper, which is what
 * makes the rest of the import the same as for a file: the review dialog, the base picker, the
 * mapping table and the save are the code of ResinProfileImportInteractor and are not repeated
 * here. A field left empty is left out of the profile, so it has no row in the mapping table,
 * rather than being written as a zero.
 *
 * The format id is SOURCE_FORMAT_DATASHEET, so the source note of the saved preset says the values
 * were typed in from a datasheet instead of naming a file that does not exist.
 *
 * Pure, and it does not validate: a value that is not a number is left out rather than turned into
 * one, and the caller is expected to have asked validate_datasheet() first.
 */
ForeignResinProfile datasheet_to_profile(const ResinDatasheet& datasheet);

} // namespace Slic3r::Biz::ResinProfile
