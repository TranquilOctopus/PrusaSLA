#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/DisplayStrings.hpp"
#include "Slic3r/Biz/Slicing/SlicingStatus.hpp"

using namespace Slic3r::App;
using namespace Slic3r::Biz::Slicing;

TEST_CASE("to_display_string for new ErrorCode values", "[display-strings]")
{
    CHECK(to_display_string(ErrorCode::OutOfMemory) ==
          "Not enough memory to slice this bed. Try fewer or smaller objects, or close other programs, then slice again.");

    CHECK(to_display_string(ErrorCode::InternalError) ==
          "Slicing failed because of an internal error. The details are in the log.");
}