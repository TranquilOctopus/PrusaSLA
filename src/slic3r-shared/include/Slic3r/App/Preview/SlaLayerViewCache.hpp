#pragma once

#include <vector>
#include <optional>
#include <cstddef>

namespace Slic3r::App::Preview {

class SlaLayerViewCache
{
public:
    explicit SlaLayerViewCache(size_t capacity = 4);

    void set_capacity(size_t capacity);

    struct CacheEntry
    {
        size_t layer_index;
        std::vector<uint8_t> image_data;
        unsigned width;
        unsigned height;
    };

    std::optional<CacheEntry> get(size_t layer_index) const;
    void put(size_t layer_index, std::vector<uint8_t>&& image_data, unsigned width, unsigned height);

    void clear();

private:
    struct Entry
    {
        size_t layer_index;
        std::vector<uint8_t> image_data;
        unsigned width;
        unsigned height;
        size_t access_order;
    };

    std::vector<Entry> m_entries;
    size_t m_capacity;
    size_t m_access_counter = 0;
};

size_t sla_layer_index_from_slider_pos(int slider_pos, size_t max_layer_index);

} // namespace Slic3r::App::Preview