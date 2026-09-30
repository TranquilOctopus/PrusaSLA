// The raft floor thickness (raft_floor_thickness) is the slab on the build plate the raft stands
// on, which Lychee and Chitubox let the user set apart from the thickness of the raft walls. A
// thicker floor makes the raft stand higher on the plate without its outline, its walls or the
// height the object sits at moving. Zero asks for no floor of its own, which is a floor as thick
// as the wall and therefore the raft the generator has always built.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstddef>
#include <memory>
#include <vector>

#include "sla_test_utils.hpp"

#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/SLA/RaftPreset.hpp"
#include "Slic3r/Domain/SlaLayerHeight.hpp"

#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

namespace {

// A config view over the given object settings, the way the engine builds one for a print object.
Slic3r::SLAPrintObjectConfigView make_config_view(Slic3r::Domain::SLAObjectSettings& settings)
{
    Slic3r::Domain::FullConfigSLAPtr full{std::make_shared<const Slic3r::Domain::FullConfigSLA>(
        Slic3r::Domain::FullConfigSLA::defaults())};
    Slic3r::Domain::PartialObjectConfigSLAPtr object{
        std::make_shared<const Slic3r::Domain::PartialObjectConfigSLA>(settings, full->hw_config())};
    return Slic3r::SLAPrintObjectConfigView{full, object};
}

// A raft with a 2 mm wall and a 1 mm cavity the object sits in, standing on the floor thickness it
// is given. The walls are straight, so the raft is a prism: its outline, its top face and the
// height of its cavity are all easy to compare between two floor thicknesses.
sla::PadConfig raft_with_cavity(double floor_thickness_mm)
{
    sla::PadConfig cfg;
    cfg.wall_thickness_mm  = 2.;
    cfg.floor_thickness_mm = floor_thickness_mm;
    cfg.wall_height_mm     = 1.;
    cfg.wall_slope         = PI / 2;
    cfg.brim_size_mm       = 1.6;
    return cfg;
}

// How tall a raft is, which is what its top face height above the build plate is.
double height_of(const Domain::TriangleMesh& mesh)
{
    const Domain::BoundingBox3d bb = mesh.bounding_box();
    return bb.max.z() - bb.min.z();
}

// How much resin a raft has in one layer, which is what the printer sees in it.
double area_at(const Domain::TriangleMesh& mesh, double z)
{
    double area = 0.;

    for (const ExPolygon& part : slice_mesh_ex(mesh.its, std::vector<float>{float(z)}).front())
        area += part.area();

    return area;
}

// The layers of a print config, the way the exporters and the layer statistics read them.
Slic3r::Domain::ConfigView make_view(const Slic3r::Domain::ConfigPackSLA& config_pack)
{
    const Slic3r::Domain::Preset::HwPrinterConfig hw_config{
        .technology = Slic3r::Domain::PrinterTechnology::SLA};
    auto full_config = std::make_shared<const Slic3r::Domain::FullConfigSLA>(config_pack, hw_config);
    Slic3r::Domain::ConfigView config_view{full_config, {}};
    // Until finalize() runs, values() is empty and every lookup misses.
    config_view.finalize();
    return config_view;
}

} // namespace

TEST_CASE("The raft floor thickness reaches the pad config", "[SLA][RaftFloorThickness]")
{
    Slic3r::Domain::SLAObjectSettings settings;
    settings.overrides.set("raft_type", Slic3r::Domain::sla::RaftType::Full);
    settings.overrides.set("pad_wall_thickness", 1.5);

    SECTION("a config without the key gets a floor as thick as the wall")
    {
        CHECK(Slic3r::make_pad_cfg(make_config_view(settings)).floor_thickness_mm == Approx(0.));
    }

    SECTION("the key is what the floor of the raft is built with")
    {
        settings.overrides.set("raft_floor_thickness", 3.);

        const sla::PadConfig cfg = Slic3r::make_pad_cfg(make_config_view(settings));
        CHECK(cfg.floor_thickness_mm == Approx(3.));
        // The wall keeps the thickness the user gave it: the floor is a knob of its own.
        CHECK(cfg.wall_thickness_mm == Approx(1.5));
    }
}

TEST_CASE("A raft floor thickness of zero is the raft of today", "[SLA][RaftFloorThickness]")
{
    PadByproducts no_floor, floor_as_the_wall;
    test_pad("20mm_cube.obj", raft_with_cavity(0.), no_floor);
    test_pad("20mm_cube.obj", raft_with_cavity(2.), floor_as_the_wall);

    // Zero asks for no floor of its own, so the raft is built with a floor as thick as its walls,
    // which is the only raft the generator had before the setting existed.
    REQUIRE(no_floor.mesh.its.size() == floor_as_the_wall.mesh.its.size());
    CHECK(double(no_floor.mesh.volume()) == Approx(double(floor_as_the_wall.mesh.volume())));

    // A 1 mm cavity on a 2 mm floor, so the raft is 3 mm tall as it was.
    CHECK(height_of(no_floor.mesh) == Approx(3.));
}

TEST_CASE("A thicker raft floor is a taller raft with the same outline", "[SLA][RaftFloorThickness]")
{
    const double extra_floor_mm = 2.;

    PadByproducts today, thicker;
    test_pad("20mm_cube.obj", raft_with_cavity(0.), today);
    test_pad("20mm_cube.obj", raft_with_cavity(2. + extra_floor_mm), thicker);

    // The raft stands on the build plate, so a floor that is thicker by this much lifts the top
    // face of the raft by exactly this much.
    CHECK(height_of(thicker.mesh) == Approx(height_of(today.mesh) + extra_floor_mm));

    // The raft did not spread: the outline on the plate and the top face are where they were.
    const Domain::BoundingBox3d today_bb   = today.mesh.bounding_box();
    const Domain::BoundingBox3d thicker_bb = thicker.mesh.bounding_box();
    CHECK(thicker_bb.min.x() == Approx(today_bb.min.x()).margin(0.001));
    CHECK(thicker_bb.max.x() == Approx(today_bb.max.x()).margin(0.001));
    CHECK(thicker_bb.min.y() == Approx(today_bb.min.y()).margin(0.001));
    CHECK(thicker_bb.max.y() == Approx(today_bb.max.y()).margin(0.001));

    // The walls kept their thickness and the cavity the object sits in its height: a layer through
    // the wall has as much resin as before, and so has a layer through the floor.
    CHECK(area_at(thicker.mesh, -0.5) == Approx(area_at(today.mesh, -0.5)));
    CHECK(area_at(thicker.mesh, -2.5) == Approx(area_at(today.mesh, -2.5)));

    // The extra floor took resin: the taller raft is the bigger one.
    CHECK(double(thicker.mesh.volume()) > double(today.mesh.volume()));
}

TEST_CASE("The raft interface band follows the top of a thicker raft", "[SLA][RaftFloorThickness]")
{
    using Slic3r::Domain::sla_raft_interface;

    // A raft with a 2 mm wall and a 1 mm cavity, a 1.5 mm interface and 0.05 mm layers, with the
    // exposure faded over no layers, so the first layer alone is a bottom layer.
    const auto band_with_floor = [](double floor_thickness_mm) {
        Slic3r::Domain::ConfigPackSLA config_pack;
        config_pack.sla_print_settings.items.opt("pad_wall_thickness").set(2.);
        config_pack.sla_print_settings.items.opt("pad_wall_height").set(1.);
        config_pack.sla_print_settings.items.opt("raft_floor_thickness").set(floor_thickness_mm);
        config_pack.sla_print_settings.items.opt("raft_interface_thickness").set(1.5);
        config_pack.sla_print_settings.items.opt("layer_height").set(0.05);
        config_pack.sla_print_settings.items.opt("faded_layers").set(0);
        return sla_raft_interface(make_view(config_pack), 200);
    };

    // 3 mm of raft at 0.05 mm is 60 layers and the top 30 of them are the interface.
    const Slic3r::Domain::RaftInterface today = band_with_floor(0.);
    REQUIRE(today.count() == 30);
    REQUIRE(today.first_layer == 30);
    REQUIRE(today.last_layer == 59);

    // A 4 mm floor instead of a 2 mm one is a 5 mm raft, so the band is the same 30 layers, 40 of
    // them higher up: the object rests on the top of a taller raft.
    const Slic3r::Domain::RaftInterface thicker = band_with_floor(4.);
    REQUIRE(thicker.count() == 30);
    CHECK(thicker.first_layer == today.first_layer + 40);
    CHECK(thicker.last_layer == today.last_layer + 40);
}
