// The raft infill (raft_infill) cuts a pattern out of the inside of the raft instead of leaving it
// solid, the way Lychee and Chitubox can. The raft keeps a solid skin under its top face and a
// solid rim around the pattern, as thick as the material between two cells, so the object never
// rests on a hole and the raft stays printable. None is the solid slab the generator has always
// built, so it is the default.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "sla_test_utils.hpp"

#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/SLA/RaftPreset.hpp"

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

// A straight walled raft with no cavity, so the only thing that can open a hole in it is the
// infill, and the object would rest on the top of it if the infill did not leave a skin there.
sla::PadConfig infilled_raft(Domain::sla::RaftInfillType type = Domain::sla::RaftInfillType::None,
                             double                     spacing_mm = 2.,
                             double                     wall_mm    = .4,
                             double                     skin_mm    = .5)
{
    sla::PadConfig cfg;
    cfg.wall_thickness_mm = 2.;
    cfg.wall_height_mm = 0.;
    cfg.wall_slope = PI / 2;
    cfg.brim_size_mm = 1.6;
    cfg.infill.type = type;
    cfg.infill.spacing_mm = spacing_mm;
    cfg.infill.wall_mm = wall_mm;
    cfg.infill.skin_mm = skin_mm;
    return cfg;
}

// The cross section of a mesh at one height, which is what the printer sees in one layer.
ExPolygons cross_section_at(const Domain::TriangleMesh& mesh, double z)
{
    return slice_mesh_ex(mesh.its, std::vector<float>{float(z)}).front();
}

// How much material a raft has in one layer and how many holes are in it.
struct Layer {
    double area = 0.;
    size_t holes = 0;
};

Layer layer_at(const Domain::TriangleMesh& mesh, double z)
{
    Layer layer;

    for (const ExPolygon &part : cross_section_at(mesh, z)) {
        layer.area += part.area();
        layer.holes += part.holes.size();
    }

    return layer;
}

// The bounds of a mesh in mm, which is what says the raft has the same outline as before.
Domain::BoundingBox3d bounds_of(const Domain::TriangleMesh& mesh)
{
    return mesh.bounding_box();
}

} // namespace

TEST_CASE("The raft infill reaches the pad config", "[SLA][RaftInfill]")
{
    Slic3r::Domain::SLAObjectSettings settings;
    settings.overrides.set("raft_type", Slic3r::Domain::sla::RaftType::Full);

    SECTION("a config without the keys gets the solid raft it has always had")
    {
        CHECK(Slic3r::make_pad_cfg(make_config_view(settings)).infill.type ==
              Slic3r::Domain::sla::RaftInfillType::None);
    }

    SECTION("the keys are what the raft is filled with")
    {
        settings.overrides.set("raft_infill", Slic3r::Domain::sla::RaftInfillType::Honeycomb);
        settings.overrides.set("raft_infill_spacing", 3.);
        settings.overrides.set("raft_infill_wall", 0.6);
        settings.overrides.set("raft_infill_skin", 0.8);

        const sla::PadConfig cfg = Slic3r::make_pad_cfg(make_config_view(settings));
        CHECK(cfg.infill.type == Slic3r::Domain::sla::RaftInfillType::Honeycomb);
        CHECK(cfg.infill.spacing_mm == Approx(3.));
        CHECK(cfg.infill.wall_mm == Approx(0.6));
        CHECK(cfg.infill.skin_mm == Approx(0.8));
    }
}

TEST_CASE("A raft with no infill is the solid slab of today", "[SLA][RaftInfill]")
{
    PadByproducts default_cfg, none_cfg;
    test_pad("20mm_cube.obj", infilled_raft(), default_cfg);
    test_pad("20mm_cube.obj", infilled_raft(Slic3r::Domain::sla::RaftInfillType::None), none_cfg);

    // Choosing None over the default is the same mesh, so a raft without an infill is unchanged.
    REQUIRE(none_cfg.mesh.facets_count() == default_cfg.mesh.facets_count());
    CHECK(double(none_cfg.mesh.volume()) == Approx(double(default_cfg.mesh.volume())));

    // A solid slab has no hole in it anywhere.
    CHECK(layer_at(default_cfg.mesh, -1.).holes == 0);
}

TEST_CASE("A filled raft is the same outline with less resin", "[SLA][RaftInfill]")
{
    PadByproducts solid;
    test_pad("20mm_cube.obj", infilled_raft(), solid);

    const Domain::BoundingBox3d solid_bounds = bounds_of(solid.mesh);

    for (const Domain::sla::RaftInfillType type : {Domain::sla::RaftInfillType::Grid,
                                                   Domain::sla::RaftInfillType::Honeycomb}) {
        INFO("infill " << static_cast<int>(type));

        PadByproducts filled;
        test_pad("20mm_cube.obj", infilled_raft(type), filled);

        // The pattern is cut out of the inside of the raft, so the raft still stands where it did.
        const Domain::BoundingBox3d filled_bounds = bounds_of(filled.mesh);
        CHECK(filled_bounds.min.x() == Approx(solid_bounds.min.x()).margin(0.001));
        CHECK(filled_bounds.min.y() == Approx(solid_bounds.min.y()).margin(0.001));
        CHECK(filled_bounds.min.z() == Approx(solid_bounds.min.z()).margin(0.001));
        CHECK(filled_bounds.max.x() == Approx(solid_bounds.max.x()).margin(0.001));
        CHECK(filled_bounds.max.y() == Approx(solid_bounds.max.y()).margin(0.001));
        CHECK(filled_bounds.max.z() == Approx(solid_bounds.max.z()).margin(0.001));

        // The cells took resin out of it.
        CHECK(double(filled.mesh.volume()) < double(solid.mesh.volume()));

        const Layer filled_layer = layer_at(filled.mesh, -1.);
        const Layer solid_layer  = layer_at(solid.mesh, -1.);
        CHECK(filled_layer.area < solid_layer.area);
        CHECK(filled_layer.holes > 0);
    }
}

TEST_CASE("A filled raft keeps a solid skin under its top face", "[SLA][RaftInfill]")
{
    const sla::PadConfig cfg = infilled_raft(Slic3r::Domain::sla::RaftInfillType::Grid);

    PadByproducts solid, filled;
    test_pad("20mm_cube.obj", infilled_raft(), solid);
    test_pad("20mm_cube.obj", cfg, filled);

    // In the top skin, which is where the object rests on the raft, a filled raft has as much
    // material as a solid one: no hole reaches up under the object.
    const double   skin_z    = -cfg.infill.skin_mm * 0.5;
    const Layer    skin      = layer_at(filled.mesh, skin_z);
    const Layer    reference = layer_at(solid.mesh, skin_z);
    CHECK(skin.holes == 0);
    CHECK(skin.area == Approx(reference.area));

    // Between the build plate and the skin the pattern is open, which is where the resin goes.
    const double cell_z = -(cfg.full_height() + cfg.infill.skin_mm) / 2.;
    const Layer  cells  = layer_at(filled.mesh, cell_z);
    CHECK(cells.holes > 0);
    CHECK(cells.area < layer_at(solid.mesh, cell_z).area);
}