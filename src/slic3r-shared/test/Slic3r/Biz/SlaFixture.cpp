#include "Slic3r/Biz/SlaFixture.hpp"

#include <chrono>
#include <thread>

namespace Slic3r::Test {

SlaSlicingFixture::SlaSlicingFixture()
    : workbench{}
    , dispatcher{}
    , app_instance_message_handler_scope{dispatcher}
    , job_manager_scope{dispatcher}
    , thumbnail_image_generator{}
    , project_interactor{workbench, dispatcher, thumbnail_image_generator}
    , thread_dispatcher{dispatcher}
{
    boost::nowide::nowide_filesystem();

    std::unique_ptr<SecretStoreDummy> store_dummy = std::make_unique<SecretStoreDummy>();
    Platform::PlatformServices::instance().set_secret_store(std::move(store_dummy));

    Slic3r::set_data_dir(Tests::get_datadir().string());

    project_interactor.preset_interactor().load_preset_bundle(
        Preset::IO::BundlePaths::make_test_runtime(Tests::get_datadir())
    );
}

std::shared_ptr<const Biz::Slicing::SLAResultData> SlaSlicingFixture::slice_sla_model(
    const Domain::Model& model,
    const Domain::ConfigPackSLA& config
)
{
    std::promise<std::shared_ptr<const Biz::Slicing::SLAResultData>> promise;
    auto future = promise.get_future();

    SlicingStatusListener listener{project_interactor, promise};
    project_interactor.slicing_interactor().add_listener<Biz::Slicing::IStatusListener>(&listener);

    Domain::Model model_copy = model;
    Domain::ConfigPackSLA config_copy = config;

    auto project_metadata = Domain::ProjectMetadata{};
    auto preset_metadata = Domain::Preset::SelectedPresetMetadata{};
    auto bed = Domain::Bed{};
    auto bed_instance = Domain::BedInstance{bed, Domain::SelectionId{0}};

    project_interactor.new_project();

    project_interactor.slicing_interactor().update_process(
        model_copy,
        project_metadata,
        preset_metadata,
        config_copy,
        bed_instance
    );
    project_interactor.slicing_interactor().slice_all();

    const auto status = future.wait_for(30s);
    REQUIRE(status == std::future_status::ready);

    return future.get();
}

} // namespace Slic3r::Test