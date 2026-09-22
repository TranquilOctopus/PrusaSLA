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

#include <algorithm>
#include <chrono>

using namespace Slic3r::Biz;
using namespace trompeloeil;

using Slic3r::Test::ModelOnBed;
using Slic3r::Test::StatusEvents;
using Slic3r::Test::get_cubes_model;
using Slic3r::Test::wait_for_status;

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
        project_interactor.slicing_interactor().add_listener<Slicing::IStatusListener>(&status_listener);
    }

    Slic3r::Domain::Workbench workbench;
    Slic3r::App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope{dispatcher};
    Tests::JobManagerScope job_manager_scope{dispatcher};
    Slic3r::Test::MockThumbnailImageGenerator thumbnail_image_generator;
    ProjectInteractor project_interactor{workbench, dispatcher, thumbnail_image_generator};
    Slic3r::Test::StatusListener status_listener;
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

// The only test that exercises the wiring end to end: a real slice, then the cache lookup,
// the slicing-id construction and the config reads that feed ResinEconomics::calculate.
TEST_CASE_METHOD(
    ResinEconomicsInteractorFixture,
    "ResinEconomicsInteractor - sliced bed reports resin usage",
    "[resin_economics_interactor][timeout]"
)
{
    using namespace std::chrono_literals;

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
    project_interactor.slicing_interactor().slice_all();

    REQUIRE(wait_for_status(dispatcher, status_listener, 120s, [](const StatusEvents& events) {
        return !events.empty() && events.back().status_code == Slicing::StatusCode::Finished;
    }));

    const ProjectResinEconomics result = economics_interactor.compute_project_economics(project_id);

    REQUIRE(result.beds_with_results == 1);
    REQUIRE(result.beds.size() == result.beds_with_results + result.beds_skipped);

    const auto sliced = std::ranges::find_if(
        result.beds,
        [](const BedResinEconomics& bed) { return bed.has_result; }
    );
    REQUIRE(sliced != result.beds.end());
    REQUIRE(sliced->bed_instance_id == bed_instance.id().id);
    REQUIRE(sliced->economics.millilitres.has_value());
    REQUIRE(*sliced->economics.millilitres > 0.0);

    // The project total is the sum over beds that produced a result; with one bed it equals it.
    REQUIRE(result.total.millilitres.has_value());
    REQUIRE(*result.total.millilitres == *sliced->economics.millilitres);
    REQUIRE(result.total.summary != "No data");
}
