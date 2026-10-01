#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/App/Plater/AutoOrientProgress.hpp"
#include "Slic3r/Biz/Platform/JobManager/JobManager.hpp"
#include "Slic3r/Domain/JobStatus.hpp"

#include <chrono>
#include <set>
#include <vector>

using namespace std::chrono_literals;

using Catch::Approx;
using Slic3r::App::Plater::auto_orient_progress_fraction;
using Slic3r::App::Plater::auto_orient_progress_percent;
using Slic3r::App::Platform::StdMainThreadDispatcher;
using Slic3r::Biz::JThread::StopToken;
using Slic3r::Biz::Platform::JobManager::IJobManagerStatusChangedListener;
using Slic3r::Biz::Platform::JobManager::JobManager;
using Slic3r::Biz::Platform::JobManager::JobManagerStatus;
using Slic3r::Biz::Platform::JobManager::Progress;
using Slic3r::Biz::Platform::JobManager::ProgressTracker;
using Slic3r::Domain::JobStatus;
using Slic3r::Domain::Percentage;

// The engine's per cent becomes the tracker's fraction, and back again (M4.15).
TEST_CASE("Auto orient progress converts per cent to a fraction and back", "[AutoOrientProgress]")
{
    for (int percent = 0; percent <= 100; ++percent) {
        const Percentage fraction{auto_orient_progress_fraction(percent)};
        CHECK(fraction.value >= 0.);
        CHECK(fraction.value <= 1.);
        CHECK(fraction.value == Approx(percent / 100.));
        CHECK(auto_orient_progress_percent(fraction) == percent);
    }
}

TEST_CASE("Auto orient progress clamps what the engine reports", "[AutoOrientProgress]")
{
    CHECK(auto_orient_progress_fraction(-1).value == Approx(0.));
    CHECK(auto_orient_progress_fraction(1000).value == Approx(1.));
    CHECK(auto_orient_progress_percent(Percentage{-3.}) == 0);
    CHECK(auto_orient_progress_percent(Percentage{7.}) == 100);
}

// The percent the search hands the tracker used to be the engine's 55, which the pop notification
// then showed as 5500%. A search reporting through a real job manager stays inside 0..1.
namespace {

struct StatusCollector : public IJobManagerStatusChangedListener
{
    void on_job_manager_status_changed(const JobManagerStatus& status) override
    {
        const auto it{status.find("sla_auto_orient")};
        if (it == status.end()) {
            return;
        }
        percentages.push_back(it->second);
        statuses.insert(it->second.status);
    }

    std::vector<Progress> percentages;
    std::set<JobStatus> statuses;
};

struct JobFixture
{
    JobFixture()
    {
        job_manager.add_listener<IJobManagerStatusChangedListener>(&collector);
    }

    ~JobFixture()
    {
        dispatcher.close();
    }

    // The job function of RotationDialog.cpp, without the search: what matters here is what the
    // engine's percentages turn into on their way into the tracker.
    void report(int from, int to)
    {
        job_manager
            .create_job(
                "sla_auto_orient",
                [from, to](StopToken, ProgressTracker progress)
                {
                    for (int percent = from; percent <= to; ++percent) {
                        progress.set(auto_orient_progress_fraction(percent));
                    }
                }
            )
            .on_result([]() {})
            .start();

        const auto start{std::chrono::high_resolution_clock::now()};
        while (!collector.statuses.contains(JobStatus::Finished)) {
            dispatcher.dispatch_enqueued();
            REQUIRE(std::chrono::high_resolution_clock::now() - start < 5s);
        }
    }

    StatusCollector collector;
    StdMainThreadDispatcher dispatcher;
    JobManager job_manager{dispatcher};
};

} // namespace

TEST_CASE_METHOD(
    JobFixture,
    "Auto orient job reports a fraction inside 0..1",
    "[AutoOrientProgress]"
)
{
    report(0, 100);

    size_t reported{0};
    for (const Progress& progress : collector.percentages) {
        if (!progress.percent) {
            continue;
        }
        ++reported;
        CHECK(progress.percent->value >= 0.);
        CHECK(progress.percent->value <= 1.);
    }
    CHECK(reported > 0);
}

TEST_CASE_METHOD(
    JobFixture,
    "Auto orient job reads back the per cent it reported",
    "[AutoOrientProgress]"
)
{
    report(0, 100);

    const Percentage last{collector.percentages.back().percent.value()};
    CHECK(auto_orient_progress_percent(last) == 100);
}
