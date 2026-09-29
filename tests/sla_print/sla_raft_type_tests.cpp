// raft_type is the single source of truth for the SLA raft (pad). "Use raft" (pad_enable)
// and "Raft around object" (pad_around_object) are hidden in the UI and only read for configs
// that have no raft_type, so a config that says raft None with pad_enable on must not get a pad.
#include <catch2/catch_test_macros.hpp>

#include <future>

#include "Slic3r/Domain/ConfigBoxesSLA.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/ConfigPack.hpp"
#include "Slic3r/Domain/FullConfigSLA.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/SLA/SupportPoint.hpp"

#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Algorithms/TriangleMesh.hpp"
#include "Slic3r/Biz/Slicing/BackgroundProcess.hpp"
#include "Slic3r/Domain/Bed.hpp"
#include "Slic3r/Domain/BedInstance.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"
#include "Slic3r/Domain/PrinterTechnology.hpp"
#include "Slic3r/Domain/SlicingId.hpp"
#include "Slic3r/TestUtils/HwConfigUtils.hpp"

#include "libslic3r/ConfigViews.hpp"
#include "libslic3r/IThumbnailImageGenerator.hpp"
#include "libslic3r/SLAPrint.hpp"

namespace {

// A config view over the given object settings, the way the engine builds one for a print object.
// The raft settings live in the Print box, so a per object change is an override.
Slic3r::SLAPrintObjectConfigView make_config_view(Slic3r::Domain::SLAObjectSettings& settings)
{
    Slic3r::Domain::FullConfigSLAPtr full{std::make_shared<const Slic3r::Domain::FullConfigSLA>(
        Slic3r::Domain::FullConfigSLA::defaults())};
    Slic3r::Domain::PartialObjectConfigSLAPtr object{
        std::make_shared<const Slic3r::Domain::PartialObjectConfigSLA>(settings, full->hw_config())};
    return Slic3r::SLAPrintObjectConfigView{full, object};
}

// Local stand-in for the fff_print test helper, which is not on this target's include path.
Slic3r::Domain::Preset::SelectedPresetMetadata
make_preset_metadata(const Slic3r::Domain::Preset::HwPrinterConfig& hw_config)
{
    return Slic3r::Domain::Preset::SelectedPresetMetadata{
        .hw_config = hw_config,
        .tools     = std::vector<Slic3r::Domain::Preset::EvaluatedPresetMetadata>{hw_config.tool_count},
        .materials = std::vector<Slic3r::Domain::Preset::EvaluatedPresetMetadata>{hw_config.material_slot_count()}
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

// A 20 mm cube with four support points on its bottom face, so a support tree and a raft
// under the lifted object can actually be generated.
struct CubeWithSupports {
    Slic3r::Domain::Model model;

    CubeWithSupports()
    {
        using Slic3r::Domain::SLA::SupportPoint;
        using Slic3r::Domain::SLA::SupportPointType;
        using Slic3r::Domain::Vec3f;

        Slic3r::Domain::ModelObject* object = model.add_object();
        object->name = "cube.stl";
        Slic3r::Biz::Algorithms::add_volume(
            object, Slic3r::Biz::Algorithms::TriangleMesh::make_cube(20., 20., 20.));
        object->add_instance();
        object->sla_support_points = {
            SupportPoint{Vec3f{ 5.f,  5.f, 0.f}, 0.2f, SupportPointType::island},
            SupportPoint{Vec3f{15.f,  5.f, 0.f}, 0.2f, SupportPointType::island},
            SupportPoint{Vec3f{ 5.f, 15.f, 0.f}, 0.2f, SupportPointType::island},
            SupportPoint{Vec3f{15.f, 15.f, 0.f}, 0.2f, SupportPointType::island},
        };
    }
};

// The BedInstance refers to the bed, so both have to outlive the slice.
struct BedWithInstances {
    Slic3r::Domain::Bed bed;
    Slic3r::Domain::BedInstance bed_instance{bed};

    explicit BedWithInstances(Slic3r::Domain::Model& model)
    {
        for (const Slic3r::Domain::ModelObject* object : model.objects) {
            for (Slic3r::Domain::ModelInstance* instance : object->instances)
                bed_instance.model_instances.push_back(instance);
        }
    }
};

struct SliceResult {
    bool pad_enabled;
    bool has_pad;
    double elevation;
};

// Slice the cube and report what the engine did with the raft.
SliceResult slice_cube(Slic3r::Domain::ConfigPackSLA& config, Slic3r::Domain::Model& model)
{
    BedWithInstances bed{model};

    auto hw_config = Slic3r::Test::create_dummy_hw_config(1, 0, Slic3r::Domain::PrinterTechnology::SLA);
    auto preset_metadata = make_preset_metadata(hw_config);
    auto metadata = Slic3r::Biz::Slicing::build_gcode_metadata({}, preset_metadata, config);

    Slic3r::SLAPrint print{
        [](Slic3r::Biz::Slicing::SLAResult&&) {}, [](const Slic3r::Biz::Slicing::Sla::Object&) {}};
    print.update(model, config, bed.bed_instance, preset_metadata,
                 Slic3r::Biz::Slicing::build_metadata_serializer(metadata, preset_metadata, config));

    NoopThumbnailGenerator thumbnail_generator;
    print.slice(Slic3r::Domain::SlicingId{0, 0}, thumbnail_generator, std::nullopt);

    REQUIRE(print.m_objects.size() == 1);
    const Slic3r::SLAPrintObject& po = *print.m_objects.front();
    return SliceResult{.pad_enabled = Slic3r::is_pad_enabled(po.config()),
                       .has_pad     = !po.pad_mesh().empty(),
                       .elevation   = po.get_elevation()};
}

Slic3r::Domain::ConfigPackSLA make_print_config(Slic3r::Domain::sla::RaftType raft, bool pad_enable)
{
    Slic3r::Domain::ConfigPackSLA config;
    config.sla_printer_settings.items.opt("sla_archive_format").set(std::string("SL1"));
    config.sla_print_settings.items.opt("layer_height").set(0.05);
    config.sla_material_settings.items.opt("initial_layer_height").set(0.05);
    config.sla_print_settings.items.opt("supports_enable").set(true);
    config.sla_print_settings.items.opt("pad_enable").set(pad_enable);
    config.sla_print_settings.items.opt("raft_type").set(raft);
    return config;
}

} // namespace

TEST_CASE("is_pad_enabled follows raft_type, not pad_enable", "[SLA][RaftType]")
{
    using Slic3r::Domain::sla::RaftType;

    SECTION("raft None wins over pad_enable on")
    {
        Slic3r::Domain::SLAObjectSettings settings;
        settings.overrides.set("pad_enable", true);
        settings.overrides.set("raft_type", RaftType::None);

        CHECK_FALSE(Slic3r::is_pad_enabled(make_config_view(settings)));
    }

    SECTION("a raft type wins over pad_enable off")
    {
        for (const RaftType raft : {RaftType::Full, RaftType::AroundObject, RaftType::Skate}) {
            CAPTURE(int(raft));
            Slic3r::Domain::SLAObjectSettings settings;
            settings.overrides.set("pad_enable", false);
            settings.overrides.set("raft_type", raft);

            CHECK(Slic3r::is_pad_enabled(make_config_view(settings)));
        }
    }

    SECTION("Around object and Skate raft around the object, Full and None do not")
    {
        const auto is_around_object = [](RaftType raft) {
            Slic3r::Domain::SLAObjectSettings settings;
            settings.overrides.set("raft_type", raft);
            return Slic3r::is_pad_around_object(make_config_view(settings));
        };

        CHECK(is_around_object(RaftType::AroundObject));
        CHECK(is_around_object(RaftType::Skate));
        CHECK_FALSE(is_around_object(RaftType::Full));
        CHECK_FALSE(is_around_object(RaftType::None));
    }
}

TEST_CASE("A sliced cube gets a pad only when raft_type asks for one", "[SLA][RaftType]")
{
    using Slic3r::Domain::sla::RaftType;

    SECTION("raft None with pad_enable on slices without a pad")
    {
        Slic3r::Domain::ConfigPackSLA config = make_print_config(RaftType::None, /*pad_enable*/ true);
        CubeWithSupports cube;

        const SliceResult result = slice_cube(config, cube.model);
        CHECK_FALSE(result.pad_enabled);
        CHECK_FALSE(result.has_pad);
    }

    SECTION("raft Full with pad_enable off still gets a pad under the lifted object")
    {
        Slic3r::Domain::ConfigPackSLA config = make_print_config(RaftType::Full, /*pad_enable*/ false);
        CubeWithSupports cube;

        const SliceResult result = slice_cube(config, cube.model);
        CHECK(result.pad_enabled);
        CHECK(result.has_pad);
        // The object is lifted by the support pillars, so the raft sits under them.
        CHECK(result.elevation > 0.);
    }
}
