#include <catch2/catch_test_macros.hpp>

#include <Slic3r/App/Preview/SlaLayerViewCache.hpp>

using namespace Slic3r::App::Preview;

TEST_CASE("SlaLayerViewCache - basic put and get", "[SlaLayerViewCache]")
{
    SlaLayerViewCache cache(4);

    std::vector<uint8_t> img1 = {1, 2, 3, 4};
    cache.put(0, std::move(img1), 2, 2);

    auto entry = cache.get(0);
    REQUIRE(entry.has_value());
    REQUIRE(entry->layer_index == 0);
    REQUIRE(entry->width == 2);
    REQUIRE(entry->height == 2);
    REQUIRE(entry->image_data == std::vector<uint8_t>{1, 2, 3, 4});
}

TEST_CASE("SlaLayerViewCache - miss returns nullopt", "[SlaLayerViewCache]")
{
    SlaLayerViewCache cache(4);

    auto entry = cache.get(5);
    REQUIRE_FALSE(entry.has_value());
}

TEST_CASE("SlaLayerViewCache - eviction policy LRU", "[SlaLayerViewCache]")
{
    SlaLayerViewCache cache(3);

    cache.put(0, std::vector<uint8_t>{0}, 1, 1);
    cache.put(1, std::vector<uint8_t>{1}, 1, 1);
    cache.put(2, std::vector<uint8_t>{2}, 1, 1);

    REQUIRE(cache.get(0).has_value());
    REQUIRE(cache.get(1).has_value());
    REQUIRE(cache.get(2).has_value());

    cache.put(3, std::vector<uint8_t>{3}, 1, 1);

    REQUIRE_FALSE(cache.get(0).has_value());
    REQUIRE(cache.get(1).has_value());
    REQUIRE(cache.get(2).has_value());
    REQUIRE(cache.get(3).has_value());
}

TEST_CASE("SlaLayerViewCache - access updates LRU order", "[SlaLayerViewCache]")
{
    SlaLayerViewCache cache(3);

    cache.put(0, std::vector<uint8_t>{0}, 1, 1);
    cache.put(1, std::vector<uint8_t>{1}, 1, 1);
    cache.put(2, std::vector<uint8_t>{2}, 1, 1);

    cache.get(0);

    cache.put(3, std::vector<uint8_t>{3}, 1, 1);

    REQUIRE(cache.get(0).has_value());
    REQUIRE_FALSE(cache.get(1).has_value());
    REQUIRE(cache.get(2).has_value());
    REQUIRE(cache.get(3).has_value());
}

TEST_CASE("SlaLayerViewCache - update existing entry", "[SlaLayerViewCache]")
{
    SlaLayerViewCache cache(4);

    cache.put(0, std::vector<uint8_t>{0}, 1, 1);
    cache.put(0, std::vector<uint8_t>{99}, 2, 2);

    auto entry = cache.get(0);
    REQUIRE(entry.has_value());
    REQUIRE(entry->width == 2);
    REQUIRE(entry->height == 2);
    REQUIRE(entry->image_data == std::vector<uint8_t>{99});
}

TEST_CASE("SlaLayerViewCache - clear removes all entries", "[SlaLayerViewCache]")
{
    SlaLayerViewCache cache(4);

    cache.put(0, std::vector<uint8_t>{0}, 1, 1);
    cache.put(1, std::vector<uint8_t>{1}, 1, 1);
    cache.clear();

    REQUIRE_FALSE(cache.get(0).has_value());
    REQUIRE_FALSE(cache.get(1).has_value());
}

TEST_CASE("sla_layer_index_from_slider_pos - clamps to max", "[SlaLayerViewCache]")
{
    REQUIRE(sla_layer_index_from_slider_pos(5, 10) == 5);
    REQUIRE(sla_layer_index_from_slider_pos(15, 10) == 10);
    REQUIRE(sla_layer_index_from_slider_pos(-1, 10) == 0);
}

TEST_CASE("sla_layer_index_from_slider_pos - handles zero max", "[SlaLayerViewCache]")
{
    REQUIRE(sla_layer_index_from_slider_pos(0, 0) == 0);
    REQUIRE(sla_layer_index_from_slider_pos(5, 0) == 0);
}