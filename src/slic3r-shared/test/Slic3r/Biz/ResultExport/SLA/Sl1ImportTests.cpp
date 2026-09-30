#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/Biz/ResinProfile/SlicedArchiveResinReader.hpp"
#include "Slic3r/Biz/SlaFixture.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SL1.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SL1Import.hpp"
#include "Slic3r/Biz/ResultExport/SLA/Zipper.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/SlaLayerHeight.hpp"
#include "Slic3r/TestUtils/TestTempDir.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <map>
#include <string>

namespace fs = boost::filesystem;

using Slic3r::Biz::PrintHost::Sla::import_sl1_archive;
using Slic3r::Biz::PrintHost::Sla::store_sl1;
using Slic3r::Biz::ResinProfile::SlicedArchiveResinReader;
using Slic3r::Domain::Vec3d;
using Catch::Approx;

// A hand made archive, for the cases a slicing run cannot produce.
static fs::path write_archive(const fs::path                                &path,
                             const std::map<std::string, std::string> &entries)
{
    Slic3r::Biz::PrintHost::Sla::Zipper zipper{path.string()};
    for (const auto &[name, data] : entries)
        zipper.add_entry(name.c_str(), data.data(), data.size());
    zipper.finalize();
    return path;
}

static const std::string PROFILE_INI = "display_width = 144\n"
                                       "display_height = 80\n"
                                       "display_pixels_x = 1440\n"
                                       "display_pixels_y = 800\n"
                                       "display_orientation = portrait\n"
                                       "layer_height = 0.5\n";

TEST_CASE("SL1 import round trip", "[import][sla][sl1]")
{
    Slic3r::Test::SlaSlicingFixture fixture;

    // One 20 mm cube.
    auto model = Slic3r::Test::generate_cubes(1, 1);
    REQUIRE(model.objects.size() == 1);
    REQUIRE(model.objects[0]->instances.size() == 1);
    // The raster covers 0..144 x 0..80 mm, so the cube at 100..120 x 50..70 is clear of both
    // centre lines: reading the layers back mirrored or transposed would move it.
    model.objects[0]->instances[0]->set_offset(Vec3d{100.0, 50.0, 0.0});

    auto config = Slic3r::Domain::ConfigPackSLA{};
    config.sla_printer_settings.items.opt("display_pixels_x").set(1440);
    config.sla_printer_settings.items.opt("display_pixels_y").set(800);
    config.sla_printer_settings.items.opt("display_width").set(144.0);
    config.sla_printer_settings.items.opt("display_height").set(80.0);
    config.sla_printer_settings.items.opt("gamma_correction").set(1.0);
    // 20 mm in 0.5 mm steps: 40 layers, coarse enough to keep the test quick.
    config.sla_print_settings.items.opt("layer_height").set(0.5);
    config.sla_material_settings.items.opt("initial_layer_height").set(0.5);
    // No supports and no raft: every layer is a plain square.
    config.sla_print_settings.items.opt("supports_enable").set(false);
    config.sla_print_settings.items.opt("pad_enable").set(false);
    config.sla_print_settings.items.opt("raft_type").set(Slic3r::Domain::sla::RaftType::None);

    auto sla_result = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result != nullptr);
    REQUIRE(sla_result->files.type == Slic3r::Biz::Slicing::Sla::FileDataType::sl1_png);
    // 20 mm of cube in 0.5 mm layers.
    const size_t layer_count = sla_result->files.data.size();
    REQUIRE(layer_count == 40u);

    Tests::TestTempDir temp_dir;
    const fs::path sl1_path = temp_dir.path() / "roundtrip.sl1";
    REQUIRE_NOTHROW(store_sl1(sl1_path.string(), *sla_result));

    auto imported = import_sl1_archive(sl1_path);
    const std::string info_msg = imported.has_value() ? std::string{} : imported.error();
    INFO(info_msg);
    REQUIRE(imported.has_value());
    REQUIRE_FALSE(imported->mesh.empty());

    // The keys of both ini files are handed back to the caller.
    REQUIRE(std::stod(imported->profile.at("display_width")) == Approx(144.));
    REQUIRE(std::stod(imported->profile.at("display_pixels_y")) == Approx(800.));
    REQUIRE(imported->profile.at("display_orientation") == "portrait");
    REQUIRE(imported->profile.count("layerHeight") == 1);  // from config.ini
    REQUIRE(imported->profile.count("printerModel") == 1);  // from config.ini

    const Slic3r::Domain::BoundingBox3d bbox = imported->mesh.bounding_box();
    REQUIRE(bbox.defined);
    CAPTURE(bbox.min(0), bbox.max(0), bbox.min(1), bbox.max(1), bbox.min(2), bbox.max(2));

    // The marching squares contours sit on the pixel grid, and the reader derives the pixel size
    // the way it always has, so the cube comes back within a couple of pixels.
    const double tol_xy = 2. * 144. / (1440 - 1);
    const double tol_z  = 0.5; // one layer

    REQUIRE(bbox.min(0) == Approx(100.).margin(tol_xy));
    REQUIRE(bbox.min(1) == Approx(50.).margin(tol_xy));
    REQUIRE(bbox.min(2) == Approx(0.).margin(tol_z));
    REQUIRE(bbox.max(0) == Approx(120.).margin(tol_xy));
    REQUIRE(bbox.max(1) == Approx(70.).margin(tol_xy));
    REQUIRE(bbox.max(2) == Approx(0.5 * layer_count).margin(tol_z));
}

TEST_CASE("SL1 archive keeps the bottom layer count", "[export][sla][sl1][resin_profile]")
{
    Slic3r::Test::SlaSlicingFixture fixture;

    // One cube, sliced coarse and without supports or a raft, so the export is quick: what
    // this case reads back is the metadata of the archive, not its layers.
    auto model = Slic3r::Test::generate_cubes(1, 1);
    REQUIRE(model.objects.size() == 1);

    auto config = Slic3r::Domain::ConfigPackSLA{};
    // 20 mm of cube in 1 mm layers.
    config.sla_print_settings.items.opt("layer_height").set(1.0);
    config.sla_material_settings.items.opt("initial_layer_height").set(1.0);
    config.sla_print_settings.items.opt("supports_enable").set(false);
    config.sla_print_settings.items.opt("pad_enable").set(false);
    config.sla_print_settings.items.opt("raft_type").set(Slic3r::Domain::sla::RaftType::None);
    // The count the bottom_* settings of the print cover, which is a setting of its own and
    // not the transition count that numFade states.
    config.sla_material_settings.items.opt("bottom_layer_count").set(6);

    auto sla_result = fixture.slice_sla_model(model, config);
    REQUIRE(sla_result != nullptr);
    REQUIRE(sla_result->files.type == Slic3r::Biz::Slicing::Sla::FileDataType::sl1_png);

    // What the print used, so the test says what the count is against.
    const int used = Slic3r::Domain::sla_bottom_layer_count(sla_result->config);
    REQUIRE(used == 6);

    Tests::TestTempDir        temp_dir;
    const fs::path            sl1_path = temp_dir.path() / "bottom.sl1";
    REQUIRE_NOTHROW(store_sl1(sl1_path.string(), *sla_result));

    const SlicedArchiveResinReader reader;
    const auto                     profile = reader.read(sl1_path);
    const std::string              failure = profile.has_value() ? "" : profile.error();
    INFO(failure);
    REQUIRE(profile.has_value());

    // The count comes back, in both spellings: the one of the embedded profile, which the
    // reader looks for first, and the fork key of the printer config, which is all a file
    // without an embedded profile has.
    REQUIRE(profile->material.bottom_layer_count == used);
    REQUIRE(profile->raw_values.at("bottom_layer_count") == std::to_string(used));
    REQUIRE(profile->raw_values.at("numBottom") == std::to_string(used));

    // The transition count is a different setting, and the reader keeps the two apart.
    REQUIRE(profile->material.faded_layer_count ==
            Slic3r::Domain::sla_effective_faded_layers(sla_result->config));

    // Every key the printer reads is still there, with the value it had.
    REQUIRE(profile->raw_values.count("expTime") == 1);
    REQUIRE(profile->raw_values.count("printerModel") == 1);
    REQUIRE(profile->raw_values.at("numFade") ==
            std::to_string(sla_result->print_statistics->count_faded_layers));
}

TEST_CASE("SL1 import reports what is wrong with the archive", "[import][sla][sl1]")
{
    Tests::TestTempDir temp_dir;

    const auto expect_error = [](const fs::path &path, const std::string &what) {
        auto imported = import_sl1_archive(path);
        REQUIRE_FALSE(imported.has_value());
        INFO(imported.error());
        REQUIRE(imported.error().find(what) != std::string::npos);
    };

    SECTION("not an archive")
    {
        const fs::path path = temp_dir.path() / "notazip.sl1";
        boost::nowide::ofstream{path.string(), std::ios::binary} << "not a zip file";
        expect_error(path, "cannot be opened");
    }

    SECTION("no config.ini")
    {
        const fs::path path = write_archive(temp_dir.path() / "noconfig.sl1",
                                            {{"prusaslicer.ini", PROFILE_INI}});
        expect_error(path, "config.ini");
    }

    SECTION("no prusaslicer.ini")
    {
        const fs::path path = write_archive(temp_dir.path() / "noprofile.sl1",
                                            {{"config.ini", "layerHeight = 0.5\n"}});
        expect_error(path, "prusaslicer.ini");
    }

    SECTION("no layers")
    {
        const fs::path path = write_archive(temp_dir.path() / "nolayers.sl1",
                                            {{"config.ini", "layerHeight = 0.5\n"},
                                             {"prusaslicer.ini", PROFILE_INI}});
        expect_error(path, "no layer images");
    }

    SECTION("no display size in the profile")
    {
        const fs::path path = write_archive(temp_dir.path() / "nodisplay.sl1",
                                            {{"config.ini", "layerHeight = 0.5\n"},
                                             {"prusaslicer.ini", "layer_height = 0.5\n"},
                                             {"job00000.png", "not a png"}});
        expect_error(path, "display_width");
    }

    SECTION("unreadable layer image")
    {
        const fs::path path = write_archive(temp_dir.path() / "badlayer.sl1",
                                            {{"config.ini", "layerHeight = 0.5\n"},
                                             {"prusaslicer.ini", PROFILE_INI},
                                             {"job00000.png", "not a png"}});
        expect_error(path, "job00000.png");
    }
}
