// M7.8.4, rulebook R6: raft_type Auto is the default and builds no raft, unless the underside of
// the part would form a suction cup in the layers it prints first. These are the shapes the rule is
// about (a solid block, a cup standing open side down, a hollow sole, a part held above the plate)
// and the two paths that have to agree on it: the slice of a print and the tree of the support
// preview.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/CGAL/Algorithms/MeshBoolean.hpp"
#include "Slic3r/Biz/Slicing/BackgroundProcess.hpp"
#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"
#include "Slic3r/Domain/SlicingId.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/TestUtils/HwConfigUtils.hpp"
#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/IThumbnailImageGenerator.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/SLASupportTool.hpp"
#include "libslic3r/SLA/RaftAuto.hpp"
#include "libslic3r/SLA/CavityDetection.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

namespace {

namespace MeshBoolean = Slic3r::Biz::CGAL::Algorithms::MeshBoolean;

using Slic3r::Domain::sla::RaftType;

// The mesh type below is `indexed_triangle_set`, unqualified on purpose: admesh/stl.h declares it
// in the global namespace, and Slic3r::Domain only carries the coloured variant
// (Slic3r/Domain/TriangleMesh.hpp). It is written the way libslic3r/SLA/Pad.hpp writes it.

// The layer height of a real SLA print, so the millimetre of underside the shapes below put their
// pocket in is twenty layers of it.
constexpr double layer_height_mm = 0.05;

// A box centred on the origin in x and y, its bottom face at z0.
indexed_triangle_set box(double x_mm, double y_mm, double z_mm, double z0 = 0.)
{
    indexed_triangle_set its = Slic3r::Biz::Algorithms::TriangleMesh::its_make_cube(x_mm, y_mm, z_mm);
    for (Slic3r::Domain::Vec3f& vertex : its.vertices) {
        vertex += Slic3r::Domain::Vec3f(0.f, 0.f, float(z0));
    }
    return its;
}

// A block with a pocket closed above it between the two heights: a hollow sole, a recess of a solid,
// anything that would seal a pocket against the plate. Its underside is solid up to
// pocket_roof_mm, hollow up to pocket_floor_mm and solid above that.
indexed_triangle_set
block_with_pocket(double x_mm, double y_mm, double z_mm, double wall_mm, double pocket_roof_mm,
                  double pocket_floor_mm)
{
    indexed_triangle_set its = box(x_mm, y_mm, z_mm);
    indexed_triangle_set pocket = box(x_mm - 2. * wall_mm, y_mm - 2. * wall_mm,
                                      pocket_floor_mm - pocket_roof_mm, pocket_roof_mm);
    MeshBoolean::cgal::minus(its, pocket);
    return its;
}

// A cup standing open side down on the plate: a hollow block open at the bottom, so the first layer
// is a ring and the empty ring it leaves is sealed by the solid above it.
indexed_triangle_set
cup_open_down(double x_mm, double y_mm, double z_mm, double wall_mm)
{
    indexed_triangle_set its = box(x_mm, y_mm, z_mm);
    // Cavity from -layer_height_mm to z_mm (height z_mm + layer_height_mm), so it intersects the box
    // from 0 to z_mm - layer_height_mm, leaving a solid roof of thickness layer_height_mm at the top.
    indexed_triangle_set cavity =
        box(x_mm - 2. * wall_mm, y_mm - 2. * wall_mm, z_mm, -layer_height_mm);
    MeshBoolean::cgal::minus(its, cavity);
    return its;
}

// Four points on the bottom face of a 20 mm block, on the solid rim the shapes above leave around
// their pocket, so a part lifted by its supports has a tree to stand on.
Slic3r::Domain::SLA::SupportPoints corner_points()
{
    using Slic3r::Domain::SLA::SupportPoint;
    using Slic3r::Domain::SLA::SupportPointType;
    using Slic3r::Domain::Vec3f;

    return {
        SupportPoint{Vec3f{ 8.f,  8.f, 0.f}, 0.2f, SupportPointType::island},
        SupportPoint{Vec3f{-8.f,  8.f, 0.f}, 0.2f, SupportPointType::island},
        SupportPoint{Vec3f{ 8.f, -8.f, 0.f}, 0.2f, SupportPointType::island},
        SupportPoint{Vec3f{-8.f, -8.f, 0.f}, 0.2f, SupportPointType::island},
    };
}

Slic3r::Domain::ConfigPackSLA make_print_config(RaftType raft)
{
    Slic3r::Domain::ConfigPackSLA config;
    config.sla_printer_settings.items.opt("sla_archive_format").set(std::string("SL1"));
    config.sla_print_settings.items.opt("layer_height").set(layer_height_mm);
    config.sla_material_settings.items.opt("initial_layer_height").set(layer_height_mm);
    config.sla_print_settings.items.opt("supports_enable").set(false);
    config.sla_print_settings.items.opt("raft_type").set(raft);
    // The knobs a raft around an object standing on the plate is built from, the same values
    // sla_pad_robustness_tests uses so that a raft really is built rather than skipped.
    config.sla_print_settings.items.opt("pad_around_object_everywhere").set(true);
    config.sla_print_settings.items.opt("pad_wall_thickness").set(1.5);
    config.sla_print_settings.items.opt("pad_wall_height").set(0.);
    config.sla_print_settings.items.opt("pad_wall_slope").set(90.);
    config.sla_print_settings.items.opt("pad_brim_size").set(4.0);
    config.sla_print_settings.items.opt("pad_object_gap").set(0.5);
    return config;
}

// Local stand-in for the fff_print test helper, which is not on this target's include path.
Slic3r::Domain::Preset::SelectedPresetMetadata
make_preset_metadata(const Slic3r::Domain::Preset::HwPrinterConfig& hw_config)
{
    return Slic3r::Domain::Preset::SelectedPresetMetadata{
        .hw_config = hw_config,
        .tools =
            std::vector<Slic3r::Domain::Preset::EvaluatedPresetMetadata>{hw_config.tool_count},
        .materials = std::vector<Slic3r::Domain::Preset::EvaluatedPresetMetadata>{
            hw_config.material_slot_count()}
    };
}

// The tests never inspect thumbnails, so requests are dropped right away.
class NoopThumbnailGenerator : public Slic3r::Biz::Slicing::IThumbnailImageGenerator
{
    std::future<Slic3r::Biz::Slicing::ThumbnailImageResults> enqueue_thumbnail_requests(
        const Slic3r::Biz::Slicing::ThumbnailImageRequests&) override
    {
        std::promise<Slic3r::Biz::Slicing::ThumbnailImageResults> promise;
        promise.set_value(Slic3r::Biz::Slicing::ThumbnailImageResults{});
        return promise.get_future();
    }

    void handle_enqueued_requests() override {}
};

// What one object came to: which raft its raft_type resolved to, what the slice printed of it and
// what the support preview would draw of it.
struct RaftOutcome
{
    std::optional<RaftType> raft_type;    //< what the rule decided, empty when it was not Auto
    bool                    pad_enabled;  //< what is_pad_enabled() says for the object
    bool                    zero_elev;    //< whether the object stands on the plate
    bool                    sliced_pad;   //< whether the slice printed a raft
    bool                    preview_pad;  //< whether the support preview built one
    double                  elevation_mm; //< how high above the plate the object prints
};

// The tool API takes the raw config pointers and resolves the SLA object view itself.
struct ToolConfig
{
    Slic3r::Domain::FullConfigSLAPtr         full;
    Slic3r::Domain::PartialObjectConfigSLAPtr object_settings;
};

ToolConfig make_tool_config(const Slic3r::Domain::ConfigPackSLA& pack)
{
    ToolConfig cfg;
    cfg.full = std::make_shared<const Slic3r::Domain::FullConfigSLA>(
        pack, Slic3r::Domain::Preset::HwPrinterConfig{
                  .technology = Slic3r::Domain::PrinterTechnology::SLA});
    cfg.object_settings = std::make_shared<const Slic3r::Domain::PartialObjectConfigSLA>(
        Slic3r::Domain::SLAObjectSettings{}, cfg.full->hw_config());
    return cfg;
}

// Slice @p its as a model standing on the plate, and ask the support preview for the tree of the
// very same object. Nothing here slices outside that one print: the preview is the support tool
// path, which builds a tree and a raft and slices neither.
RaftOutcome resolve_and_build(const indexed_triangle_set&                its,
                              Slic3r::Domain::ConfigPackSLA               config,
                              const Slic3r::Domain::SLA::SupportPoints& points = {})
{
    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object = model.add_object();
    object->name = "shape.stl";
    Slic3r::Biz::Algorithms::ModelObject::add_volume(
        object, Slic3r::Biz::Algorithms::TriangleMesh::construct(its));
    object->add_instance();
    object->sla_support_points = points;

    Slic3r::Domain::Bed bed;
    Slic3r::Domain::BedInstance bed_instance{bed};
    for (Slic3r::Domain::ModelObject* obj : model.objects) {
        for (Slic3r::Domain::ModelInstance* instance : obj->instances) {
            bed_instance.model_instances.push_back(instance);
        }
    }

    auto hw_config =
        Slic3r::Test::create_dummy_hw_config(1, 0, Slic3r::Domain::PrinterTechnology::SLA);
    auto preset_metadata = make_preset_metadata(hw_config);
    auto metadata = Slic3r::Biz::Slicing::build_gcode_metadata({}, preset_metadata, config);

    Slic3r::SLAPrint print{
        [](Slic3r::Biz::Slicing::SLAResult&&) {}, [](const Slic3r::Biz::Slicing::Sla::Object&) {}};
    print.update(model, config, bed_instance, preset_metadata,
                 Slic3r::Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config));

    NoopThumbnailGenerator thumbnail_generator;
    print.slice(Slic3r::Domain::SlicingId{0, 0}, thumbnail_generator, std::nullopt);

    REQUIRE(print.m_objects.size() == 1);
    const Slic3r::SLAPrintObject& po = *print.m_objects.front();

    RaftOutcome outcome;
    if (po.object_raft().has_value()) {
        outcome.raft_type = po.object_raft()->type;
    }
    outcome.pad_enabled  = Slic3r::is_pad_enabled(po.config(), po.object_raft());
    outcome.zero_elev    = Slic3r::is_zero_elevation(po.config(), po.object_raft());
    outcome.sliced_pad   = !po.pad_mesh().empty();
    outcome.elevation_mm = po.get_elevation();

    // The support preview of the same object, through the support tool path the service calls.
    const ToolConfig tool = make_tool_config(config);
    const Slic3r::sla::SupportToolTree tree = Slic3r::sla::build_support_tree_for_tool(
        Slic3r::sla::support_tool_model_mesh(*object), Slic3r::Domain::Transform3d::Identity(), points,
        tool.full, tool.object_settings, [] { return false; });
    outcome.preview_pad = tree.pad != nullptr && !tree.pad->empty();

    return outcome;
}

} // namespace

TEST_CASE("Auto builds no raft under a solid block standing on the plate", "[SLA][RaftAuto]")
{
    const RaftOutcome outcome =
        resolve_and_build(box(20., 20., 20.), make_print_config(RaftType::Auto));

    // R6.1: a raft wastes resin, and a solid block has no pocket to seal against the film.
    REQUIRE(outcome.raft_type.has_value());
    CHECK(*outcome.raft_type == RaftType::None);
    CHECK_FALSE(outcome.pad_enabled);
    CHECK_FALSE(outcome.zero_elev);
    CHECK_FALSE(outcome.sliced_pad);
    CHECK_FALSE(outcome.preview_pad);
    // Nothing to lift the object for, so it prints on the plate it stands on.
    CHECK(outcome.elevation_mm == Catch::Approx(0.));
}

TEST_CASE("Auto builds a raft under a cup standing open side down", "[SLA][RaftAuto]")
{
    const RaftOutcome outcome =
        resolve_and_build(cup_open_down(20., 20., 20., 4.), make_print_config(RaftType::Auto));

    // R6.2: the first layer of the cup is a ring and the empty ring it leaves is sealed by the solid
    // above it, which is a suction cup against the vat film. It stands on the plate, so the raft
    // goes around it and the object prints where it stands.
    REQUIRE(outcome.raft_type.has_value());
    CHECK(*outcome.raft_type == RaftType::AroundObject);
    CHECK(outcome.pad_enabled);
    CHECK(outcome.zero_elev);
    CHECK(outcome.sliced_pad);
    CHECK(outcome.preview_pad);
    CHECK(outcome.elevation_mm == Catch::Approx(0.));
}

TEST_CASE("Auto builds a raft under a hollow sole", "[SLA][RaftAuto]")
{
    // A 20 mm block with a 12 mm pocket in the bottom millimetre of it: the pocket is closed above
    // by the sole and open below onto the film, which is the same suction as the cup's.
    const RaftOutcome outcome = resolve_and_build(
        block_with_pocket(20., 20., 20., 4., 0., 1.0), make_print_config(RaftType::Auto));

    REQUIRE(outcome.raft_type.has_value());
    CHECK(*outcome.raft_type == RaftType::AroundObject);
    CHECK(outcome.pad_enabled);
    CHECK(outcome.sliced_pad);
    CHECK(outcome.preview_pad);
    CHECK(outcome.elevation_mm == Catch::Approx(0.));
}

TEST_CASE("A raft the rule did not ask for is not built, whatever the underside is", "[SLA][RaftAuto]")
{
    // The four types that are not Auto are the user's own decision and M7.8.4 leaves them as they
    // were: a cup under None prints without a raft, and the same cup under Around object gets one
    // because the user asked for it and not because the rule read anything.
    const indexed_triangle_set cup = cup_open_down(20., 20., 20., 4.);

    SECTION("None prints no raft")
    {
        const RaftOutcome outcome = resolve_and_build(cup, make_print_config(RaftType::None));
        CHECK_FALSE(outcome.raft_type.has_value());
        CHECK_FALSE(outcome.pad_enabled);
        CHECK_FALSE(outcome.sliced_pad);
        CHECK_FALSE(outcome.preview_pad);
    }

    SECTION("Around object prints one")
    {
        const RaftOutcome outcome = resolve_and_build(cup, make_print_config(RaftType::AroundObject));
        CHECK_FALSE(outcome.raft_type.has_value());
        CHECK(outcome.pad_enabled);
        CHECK(outcome.zero_elev);
        CHECK(outcome.sliced_pad);
        CHECK(outcome.preview_pad);
    }
}

TEST_CASE("The slice and the support preview resolve the same raft", "[SLA][RaftAuto]")
{
    // The rule is one function on the mesh in the print pose, so the raft of the preview is the
    // raft of the print: the same mesh, the same elevation and the same settings give the same
    // answer on both sides, whatever the underside is.
    for (const indexed_triangle_set its :
         {box(20., 20., 20.), cup_open_down(20., 20., 20., 4.),
          block_with_pocket(20., 20., 20., 4., 0., 1.0)}) {
        const RaftOutcome outcome = resolve_and_build(its, make_print_config(RaftType::Auto));
        INFO("part volume " << Slic3r::Domain::its_volume(its) << " mm3");
        // The raft that was resolved is the one the config helpers answer for, and an Auto that
        // resolved to None is the no raft of R6.1. The conjunction is spelled out here: a CHECK
        // takes no `&&` (see the traps in AGENTS.md).
        const bool expect_pad = outcome.raft_type.has_value() && *outcome.raft_type != RaftType::None;
        CHECK(outcome.preview_pad == outcome.sliced_pad);
        CHECK(outcome.pad_enabled == expect_pad);
    }
}

TEST_CASE("An unresolved Auto builds no raft, the safe half of the rule", "[SLA][RaftAuto]")
{
    // Nothing has resolved the Auto of this config, which is the state of every config between the
    // moment it is read and the moment the object slice step reads the underside of its object.
    Slic3r::Domain::SLAObjectSettings settings;
    settings.overrides.set("raft_type", RaftType::Auto);

    Slic3r::Domain::FullConfigSLAPtr full{std::make_shared<const Slic3r::Domain::FullConfigSLA>(
        Slic3r::Domain::FullConfigSLA::defaults())};
    Slic3r::Domain::PartialObjectConfigSLAPtr object{
        std::make_shared<const Slic3r::Domain::PartialObjectConfigSLA>(settings, full->hw_config())};
    const Slic3r::SLAPrintObjectConfigView view{full, object};

    CHECK(Slic3r::is_raft_auto(view));
    // Unresolved (no raft passed): the helpers answer the no raft of R6.1.
    CHECK_FALSE(Slic3r::is_pad_enabled(view));
    CHECK_FALSE(Slic3r::is_pad_around_object(view));
    CHECK_FALSE(Slic3r::is_zero_elevation(view));
    // An Auto that found no suction cup RESOLVES to RaftType::None (R6.1), it is not unresolved.
    const std::optional<Slic3r::ObjectRaft> raft_solid =
        Slic3r::resolve_object_raft(view, box(20., 20., 20.), 0.);
    REQUIRE(raft_solid.has_value());
    CHECK_FALSE(raft_solid->suction);
    CHECK(raft_solid->type == RaftType::None);
    CHECK_FALSE(Slic3r::is_pad_enabled(view, raft_solid));
    CHECK_FALSE(Slic3r::is_pad_around_object(view, raft_solid));
    CHECK_FALSE(Slic3r::is_zero_elevation(view, raft_solid));

    // The other half: with the decision the rule made for a cup, it is a raft around the object.
    const std::optional<Slic3r::ObjectRaft> raft =
        Slic3r::resolve_object_raft(view, cup_open_down(20., 20., 20., 4.), 0.);
    REQUIRE(raft.has_value());
    CHECK(raft->suction);
    CHECK(raft->type == RaftType::AroundObject);
    CHECK(Slic3r::is_pad_enabled(view, raft));
    CHECK(Slic3r::is_pad_around_object(view, raft));
    CHECK(Slic3r::is_zero_elevation(view, raft));
}

TEST_CASE("Auto builds the slab a lifted part stands on", "[SLA][RaftAuto]")
{
    // A part the supports hold above the plate closes its pocket against the raft-less layers it
    // prints first rather than against the film. A full plate raft is the slab the pillars stand
    // on, and its top skin is what the pocket opens onto; a raft around the object would have to be
    // built off the film with no tree to hang on, which generate_pad() gives up on.
    Slic3r::Domain::ConfigPackSLA config = make_print_config(RaftType::Auto);
    config.sla_print_settings.items.opt("supports_enable").set(true);
    config.sla_print_settings.items.opt("support_object_elevation").set(5.);

    const RaftOutcome outcome =
        resolve_and_build(block_with_pocket(20., 20., 20., 4., 0., 1.0), config, corner_points());

    REQUIRE(outcome.raft_type.has_value());
    CHECK(*outcome.raft_type == RaftType::Full);
    CHECK(outcome.pad_enabled);
    CHECK_FALSE(outcome.zero_elev);
    // The slab is under the pillars, so the object is printed above the plate, not on it.
    CHECK(outcome.elevation_mm > 0.);
    CHECK(outcome.sliced_pad);
    CHECK(outcome.preview_pad);
}

TEST_CASE("auto_raft_decision finds the cup of a cup standing open side down", "[SLA][RaftAuto]")
{
    // Diagnostic test: call auto_raft_decision directly and also repeat its steps manually
    // to understand why the cup is not detected.
    const indexed_triangle_set mesh = cup_open_down(20., 20., 20., 4.);
    const double layer_height_mm = 0.05;
    const double elevation_mm = 0.0;
    const sla::RaftAutoOptions opts{1000.0, 1.0}; // scan_height_mm = 1000, min_cup_opening_mm2 = 1

    // Call the function directly
    const sla::RaftAutoDecision decision =
        sla::auto_raft_decision(mesh, layer_height_mm, elevation_mm, opts, [] { return false; });

    // Report mesh bounding box
    const Domain::BoundingBox3d bb = Domain::bounding_box(mesh);
    INFO("Mesh bbox: min=(" << bb.min.x() << "," << bb.min.y() << "," << bb.min.z()
         << ") max=(" << bb.max.x() << "," << bb.max.y() << "," << bb.max.z() << ")");

    // Replicate scan_layers logic (from RaftAuto.cpp anonymous namespace)
    std::vector<float> zs;
    std::vector<float> thicknesses_mm;
    {
        const Domain::BoundingBox3d bb2 = Domain::bounding_box(mesh);
        const double bottom = bb2.min.z() + elevation_mm;
        const double scan_top = std::min(bb2.max.z() + elevation_mm, bottom + opts.scan_height_mm);

        zs.clear();
        thicknesses_mm.clear();
        zs.push_back(float(bottom - layer_height_mm * 0.5));
        thicknesses_mm.push_back(float(layer_height_mm));
        for (double z = bottom + layer_height_mm * 0.5; z < scan_top; z += layer_height_mm) {
            zs.push_back(float(z));
            thicknesses_mm.push_back(float(layer_height_mm));
        }
        zs.push_back(float(scan_top + layer_height_mm * 0.5));
        thicknesses_mm.push_back(float(layer_height_mm));
    }
    INFO("zs.size() = " << zs.size());
    INFO("zs.front() = " << zs.front());
    INFO("zs.back() = " << zs.back());

    // Slice the mesh
    Slic3r::MeshSlicingParamsEx params;
    const std::vector<Domain::ExPolygons> layers = Slic3r::slice_mesh_ex(mesh, zs, params);

    // Detect cavities
    const SLA::CavityAnalysis cavities = SLA::detect_cavities(
        layers, thicknesses_mm, SLA::CavityDetectionOptions{opts.min_cup_opening_mm2});

    // Report layer details for key layers
    auto report_layer = [&](size_t idx, const char* label) {
        if (idx < layers.size()) {
            const auto& layer = layers[idx];
            size_t num_expolys = layer.size();
            size_t total_holes = 0;
            double total_area = 0.0;
            for (const auto& expoly : layer) {
                total_holes += expoly.holes.size();
                total_area += std::abs(expoly.area()) * Slic3r::Biz::Algorithms::Scaling::SCALING_FACTOR * Slic3r::Biz::Algorithms::Scaling::SCALING_FACTOR;
            }
            INFO(label << " layer " << idx << ": z=" << zs[idx] << " expolys=" << num_expolys
                 << " holes=" << total_holes << " area_mm2=" << total_area);
        }
    };

    report_layer(0, "first");
    report_layer(1, "second");
    report_layer(2, "third");
    size_t mid = layers.size() / 2;
    report_layer(mid, "middle");
    if (layers.size() >= 2) report_layer(layers.size() - 2, "last-but-one");
    if (layers.size() >= 1) report_layer(layers.size() - 1, "last");

    // Report cavity detection results
    INFO("cavities.cups.size() = " << cavities.cups.size());
    INFO("cavities.trapped_resin.size() = " << cavities.trapped_resin.size());
    for (size_t i = 0; i < cavities.cups.size(); ++i) {
        const auto& cup = cavities.cups[i];
        INFO("cup " << i << ": first_layer=" << cup.first_layer
             << " last_layer=" << cup.last_layer
             << " opening_area_mm2=" << cup.opening_area_mm2
             << " volume_mm3=" << cup.volume_mm3
             << " centroid=(" << cup.opening_centroid.x() << "," << cup.opening_centroid.y() << ")");
    }

    // Check the decision
    REQUIRE(decision.suction);
    CHECK(decision.first_cup_layer == 1); // cup starts at layer 1 (layer 0 is below part)
}