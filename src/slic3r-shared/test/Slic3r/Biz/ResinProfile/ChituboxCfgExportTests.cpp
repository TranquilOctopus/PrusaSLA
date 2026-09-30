#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/ResinProfile/ChituboxCfgExport.hpp"
#include "Slic3r/Biz/ResinProfile/ChituboxCfgReader.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileImportTestFixture.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"

#include <boost/algorithm/string/trim.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Biz::ResinProfile;

namespace Slic3r::Test {

namespace {

/// A resin preset box with the SLA defaults in it, which is what the export reads.
Domain::ConfigItems resin_items()
{
    Domain::ConfigPackSLA pack;
    return pack.sla_material_settings.items;
}

/// A resin preset that carries every setting of the two mapping tables, so that an export of it
/// has a value for every row.
Domain::ConfigItems filled_resin_items()
{
    Domain::ConfigItems items = resin_items();
    items.opt("exposure_time").set(2.5);
    items.opt("initial_exposure_time").set(30.);
    items.opt("resin_faded_layers").set(8);
    items.opt("bottom_layer_count").set(6);
    items.opt("resin_layer_height").set(0.05);
    items.opt("material_density").set(1.12);
    items.opt("delay_before_exposure").set(std::vector<double>{1., 1.});
    items.opt("delay_after_exposure").set(std::vector<double>{0.5, 0.5});
    items.opt("wait_before_lift").set(2.);
    items.opt("wait_after_lift").set(1.);
    items.opt("lift_height").set(5.);
    items.opt("lift_height_2").set(3.);
    items.opt("bottom_lift_height").set(4.);
    items.opt("bottom_lift_height_2").set(2.);
    items.opt("lift_speed").set(2.5);
    items.opt("lift_speed_2").set(1.5);
    items.opt("bottom_lift_speed").set(1.);
    items.opt("bottom_lift_speed_2").set(0.5);
    items.opt("retract_speed").set(5.);
    items.opt("retract_speed_2").set(3.);
    items.opt("light_pwm").set(255);
    items.opt("bottom_light_pwm").set(128);
    items.opt("bottle_cost").set(25.);
    items.opt("bottle_volume").set(500.);
    items.opt("material_vendor").set(std::string{"Generic"});
    return items;
}

/// The value the mapper wrote for one resin key, read back into a resin preset box. The mapper
/// writes the text the config itself writes into an .ini file, so it is one number or a pair of
/// them, and the type of the option says which of the two it is.
void set_from_text(Domain::ConfigItem& item, const std::string& text)
{
    if (item.holds_alternative<int>()) {
        item.set(std::stoi(text));
        return;
    }
    if (item.holds_alternative<double>()) {
        item.set(std::stod(text));
        return;
    }
    REQUIRE(item.holds_alternative<std::vector<double>>());
    std::vector<double> values;
    std::istringstream stream{text};
    std::string part;
    while (std::getline(stream, part, ','))
        values.push_back(std::stod(part));
    item.set(values);
}

/// The resin preset an import would have saved: the values of its mapping, and nothing else.
Domain::ConfigItems resin_items_of(const MappingResult& mapping)
{
    Domain::ConfigItems items = resin_items();
    for (const auto& [key, value] : mapping.material_values)
        set_from_text(items.opt(key), value);
    return items;
}

/// The keys of an exported file, read the way the reader of that format reads them: one
/// "key: value" per line, a '#' line is a comment.
std::map<std::string, std::string> parse_cfg(const std::string& text)
{
    std::map<std::string, std::string> keys;
    std::istringstream stream{text};
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        const size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        keys[line.substr(0, colon)] = boost::trim_copy(line.substr(colon + 1));
    }
    return keys;
}

/// The value of one key of an exported file, empty when the file does not have that key. The
/// export never writes an empty value, so an empty result means the key is not there.
std::string value_of(const std::map<std::string, std::string>& keys, const std::string& key)
{
    const auto it = keys.find(key);
    return it == keys.end() ? std::string{} : it->second;
}

const ExportedResinKey* find_row(const ChituboxCfgExport& result, const std::string& material_key)
{
    for (const ExportedResinKey& row : result.keys)
        if (row.material_key == material_key)
            return &row;
    return nullptr;
}

bool was_skipped(const ChituboxCfgExport& result, const std::string& material_key)
{
    return std::ranges::find(result.skipped, material_key) != result.skipped.end();
}

/// The same number, however the two texts happen to be written. The round trip goes through a
/// text form, so the comparison is on the value and not on the digits.
void check_same_number(const std::string& exported, const std::string& original)
{
    const double exported_value  = std::stod(exported);
    const double original_value = std::stod(original);
    const double tolerance      = 1e-6 * std::max(1., std::fabs(original_value));
    INFO("exported " << exported << ", read from the file as " << original);
    CHECK(std::fabs(exported_value - original_value) <= tolerance);
}

/**
 * @brief The round trip the export is for: a hand-written .cfg the reader reads, mapped onto a
 * resin preset, exported again, and read back. Every value the import wrote comes back with the
 * number the file gave it, and a setting Chitubox has no key for is one the report names.
 * @param expected_keys How many values of the file the export carries back.
 */
void check_round_trip(
    ResinImportFixture& fx,
    const fs::path& original_path,
    const Domain::ConfigItems& material,
    const MappingResult& mapping,
    TargetPrinterClass printer_class,
    int expected_keys
)
{
    const ChituboxCfgExport exported =
        export_chitubox_cfg_report(material, printer_class, "Round trip resin");
    const ChituboxCfgReader reader;

    // A file the registry would pick this reader for, not a text file that only looks like one.
    REQUIRE(reader.sniff(exported.text));

    const fs::path exported_path = fx.write_profile("round_trip_exported.cfg", exported.text);
    const auto reread   = reader.read(exported_path);
    const auto original = reader.read(original_path);
    REQUIRE(reread.has_value());
    REQUIRE(original.has_value());
    // Everything the export wrote is a line the reader understands, so it has nothing to warn
    // about: no key with a value it cannot read, no line that is neither.
    CHECK(reread->warnings.empty());

    // The name of the profile is a key of its own and comes back as it went in.
    const auto original_name = original->raw_values.find("currProfile");
    if (original_name != original->raw_values.end()) {
        const auto reread_name = reread->raw_values.find("currProfile");
        REQUIRE(reread_name != reread->raw_values.end());
        CHECK(reread_name->second == original_name->second);
    }

    const std::set<std::string> left_out(exported.skipped.begin(), exported.skipped.end());
    int round_tripped = 0;
    for (const MappedField& row : mapping.report) {
        // A source key the import did not write is not expected in the file: it belongs to the
        // other printer class, or it is a preview setting, or its value was not a number.
        if (row.target_key.empty() || row.value.empty())
            continue;

        INFO("Chitubox key " << row.source_key);
        const auto written_back = reread->raw_values.find(row.source_key);
        if (left_out.count(row.target_key) == 1) {
            // A resin setting with no Chitubox key. The report says the export left it out, so it
            // is not in the file, and the value is not lost: the report of the import still has it.
            CHECK(written_back == reread->raw_values.end());
            continue;
        }

        REQUIRE(written_back != reread->raw_values.end());
        const auto original_value = original->raw_values.find(row.source_key);
        REQUIRE(original_value != original->raw_values.end());
        check_same_number(written_back->second, original_value->second);
        ++round_tripped;
    }
    // Every value of the file the export can carry is carried: the test says how many there were,
    // so a key that quietly stopped being exported fails here.
    CHECK(round_tripped == expected_keys);
}

/// A .cfg for a tilt printer, written by hand: the keys a machine that tilts has, and none of the
/// lift keys a printer that lifts the build plate has.
const std::string TILT_CFG =
    "currProfile: Tilt resin\n"
    "normalExposureTime: 2.5\n"
    "bottomLayerExposureTime: 30\n"
    "transitionLayers: 8\n"
    "layerHeight: 0.05\n"
    "resinDensity: 1.12\n"
    "lightOffTime: 1\n"
    "resetTimeBeforeLift: 0.5\n";

} // anonymous namespace

TEST_CASE("ChituboxCfgExport - a resin preset becomes the keys the reader reads", "[resin_profile][export]")
{
    const ChituboxCfgExport exported =
        export_chitubox_cfg_report(filled_resin_items(), TargetPrinterClass::GenericMsla, "Grey resin");
    const std::map<std::string, std::string> keys = parse_cfg(exported.text);

    CHECK(value_of(keys, "currProfile") == "Grey resin");
    CHECK(value_of(keys, "normalExposureTime") == "2.5");
    CHECK(value_of(keys, "bottomLayerExposureTime") == "30");
    CHECK(value_of(keys, "bottomLayerCount") == "6");
    CHECK(value_of(keys, "transitionLayers") == "8");
    CHECK(value_of(keys, "layerHeight") == "0.05");
    CHECK(value_of(keys, "resinDensity") == "1.12");
    CHECK(value_of(keys, "lightOffTime") == "1");
    // The waits of a printer that lifts the build plate are the reset times of that format.
    CHECK(value_of(keys, "resetTimeBeforeLift") == "2");
    CHECK(value_of(keys, "resetTimeAfterLift") == "1");
    CHECK(value_of(keys, "normalLayerLiftHeight") == "5");
    CHECK(value_of(keys, "normalLayerLiftHeight2") == "3");
    CHECK(value_of(keys, "bottomLayerLiftHeight") == "4");
    CHECK(value_of(keys, "bottomLayerLiftHeight2") == "2");
    CHECK(value_of(keys, "normalLightIntensityPWM") == "255");
    CHECK(value_of(keys, "bottomLightIntensityPWM") == "128");
    // The price of a bottle and the bottle it is for become a price per litre and its unit.
    CHECK(value_of(keys, "resinPrice") == "50");
    CHECK(value_of(keys, "resinUnit") == "L");
    CHECK(value_of(keys, "bottleVolume") == "500");
    // A delay after the exposure is where a tilt printer keeps the wait before a lift, so a
    // printer that lifts has no use for it and the file does not carry it.
    CHECK(was_skipped(exported, "delay_after_exposure"));
}

TEST_CASE("ChituboxCfgExport - a price of a bottle becomes a price per litre", "[resin_profile][export]")
{
    SECTION("the cost of the bottle the preset names")
    {
        const std::map<std::string, std::string> keys =
            parse_cfg(export_chitubox_cfg(filled_resin_items(), TargetPrinterClass::GenericMsla));

        // 25 for a 500 ml bottle is 50 per litre, and a price without the unit it is per is not a
        // price the import would read back, so both keys are written.
        CHECK(value_of(keys, "resinPrice") == "50");
        CHECK(value_of(keys, "resinUnit") == "L");
        CHECK(value_of(keys, "bottleVolume") == "500");

        const ChituboxCfgExport exported =
            export_chitubox_cfg_report(filled_resin_items(), TargetPrinterClass::GenericMsla);
        // The row is a written row, not one of the settings left out, and it says what it was made
        // of, so a report reader can check the arithmetic.
        CHECK_FALSE(was_skipped(exported, "bottle_cost"));
        CHECK_FALSE(was_skipped(exported, "bottle_volume"));
        const ExportedResinKey* price = find_row(exported, "bottle_cost");
        REQUIRE(price != nullptr);
        CHECK(price->chitubox_key == "resinPrice");
        CHECK(price->value == "50");
        CHECK(price->unit_key == "resinUnit");
        CHECK(price->unit_value == "L");
        CHECK(price->status == MappingStatus::Converted);
        CHECK(price->note.find("500") != std::string::npos);
        const ExportedResinKey* volume = find_row(exported, "bottle_volume");
        REQUIRE(volume != nullptr);
        CHECK(volume->chitubox_key == "bottleVolume");
        CHECK(volume->value == "500");
    }

    SECTION("a cost of zero is the absence of a price and is not written")
    {
        Domain::ConfigItems items = filled_resin_items();
        items.opt("bottle_cost").set(0.);
        const ChituboxCfgExport exported = export_chitubox_cfg_report(items, TargetPrinterClass::GenericMsla);
        const std::map<std::string, std::string> keys = parse_cfg(exported.text);

        CHECK(keys.count("resinPrice") == 0);
        CHECK(keys.count("resinUnit") == 0);
        const ExportedResinKey* price = find_row(exported, "bottle_cost");
        REQUIRE(price != nullptr);
        CHECK(price->chitubox_key.empty());
        CHECK(price->value.empty());
        CHECK(price->note.find("not used") != std::string::npos);
    }

    SECTION("a bottle of no size is priced for the usual 1 litre bottle, and says so")
    {
        Domain::ConfigItems items = filled_resin_items();
        items.opt("bottle_volume").set(0.);
        const ChituboxCfgExport exported = export_chitubox_cfg_report(items, TargetPrinterClass::GenericMsla);
        const std::map<std::string, std::string> keys = parse_cfg(exported.text);

        // Dividing by a bottle of nothing would be a price of infinity, so the same bottle the
        // import assumes is used here and the row names it.
        CHECK(value_of(keys, "resinPrice") == "25");
        const ExportedResinKey* price = find_row(exported, "bottle_cost");
        REQUIRE(price != nullptr);
        CHECK(price->note.find("1000") != std::string::npos);
    }

    SECTION("a resin with no price of its own writes neither the price nor the unit")
    {
        Domain::ConfigItems items = resin_items();
        const ChituboxCfgExport exported = export_chitubox_cfg_report(items, TargetPrinterClass::GenericMsla);
        const std::map<std::string, std::string> keys = parse_cfg(exported.text);

        CHECK(keys.count("resinPrice") == 0);
        CHECK(keys.count("resinUnit") == 0);
    }
}

TEST_CASE("ChituboxCfgExport - a price per litre survives the import and back out", "[resin_profile][export]")
{
    ResinImportFixture fx;

    // A file of the shape the export writes: a price per litre, the unit it is per and the bottle
    // the price was made for. The import turns the first two into a bottle cost, and the volume
    // stays a resin setting of its own, so the export can take them apart again.
    const fs::path profile = fx.write_profile(
        "price.cfg",
        "currProfile: Priced resin\n"
        "normalExposureTime: 2.5\n"
        "resinPrice: 40\n"
        "resinUnit: /l\n"
        "bottleVolume: 500\n"
    );
    ResinProfileImportInteractor interactor(fx.project_interactor);
    const ResinImportResult       result = interactor.import_file(profile, fx.target(), /*dry_run=*/true);
    REQUIRE(result.ok);

    const MappingResult& mapping = result.mapping;
    // 40 per litre of a 500 ml bottle is 20 for the bottle.
    CHECK(mapping.material_values.at("bottle_cost") == "20");
    CHECK(mapping.material_values.at("bottle_volume") == "500");

    const ChituboxCfgExport exported =
        export_chitubox_cfg_report(resin_items_of(mapping), TargetPrinterClass::GenericMsla, "Priced resin");
    const std::map<std::string, std::string> keys = parse_cfg(exported.text);

    // 20 for a 500 ml bottle is the 40 per litre the file stated.
    CHECK(value_of(keys, "resinPrice") == "40");
    CHECK(value_of(keys, "resinUnit") == "L");
    CHECK(value_of(keys, "bottleVolume") == "500");
    // And the unit is one the import reads as a litre, so the file really does come back in.
    const ChituboxCfgReader reader;
    const auto             reread = reader.read(fx.write_profile("price_exported.cfg", exported.text));
    REQUIRE(reread.has_value());
    const MappingResult again = map_resin_profile(*reread, TargetPrinterClass::GenericMsla);
    check_same_number(again.material_values.at("bottle_cost"), mapping.material_values.at("bottle_cost"));
}

TEST_CASE("ChituboxCfgExport - the speeds are converted back to mm/min", "[resin_profile][export]")
{
    const std::map<std::string, std::string> keys =
        parse_cfg(export_chitubox_cfg(filled_resin_items(), TargetPrinterClass::GenericMsla));

    // The resin preset holds mm/s, the .cfg is assumed mm/min, which is the conversion the import
    // undoes, so the trip is there and back again.
    CHECK(value_of(keys, "normalLayerLiftSpeed") == "150");
    CHECK(value_of(keys, "normalLayerLiftSpeed2") == "90");
    CHECK(value_of(keys, "bottomLayerLiftSpeed") == "60");
    CHECK(value_of(keys, "bottomLayerLiftSpeed2") == "30");
    CHECK(value_of(keys, "normalDropSpeed") == "300");
    CHECK(value_of(keys, "normalDropSpeed2") == "180");
}

TEST_CASE("ChituboxCfgExport - a tilt printer gets the tilt mapping", "[resin_profile][export]")
{
    const ChituboxCfgExport exported = export_chitubox_cfg_report(filled_resin_items(), TargetPrinterClass::Tilt);
    const std::map<std::string, std::string> keys = parse_cfg(exported.text);

    CHECK(value_of(keys, "normalExposureTime") == "2.5");
    CHECK(value_of(keys, "transitionLayers") == "8");
    // A tilt printer has no block of bottom layers: it fades the exposure over the transition
    // layers instead, which is where the setting the import wrote them into goes back.
    CHECK(keys.count("bottomLayerCount") == 0);
    // And nothing to lift the build plate with, so none of the motion of that format.
    CHECK(keys.count("normalLayerLiftHeight") == 0);
    CHECK(keys.count("normalLayerLiftSpeed") == 0);
    CHECK(keys.count("normalDropSpeed") == 0);
    CHECK(keys.count("normalLightIntensityPWM") == 0);
    // The wait before a lift of such a machine is a delay after the exposure, so that is the key
    // it is written to, and there is no wait after a lift to state.
    CHECK(value_of(keys, "resetTimeBeforeLift") == "0.5");
    CHECK(keys.count("resetTimeAfterLift") == 0);
}

TEST_CASE("ChituboxCfgExport - a setting with no Chitubox key is left out and named", "[resin_profile][export]")
{
    const ChituboxCfgExport exported = export_chitubox_cfg_report(filled_resin_items(), TargetPrinterClass::GenericMsla);
    const std::map<std::string, std::string> keys = parse_cfg(exported.text);

    // The vendor of the resin and the tilt of the machine are resin settings that a .cfg has no key
    // for, so they are named instead of being written under a key that means something else.
    CHECK(was_skipped(exported, "material_vendor"));
    CHECK(was_skipped(exported, "use_tilt"));
    CHECK(keys.count("machineName") == 0);
    // A setting the file can carry is never reported as left out.
    CHECK_FALSE(was_skipped(exported, "exposure_time"));
    CHECK_FALSE(was_skipped(exported, "lift_height"));

    const ExportedResinKey* vendor = find_row(exported, "material_vendor");
    REQUIRE(vendor != nullptr);
    CHECK(vendor->status == MappingStatus::NotApplicable);
    CHECK(vendor->chitubox_key.empty());
    CHECK(vendor->value.empty());
    // The report says why there is nothing to write, so the setting is not lost without a word.
    CHECK(vendor->note.find("vendor") != std::string::npos);

    // Every key of the table has a row, and a row says in one way or the other what became of it.
    int written = 0;
    for (const ExportedResinKey& row : exported.keys) {
        INFO(row.material_key);
        const bool wrote = !row.chitubox_key.empty();
        CHECK(wrote == !row.value.empty());
        // A row that writes a value and its unit writes both, and a row that writes nothing has
        // no unit either: the unit of a price is what makes it a price.
        CHECK(wrote == !row.unit_key.empty());
        CHECK(row.unit_key.empty() == row.unit_value.empty());
        if (wrote)
            ++written;
    }
    CHECK(written == static_cast<int>(exported.keys.size() - exported.skipped.size()));
}

TEST_CASE("ChituboxCfgExport - a zero is the absence of a setting and is not written", "[resin_profile][export]")
{
    Domain::ConfigItems items = filled_resin_items();
    items.opt("lift_speed").set(0.);
    items.opt("light_pwm").set(0);
    const ChituboxCfgExport exported = export_chitubox_cfg_report(items, TargetPrinterClass::GenericMsla);
    const std::map<std::string, std::string> keys = parse_cfg(exported.text);

    // Zero is not a speed or a level in this config, it is the setting not being used, and a .cfg
    // key of zero would be a value the other slicer would print with.
    CHECK(keys.count("normalLayerLiftSpeed") == 0);
    CHECK(keys.count("normalLightIntensityPWM") == 0);
    const ExportedResinKey* speed = find_row(exported, "lift_speed");
    REQUIRE(speed != nullptr);
    CHECK(speed->chitubox_key.empty());
    CHECK(speed->note.find("not used") != std::string::npos);
}

TEST_CASE(
    "ChituboxCfgExport - a [below, above] delay pair writes one value and says which",
    "[resin_profile][export]"
)
{
    Domain::ConfigItems items = filled_resin_items();
    items.opt("delay_before_exposure").set(std::vector<double>{1., 2.});
    const ChituboxCfgExport exported = export_chitubox_cfg_report(items, TargetPrinterClass::GenericMsla);
    const std::map<std::string, std::string> keys = parse_cfg(exported.text);

    // A .cfg states one time for the whole layer, so the one below the area fill threshold is
    // written and the other one is named in the report instead of being dropped.
    CHECK(value_of(keys, "lightOffTime") == "1");
    const ExportedResinKey* delay = find_row(exported, "delay_before_exposure");
    REQUIRE(delay != nullptr);
    CHECK(delay->value == "1");
    CHECK(delay->note.find("2") != std::string::npos);
}

TEST_CASE("ChituboxCfgExport - a preset name cannot break the line it is written on", "[resin_profile][export]")
{
    const std::map<std::string, std::string> keys = parse_cfg(
        export_chitubox_cfg(filled_resin_items(), TargetPrinterClass::Tilt, "Grey: resin\nlayerHeight: 99")
    );

    // A colon or a line break in the name would end the line early and the rest of the name
    // would be read as a key of its own.
    const std::string name = value_of(keys, "currProfile");
    CHECK(name.find("Grey") != std::string::npos);
    CHECK(name.find(':') == std::string::npos);
    CHECK(name.find('\n') == std::string::npos);
    CHECK(value_of(keys, "layerHeight") == "0.05");
}

TEST_CASE("export_printer_class - the model decides, use_tilt is the hint", "[resin_profile][export]")
{
    SECTION("a resin that turns use_tilt off belongs to a printer that lifts")
    {
        Domain::ConfigItems items = resin_items();
        items.opt("use_tilt").set(std::vector<bool>{false, false});
        CHECK(export_printer_class(items, "") == TargetPrinterClass::GenericMsla);
    }

    SECTION("a printer that names no model is decided by use_tilt")
    {
        Domain::ConfigItems items = resin_items();
        items.opt("use_tilt").set(std::vector<bool>{true, true});
        CHECK(export_printer_class(items, "") == TargetPrinterClass::Tilt);
    }

    SECTION("the model wins, as it does for the import")
    {
        Domain::ConfigItems items = resin_items();
        items.opt("use_tilt").set(std::vector<bool>{false, false});
        CHECK(export_printer_class(items, "Prusa SL1S") == TargetPrinterClass::Tilt);
        CHECK(export_printer_class(items, "Photon Mono M5") == TargetPrinterClass::GenericMsla);
    }
}

TEST_CASE(
    "ChituboxCfgExport - a .cfg keeps its values through the import and back out",
    "[resin_profile][export]"
)
{
    ResinImportFixture fx;

    SECTION("a profile of a printer that lifts the build plate")
    {
        const fs::path profile = fx.write_profile("round_trip.cfg", chitubox_cfg("Round trip resin", "8"));
        ResinProfileImportInteractor interactor(fx.project_interactor);
        // A dry run is the mapping without the save, which is the same set of values the import
        // would have written into the preset.
        const ResinImportResult result =
            interactor.import_file(profile, fx.target(), /*dry_run=*/true);
        REQUIRE(result.ok);
        REQUIRE_FALSE(result.mapping.material_values.empty());

        const Domain::ConfigItems material = resin_items_of(result.mapping);
        // The printer of the fixture is a Z-lift machine, and the preset says so by leaving
        // use_tilt out, so the export goes through the table of that class.
        const TargetPrinterClass printer_class = export_printer_class(material, "Photon Mono M5");
        CHECK(printer_class == TargetPrinterClass::GenericMsla);
        check_round_trip(fx, profile, material, result.mapping, printer_class, /*expected_keys=*/5);
    }

    SECTION("a profile of a tilt printer")
    {
        const fs::path profile = fx.write_profile("round_trip_tilt.cfg", TILT_CFG);
        const ChituboxCfgReader reader;
        const auto read = reader.read(profile);
        REQUIRE(read.has_value());

        // The same mapping the import would do for a machine of that class, on a profile that came
        // from a file rather than from a preset, so the export is checked on both.
        const MappingResult mapping = map_resin_profile(*read, TargetPrinterClass::Tilt);
        REQUIRE_FALSE(mapping.material_values.empty());
        check_round_trip(
            fx,
            profile,
            resin_items_of(mapping),
            mapping,
            TargetPrinterClass::Tilt,
            /*expected_keys=*/7
        );
    }
}

} // namespace Slic3r::Test
