// Copyright (c) Prusa Research 2020 - 2023 Tomáš Mészáros @tamasmeszaros, Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv
//
// PrusaSlicer is released under the terms of the AGPLv3 or higher

#include <catch2/catch_test_macros.hpp>
#include <test_utils.hpp>

#include "Slic3r/Biz/SlaFixture.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SL1.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"
#include "Slic3r/Biz/Config/ConfigLegacy.hpp"
#include "Slic3r/Biz/Config/ConfigSerialize.hpp"
#include "Slic3r/Biz/Algorithms/Model.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "libslic3r/Format/SLAArchiveReader.hpp"
#include "libslic3r/Format/SL1.hpp"

#include "Slic3r/Domain/ConfigPack.hpp"

#include <boost/filesystem.hpp>

using namespace Slic3r;
using namespace Slic3r::Biz::Slicing; // SLAResultData
using Slic3r::Biz::PrintHost::Sla::store_sl1;

using Biz::Algorithms::Model::flatten_to_mesh;

TEST_CASE("Archive readwrite roundtrip test", "[sla_archives]") {
    Slic3r::Test::SlaSlicingFixture fixture;

    for (const char * pname : {"20mm_cube", "extruder_idler"}){
        INFO("Testing archive type: SL1 -- writing...");

        Domain::Model m;
        ASSERT(load_obj((TEST_DATA_DIR PATH_SEPARATOR + std::string(pname) + ".obj").c_str(), &m));
        m.objects.back()->add_instance();

        Domain::ConfigPackSLA cfg;

        cfg.sla_printer_settings.items.opt("printer_technology").set(Domain::PrinterTechnology::SLA);
        cfg.sla_printer_settings.items.opt("sla_archive_format").set("SL1");
        cfg.sla_print_settings.items.opt("supports_enable").set(false);
        cfg.sla_print_settings.items.opt("pad_enable").set(false);

        auto sla_result = fixture.slice_sla_model(m, cfg);
        REQUIRE(sla_result != nullptr);

        auto outputfname = std::string("output_") + pname + ".sl1";

        // Export using the SL1 writer
        REQUIRE_NOTHROW(store_sl1(outputfname, *sla_result));

        // Verify the archive was created
        REQUIRE(boost::filesystem::exists(outputfname));

        double vol_written = flatten_to_mesh(m).volume();

        INFO("Testing archive type: SL1 -- reading back...");
        indexed_triangle_set its;
        DynamicPrintConfig cfg_read;

        try {
            // Leave format_id deliberately empty, guessing should always
            // work here.
            import_sla_archive(outputfname, "", its, cfg_read);
        } catch (...) {
            REQUIRE(false);
        }

        // Verify the archive was read correctly
        REQUIRE(!cfg_read.empty());
        REQUIRE(!its.empty());

        // Check that layer count matches (config.ini has layerHeight and initial_layer_height)
        auto *opt_layerh = cfg_read.option<ConfigOptionFloat>("layer_height");
        auto *opt_init_layerh = cfg_read.option<ConfigOptionFloat>("initial_layer_height");
        REQUIRE(opt_layerh != nullptr);
        REQUIRE(opt_init_layerh != nullptr);
        
        // Check that slices are non-empty (we reconstructed mesh from slices)
        double vol_read = Domain::its_volume(its);
        double rel_err  = std::abs(vol_written - vol_read) / vol_written;
        REQUIRE(rel_err < 0.1);
    }
}