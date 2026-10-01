#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Slic3r/Biz/FileLoadingLogic.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

using Slic3r::Domain::Vec3f;
using Slic3r::Biz::FileLoadingLogic::SlaArchiveImport;
using Slic3r::Biz::FileLoadingLogic::SlaArchiveRead;
using Slic3r::Biz::FileLoadingLogic::SlaArchiveReadResult;
using Slic3r::Domain::Index3;
using Slic3r::Domain::TriangleMesh;

namespace {

/// One triangle. The job contract is about whether a mesh is there at all, not about its shape,
/// and a single facet is the cheapest thing that is not empty.
TriangleMesh one_triangle()
{
    TriangleMesh mesh;
    mesh.its.vertices.emplace_back(Vec3f{0.f, 0.f, 0.f});
    mesh.its.vertices.emplace_back(Vec3f{1.f, 0.f, 0.f});
    mesh.its.vertices.emplace_back(Vec3f{0.f, 1.f, 0.f});
    mesh.its.indices.emplace_back(Index3{0, 1, 2});
    return mesh;
}

/// A stand-in for import_sl1_archive(): it reports the progress it is asked to report, honours the
/// stop it is handed, and answers with a mesh it was told to build. A real archive would have to be
/// sliced first, which is what makes the progress and the cancel of the job untestable without it.
class FakeArchiveReader
{
public:
    /// @brief What one read of one archive does: how many progress steps it takes, whether it
    /// reports a cancel of its own after the first of them, and what it answers with otherwise.
    struct Behaviour
    {
        int progress_steps{4};
        bool stop_after_first_step{false};
        std::string error;
    };

    std::vector<std::string> read_files;

    /// @brief Called with the index of each read once that read has answered. A test that cancels
    /// half way through a list flips its flag from here.
    std::function<void(std::size_t)> after_read;

    /// @brief Records what it is asked for and answers from @p behaviours, one per read, in order.
    SlaArchiveRead reader(const std::vector<Behaviour>& behaviours)
    {
        read_files.reserve(behaviours.size());
        return
            [this, behaviours](
                const boost::filesystem::path& path,
                const std::function<bool()>& stop,
                const std::function<void(double)>& progress
            )
        {
            const std::size_t index = read_files.size();
            read_files.push_back(path.filename().string());

            const Behaviour behaviour = index < behaviours.size() ? behaviours[index] : Behaviour{};
            const double steps        = double(std::max(behaviour.progress_steps, 1));
            for (int step = 0; step < behaviour.progress_steps; ++step) {
                if (progress)
                    progress(double(step + 1) / steps);
                // A read that stops itself part way through, which is what import_sl1_archive does
                // when a cancel lands between two layer images.
                if (behaviour.stop_after_first_step)
                    return SlaArchiveReadResult{.mesh = {}, .error = {}, .cancelled = true};
                if (stop && stop())
                    return SlaArchiveReadResult{.mesh = {}, .error = {}, .cancelled = true};
            }

            if (after_read)
                after_read(index);

            if (!behaviour.error.empty())
                return SlaArchiveReadResult{
                    .mesh      = {},
                    .error     = behaviour.error,
                    .cancelled = false
                };

            return SlaArchiveReadResult{.mesh = one_triangle(), .error = {}, .cancelled = false};
        };
    }
};

std::vector<boost::filesystem::path> archives(const std::vector<std::string>& names)
{
    std::vector<boost::filesystem::path> paths;
    paths.reserve(names.size());
    for (const std::string& name : names)
        paths.emplace_back("/somewhere/" + name);
    return paths;
}

} // namespace

TEST_CASE("An archive import reports its progress", "[import][sla][archive]")
{
    FakeArchiveReader fake;

    std::vector<double> progress;
    const std::vector<SlaArchiveImport> imported = Slic3r::Biz::FileLoadingLogic::read_sla_archives(
        archives({"one.sl1"}),
        {},
        [&progress](double share) { progress.push_back(share); },
        fake.reader({FakeArchiveReader::Behaviour{}})
    );

    SECTION("the archive lands on the plate with its mesh")
    {
        REQUIRE(imported.size() == 1);
        CHECK(imported.front().error.empty());
        CHECK_FALSE(imported.front().mesh.empty());
        CHECK(imported.front().file_name == "one.sl1");
        CHECK(fake.read_files == std::vector<std::string>{"one.sl1"});
    }

    SECTION("the bar moves and ends full, never going back")
    {
        REQUIRE_FALSE(progress.empty());
        CHECK(progress.front() == 0.0);
        CHECK(progress.back() == Catch::Approx(1.0));
        for (std::size_t i = 1; i < progress.size(); ++i)
            CHECK(progress[i] >= progress[i - 1]);
        for (const double share : progress) {
            CHECK(share >= 0.0);
            CHECK(share <= 1.0);
        }
    }
}

TEST_CASE("A cancelled archive import adds nothing", "[import][sla][archive]")
{
    FakeArchiveReader fake;

    SECTION("a stop asked for before the read begins stops it")
    {
        const std::vector<SlaArchiveImport> imported =
            Slic3r::Biz::FileLoadingLogic::read_sla_archives(
                archives({"one.sl1"}),
                []() { return true; },
                {},
                fake.reader({FakeArchiveReader::Behaviour{}})
            );
        CHECK(imported.empty());
        CHECK(fake.read_files.empty());
    }

    SECTION("a stop asked for while the archive is read leaves no entry behind")
    {
        const std::vector<SlaArchiveImport> imported =
            Slic3r::Biz::FileLoadingLogic::read_sla_archives(
                archives({"one.sl1"}),
                {},
                {},
                fake.reader({FakeArchiveReader::Behaviour{.stop_after_first_step = true}})
            );
        CHECK(imported.empty());
    }

    SECTION("a cancel between two archives leaves the first one off the plate as well")
    {
        // A cancel is the user deciding not to import, so nothing of the import reaches the
        // project: not even the archive that had already been read.
        bool cancel     = false;
        fake.after_read = [&cancel](std::size_t index)
        {
            if (index == 0)
                cancel = true;
        };
        const std::vector<SlaArchiveImport> imported =
            Slic3r::Biz::FileLoadingLogic::read_sla_archives(
                archives({"one.sl1", "two.sl1s"}),
                [&cancel]() { return cancel; },
                {},
                fake.reader({FakeArchiveReader::Behaviour{}, FakeArchiveReader::Behaviour{}})
            );
        CHECK(imported.empty());
        CHECK(fake.read_files.size() == 1);
    }
}

TEST_CASE(
    "A list of archives is read one by one, and a bad one does not stop the rest",
    "[import][sla][archive]"
)
{
    FakeArchiveReader fake;

    std::vector<double> progress;
    const std::vector<SlaArchiveImport> imported = Slic3r::Biz::FileLoadingLogic::read_sla_archives(
        archives({"one.sl1", "broken.sl1s", "three.sl1"}),
        {},
        [&progress](double share) { progress.push_back(share); },
        fake.reader({
            FakeArchiveReader::Behaviour{},
            FakeArchiveReader::Behaviour{.error = "the archive is not readable"},
            FakeArchiveReader::Behaviour{},
        })
    );

    REQUIRE(imported.size() == 3);
    CHECK(imported[0].error.empty());
    CHECK_FALSE(imported[0].mesh.empty());
    CHECK(imported[1].error == "the archive is not readable");
    CHECK(imported[1].mesh.empty());
    CHECK(imported[2].error.empty());
    CHECK(fake.read_files.size() == 3);

    // The bar is over all three, so it keeps moving and ends full whatever one of them does.
    REQUIRE_FALSE(progress.empty());
    CHECK(progress.front() == 0.0);
    CHECK(progress.back() == Catch::Approx(1.0));
    for (std::size_t i = 1; i < progress.size(); ++i)
        CHECK(progress[i] >= progress[i - 1]);
}

TEST_CASE("Only an archive extension is read as an archive", "[import][sla][archive]")
{
    using Slic3r::Biz::FileLoadingLogic::is_sla_archive_file;

    CHECK(is_sla_archive_file("cube.sl1"));
    CHECK(is_sla_archive_file("cube.sl1s"));
    CHECK(is_sla_archive_file("CUBE.SL1"));
    CHECK(is_sla_archive_file("/a/b/print.sl1s"));
    CHECK_FALSE(is_sla_archive_file("cube.stl"));
    CHECK_FALSE(is_sla_archive_file("cube.3mf"));
    CHECK_FALSE(is_sla_archive_file("sl1"));
    CHECK_FALSE(is_sla_archive_file(""));
}

TEST_CASE("A file that is not an archive is reported, not read", "[import][sla][archive]")
{
    FakeArchiveReader fake;

    const std::vector<SlaArchiveImport> imported = Slic3r::Biz::FileLoadingLogic::read_sla_archives(
        archives({"cube.stl"}),
        {},
        {},
        fake.reader({FakeArchiveReader::Behaviour{}})
    );

    REQUIRE(imported.size() == 1);
    CHECK_FALSE(imported.front().error.empty());
    CHECK(imported.front().mesh.empty());
    CHECK(fake.read_files.empty());
}
