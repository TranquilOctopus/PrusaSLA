#pragma once

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/SecretStoreDummy.hpp"
#include "Slic3r/Biz/SLAResultCache.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Biz/Slicing/TestUtils.hpp"

#include "Slic3r/App/Plater/ThumbnailImageGenerator.hpp"
#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"

#include "Slic3r/Directories.hpp"
#include "Slic3r/TestUtils/AppInstanceMessageHandlerScope.hpp"
#include "Slic3r/TestUtils/JobManagerScope.hpp"
#include "Slic3r/TestUtils/ScopedThreadDispatcher.hpp"
#include "Slic3r/TestUtils/TestData.hpp"

#include <boost/filesystem/operations.hpp>
#include <boost/nowide/filesystem.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/nowide/fstream.hpp>

#include <sstream>
#include <string>

using namespace Slic3r::Biz;
using namespace trompeloeil;
namespace fs = boost::filesystem;

namespace Slic3r::Test {

class SlaSlicingFixture
{
public:
    SlaSlicingFixture();

    std::shared_ptr<const Biz::Slicing::SLAResultData> slice_sla_model(
        const Domain::Model& model,
        const Domain::ConfigPackSLA& config
    );

private:
    struct SlicingStatusListener : public Biz::Slicing::IStatusListener
    {
        SlicingStatusListener(Biz::ProjectInteractor& pi, std::promise<std::shared_ptr<const Biz::Slicing::SLAResultData>>& promise)
            : m_pi(pi), m_promise(promise) {}

        void on_status_changed(const Biz::Slicing::StatusUpdate status_update, const Domain::SlicingId id) override
        {
            if (m_done)
                return;
            // A failed slice never reports Finished. Keep every update that carries errors so a
            // timeout can say why; an early one may be transient (the fresh project validates
            // before the test's config is applied), so do not stop waiting on it.
            if (!status_update.errors_to_append.empty()) {
                std::ostringstream ss;
                ss << status_update << '\n';
                errors += ss.str();
            }
            if (status_update.code && *status_update.code == Biz::Slicing::StatusCode::Finished) {
                const std::optional<Biz::SLAResultRef> sla_result{m_pi.sla_result_cache().get_result(id)};
                if (sla_result) {
                    m_done = true;
                    m_promise.set_value(sla_result.value().get().export_data);
                }
            }
        }

        std::string errors;
        Biz::ProjectInteractor& m_pi;
        std::promise<std::shared_ptr<const Biz::Slicing::SLAResultData>>& m_promise;
        bool m_done{false};
    };

    Domain::Workbench workbench;
    App::Platform::StdMainThreadDispatcher dispatcher;
    Tests::AppInstanceMessageHandlerScope app_instance_message_handler_scope;
    Tests::JobManagerScope job_manager_scope;
    MockThumbnailImageGenerator thumbnail_image_generator;
    Biz::ProjectInteractor project_interactor;
    Tests::ScopedThreadDispatcher thread_dispatcher;
};

} // namespace Slic3r::Test