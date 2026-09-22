#include "Slic3r/App/Preview/SlaLayerViewCache.hpp"

#include <algorithm>

namespace Slic3r::App::Preview {

SlaLayerViewCache::SlaLayerViewCache(size_t capacity) : m_capacity(capacity) {}

void SlaLayerViewCache::set_capacity(size_t capacity)
{
    m_capacity = capacity;
    if (m_entries.size() > m_capacity) {
        std::sort(m_entries.begin(), m_entries.end(),
            [](const Entry& a, const Entry& b) { return a.access_order < b.access_order; });
        m_entries.resize(m_capacity);
    }
}

std::optional<SlaLayerViewCache::CacheEntry> SlaLayerViewCache::get(size_t layer_index) const
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
        [layer_index](const Entry& e) { return e.layer_index == layer_index; });

    if (it == m_entries.end())
        return std::nullopt;

    CacheEntry result;
    result.layer_index = it->layer_index;
    result.image_data = it->image_data;
    result.width = it->width;
    result.height = it->height;
    return result;
}

void SlaLayerViewCache::put(size_t layer_index, std::vector<uint8_t>&& image_data, unsigned width, unsigned height)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
        [layer_index](const Entry& e) { return e.layer_index == layer_index; });

    if (it != m_entries.end()) {
        it->image_data = std::move(image_data);
        it->width = width;
        it->height = height;
        it->access_order = m_access_counter++;
        return;
    }

    if (m_entries.size() >= m_capacity) {
        auto oldest_it = std::min_element(m_entries.begin(), m_entries.end(),
            [](const Entry& a, const Entry& b) { return a.access_order < b.access_order; });
        m_entries.erase(oldest_it);
    }

    Entry new_entry;
    new_entry.layer_index = layer_index;
    new_entry.image_data = std::move(image_data);
    new_entry.width = width;
    new_entry.height = height;
    new_entry.access_order = m_access_counter++;
    m_entries.push_back(std::move(new_entry));
}

void SlaLayerViewCache::clear()
{
    m_entries.clear();
    m_access_counter = 0;
}

size_t sla_layer_index_from_slider_pos(int slider_pos, size_t max_layer_index)
{
    if (max_layer_index == 0)
        return 0;

    size_t clamped = static_cast<size_t>(slider_pos);
    if (clamped > max_layer_index)
        clamped = max_layer_index;
    return clamped;
}

} // namespace Slic3r::App::Preview