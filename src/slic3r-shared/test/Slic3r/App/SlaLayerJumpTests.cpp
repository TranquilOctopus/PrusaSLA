#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/SlaLayerJump.hpp"

using Slic3r::App::SlaLayerJump;
using Slic3r::Domain::SlicingId;

namespace {

SlicingId
make_id(Slic3r::Domain::SelectionId project_id, Slic3r::Domain::SelectionId bed_instance_id)
{
    return SlicingId{project_id, bed_instance_id};
}

} // namespace

TEST_CASE("SlaLayerJump - nothing requested", "[SlaLayerJump]")
{
    SlaLayerJump jump;

    CHECK(jump.empty());
    CHECK_FALSE(jump.take(make_id(1, 1)).has_value());
}

TEST_CASE("SlaLayerJump - a request is answered once", "[SlaLayerJump]")
{
    SlaLayerJump jump;
    jump.request(make_id(1, 2), 218);

    CHECK_FALSE(jump.empty());
    const std::optional<size_t> layer = jump.take(make_id(1, 2));
    REQUIRE(layer.has_value());
    CHECK(*layer == 218);
    CHECK(jump.empty());
    CHECK_FALSE(jump.take(make_id(1, 2)).has_value());
}

TEST_CASE("SlaLayerJump - the request waits for the window of its own bed", "[SlaLayerJump]")
{
    SlaLayerJump jump;
    jump.request(make_id(1, 2), 218);

    // Another project or another bed of the same project cannot answer it.
    CHECK_FALSE(jump.take(make_id(2, 2)).has_value());
    CHECK_FALSE(jump.take(make_id(1, 3)).has_value());
    CHECK_FALSE(jump.empty());

    const std::optional<size_t> layer = jump.take(make_id(1, 2));
    REQUIRE(layer.has_value());
    CHECK(*layer == 218);
}

TEST_CASE("SlaLayerJump - the last request wins", "[SlaLayerJump]")
{
    SlaLayerJump jump;
    jump.request(make_id(1, 2), 10);
    jump.request(make_id(1, 2), 11);

    const std::optional<size_t> layer = jump.take(make_id(1, 2));
    REQUIRE(layer.has_value());
    CHECK(*layer == 11);
}

TEST_CASE("SlaLayerJump - a request is dropped when the user leaves the bed", "[SlaLayerJump]")
{
    SlaLayerJump jump;
    jump.request(make_id(1, 2), 218);

    jump.clear();

    CHECK(jump.empty());
    CHECK_FALSE(jump.take(make_id(1, 2)).has_value());
}

TEST_CASE("SlaLayerJump - layer zero is a request too", "[SlaLayerJump]")
{
    SlaLayerJump jump;
    jump.request(make_id(1, 2), 0);

    const std::optional<size_t> layer = jump.take(make_id(1, 2));
    REQUIRE(layer.has_value());
    CHECK(*layer == 0);
}
