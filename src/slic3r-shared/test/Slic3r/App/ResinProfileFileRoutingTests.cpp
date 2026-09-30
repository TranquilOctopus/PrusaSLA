#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/FileLoadingLogic.hpp"

#include <string>

using Slic3r::Biz::FileLoadingLogic::get_resin_profile_extensions;
using Slic3r::Biz::FileLoadingLogic::is_resin_profile_file;
using Slic3r::Biz::FileLoadingLogic::is_supported_file;

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
