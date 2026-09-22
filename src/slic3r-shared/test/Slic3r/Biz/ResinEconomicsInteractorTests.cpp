#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

#include "Slic3r/Biz/ResinEconomicsInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/SecretStoreDummy.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/Slicing/TestUtils.hpp"

#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"

#include "Slic3r/Domain/ProjectMetadata.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"

#include "Slic3r/Directories.hpp"
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/ScopedThreadDispatcher.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/nowide/filesystem.hpp>



using namespace Slic3r::Biz;
using namespace trompeloeil;

using Slic3r::Test::ModelOnBed;
using Slic3r::Test::get_cubes_model;

struct ResinEconomicsInteractorFixture
{
    ResinEconomicsInteractorFixture()
    {
        boost::nowide::nowide_filesystem();

        std::unique_ptr<SecretStoreDummy> store_dummy = std::make_unique<SecretStoreDummy>();
        Platform::PlatformServices::instance().set_secret_store(std::move(store_dummy));

        Slic3r::set_data_dir(Tests::get_datadir().string());

        project_interactor.preset_interactor().load_preset_bundle(
            Preset::IO::BundlePaths::make_test_runtime(Tests::get_datadir())
        );
    }

    Slic3r::Domain::Workbench workbench;
    Slic3r::App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{dispatcher};
    Tests::JobManagerScope job_manager_scope{dispatcher};
    Slic3r::Test::MockThumbnailImageGenerator thumbnail_image_generator;
    ProjectInteractor project_interactor{workbench, dispatcher, thumbnail_image_generator};
    Tests::ScopedThreadDispatcher thread_dispatcher{dispatcher};
    ResinEconomicsInteractor economics_interactor{project_interactor};
};

// The bed counters must always account for every bed that was walked, whatever the project holds.
TEST_CASE_METHOD(
    ResinEconomicsInteractorFixture,
    "ResinEconomicsInteractor - unsliced project reports no economics",
    "[resin_economics_interactor]"
)
{
    const auto project_id = project_interactor.new_project();

    const ProjectResinEconomics result = economics_interactor.compute_project_economics(project_id);

    REQUIRE(result.beds_with_results == 0);
    REQUIRE(result.beds.size() == result.beds_skipped);
    REQUIRE_FALSE(result.total.millilitres.has_value());
    REQUIRE_FALSE(result.total.grams.has_value());
    REQUIRE_FALSE(result.total.cost.has_value());
    REQUIRE(result.total.summary == "No data");
}

TEST_CASE_METHOD(
    ResinEconomicsInteractorFixture,
    "ResinEconomicsInteractor - bed with a model but no slicing is skipped",
    "[resin_economics_interactor]"
)
{
    const auto project_id = project_interactor.new_project();

    const Slic3r::Domain::Project& project = project_interactor.project(project_id);
    REQUIRE_FALSE(project.config_containers().empty());
    const Slic3r::Domain::ConfigContainer& config_container = *project.config_containers().front();
    REQUIRE_FALSE(config_container.bed_instances().empty());
    const Slic3r::Domain::BedInstance& bed_instance = *config_container.bed_instances().front();

    ModelOnBed model_on_bed{get_cubes_model(1, 1, Slic3r::Domain::PrinterTechnology::SLA)};
    project_interactor.slicing_interactor().update_process(
        model_on_bed.model,
        model_on_bed.project_metadata,
        model_on_bed.preset_metadata,
        model_on_bed.config,
        bed_instance
    );

    const ProjectResinEconomics result = economics_interactor.compute_project_economics(project_id);

    REQUIRE(result.beds_with_results == 0);
    REQUIRE(result.beds_skipped >= 1);
    REQUIRE_FALSE(result.beds.empty());
    REQUIRE_FALSE(result.beds.front().has_result);
    // The bed is still identified even when there is nothing to report for it.
    REQUIRE(result.beds.front().bed_instance_id == bed_instance.id().id);
    REQUIRE_FALSE(result.beds.front().economics.summary.empty());
}

// NOT COVERED: the cache-hit path of compute_bed_economics -- the SLA result lookup, the
// slicing-id construction and the config reads that feed ResinEconomics::calculate. Every case
// above stops at an early return, so a regression there would go unnoticed here.
//
// Reaching it needs a project whose own bed is SLA, because compute_bed_economics resolves the
// bed through the project's config containers. Slicing a standalone bed the way SlaFixture does
// is not enough, hand-built SelectedPresetMetadata is rejected by load_selected_preset_from_3mf,
// and the test bundle's SLA vendor did not resolve under the hw-config id tried. See the M1.10
// note in doc/sla-fork/ROADMAP.md.
