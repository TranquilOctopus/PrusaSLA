#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/FileLoadingLogic.hpp"

#include <initializer_list>
#include <string>
#include <vector>

using Slic3r::Biz::FileLoadingLogic::get_resin_profile_extensions;
using Slic3r::Biz::FileLoadingLogic::is_resin_profile_file;
using Slic3r::Biz::FileLoadingLogic::is_supported_file;
using Slic3r::Biz::FileLoadingLogic::route_dropped_files;

namespace {

using boost::filesystem::path;

/// @brief The files of a drop, as the drop target collects them before routing them.
std::vector<path> drop(std::initializer_list<const char*> files)
{
    std::vector<path> dropped;
    dropped.reserve(files.size());
    for (const char* file : files)
        dropped.emplace_back(file);
    return dropped;
}

/// @brief What the drop target does with a drop of @p files.
Slic3r::Biz::FileLoadingLogic::DropRouting route(std::vector<path> files)
{
    return route_dropped_files(files);
}

} // namespace

TEST_CASE("is_resin_profile_file takes the foreign profile formats", "[resin_import][routing]")
{
    SECTION("the three profile extensions")
    {
        CHECK(is_resin_profile_file("orange.cfg"));
        CHECK(is_resin_profile_file("orange.cfgx"));
        CHECK(is_resin_profile_file("orange.lyr"));
    }

    SECTION("the extension list is what the routing matches")
    {
        CHECK(get_resin_profile_extensions().size() == 3);
        for (const std::string& ext : get_resin_profile_extensions()) {
            CHECK(is_resin_profile_file("orange" + ext));
        }
    }

    SECTION("a full path and an upper-case extension")
    {
        CHECK(is_resin_profile_file("C:/Users/someone/Desktop/Orange Resin.CFGX"));
        CHECK(is_resin_profile_file("/home/someone/profiles/orange.Lyr"));
    }

    SECTION("a model or a project is not a resin profile")
    {
        CHECK_FALSE(is_resin_profile_file("part.stl"));
        CHECK_FALSE(is_resin_profile_file("part.3mf"));
        CHECK_FALSE(is_resin_profile_file("part.obj"));
        CHECK_FALSE(is_resin_profile_file("part.step"));
    }

    SECTION("a sliced archive keeps loading as a project")
    {
        CHECK_FALSE(is_resin_profile_file("print.sl1"));
        CHECK_FALSE(is_resin_profile_file("print.sl1s"));
        CHECK(is_supported_file("print.sl1"));
        CHECK(is_supported_file("print.sl1s"));
    }

    SECTION("only the extension counts")
    {
        CHECK_FALSE(is_resin_profile_file(""));
        CHECK_FALSE(is_resin_profile_file("cfg"));
        CHECK_FALSE(is_resin_profile_file("orange.cfg.txt"));
        CHECK_FALSE(is_resin_profile_file("orange"));
    }
}

TEST_CASE("a resin profile is not a model file", "[resin_import][routing]")
{
    // The profile formats are deliberately kept out of get_import_extensions(): the
    // File > Import dialog and the model loader have nothing to do with a resin profile.
    for (const std::string& ext : get_resin_profile_extensions()) {
        CHECK_FALSE(is_supported_file("orange" + ext));
    }
    CHECK(is_supported_file("part.stl"));
}

TEST_CASE("a drop is split into what is loaded and what is reviewed", "[resin_import][routing]")
{
    SECTION("a drop of models loads all of them and reviews nothing")
    {
        const auto routing = route(drop({"part.stl", "bracket.obj", "print.3mf"}));
        CHECK(routing.files_to_load.size() == 3);
        CHECK(routing.files_to_load[0].string() == "part.stl");
        CHECK(routing.files_to_load[1].string() == "bracket.obj");
        CHECK(routing.files_to_load[2].string() == "print.3mf");
        CHECK(routing.profile_to_review.empty());
        CHECK(routing.profiles_left_out.empty());
    }

    SECTION("a profile alone goes to the review dialog")
    {
        const auto routing = route(drop({"orange.cfg"}));
        CHECK(routing.files_to_load.empty());
        CHECK(routing.profile_to_review.string() == "orange.cfg");
        CHECK(routing.profiles_left_out.empty());
    }

    SECTION("a model dropped with a profile is loaded, not left out")
    {
        // The models go to the scene first and the review dialog is opened after them, so the
        // dialog is the last thing the drop leaves on the screen.
        const auto routing = route(drop({"part.stl", "orange.lyr"}));
        CHECK(routing.files_to_load.size() == 1);
        CHECK(routing.files_to_load[0].string() == "part.stl");
        CHECK(routing.profile_to_review.string() == "orange.lyr");
        CHECK(routing.profiles_left_out.empty());
    }

    SECTION("a drop that holds no profile loads exactly as it did before the split")
    {
        const auto routing = route(drop({"part.stl", "print.sl1"}));
        CHECK(routing.files_to_load.size() == 2);
        CHECK(routing.profile_to_review.empty());
        CHECK(routing.profiles_left_out.empty());
    }

    SECTION("only the first profile is reviewed, the rest are named to the user")
    {
        const auto routing = route(drop({"blue.cfg", "green.cfgx", "orange.lyr"}));
        CHECK(routing.profile_to_review.string() == "blue.cfg");
        CHECK(routing.profiles_left_out.size() == 2);
        CHECK(routing.profiles_left_out[0].string() == "green.cfgx");
        CHECK(routing.profiles_left_out[1].string() == "orange.lyr");
    }

    SECTION("the order of the drop decides which profile is the first one")
    {
        const auto routing = route(drop({"green.cfgx", "part.stl", "blue.cfg"}));
        CHECK(routing.profile_to_review.string() == "green.cfgx");
        CHECK(routing.profiles_left_out.size() == 1);
        CHECK(routing.profiles_left_out[0].string() == "blue.cfg");
        CHECK(routing.files_to_load.size() == 1);
    }

    SECTION("a sliced archive is a project, not a profile, next to a profile")
    {
        const auto routing = route(drop({"print.sl1", "orange.cfg"}));
        CHECK(routing.files_to_load.size() == 1);
        CHECK(routing.files_to_load[0].string() == "print.sl1");
        CHECK(routing.profile_to_review.string() == "orange.cfg");
    }

    SECTION("a file of a type nothing reads is left out of every list")
    {
        const auto routing = route(drop({"notes.txt", "orange.cfg"}));
        CHECK(routing.files_to_load.empty());
        CHECK(routing.profile_to_review.string() == "orange.cfg");
        CHECK(routing.profiles_left_out.empty());
    }

    SECTION("an empty drop is nothing at all")
    {
        const auto routing = route(drop({}));
        CHECK(routing.files_to_load.empty());
        CHECK(routing.profile_to_review.empty());
        CHECK(routing.profiles_left_out.empty());
    }
}
