#pragma once

#include "Slic3r/App/Yoga/Item.hpp"

#include "Slic3r/Biz/ConfigItemContext.hpp"

#include <string>
#include <vector>

namespace Slic3r::Biz {
class IConfigBoxSetter;
} // namespace Slic3r::Biz

namespace Slic3r::App {

class ConfigRowItem;

/**
 * @brief A fixed list of config keys edited in place, one ConfigRowItem per key.
 *
 * ConfigRowItem is the very row the full settings dialogs build for a single config item, so an
 * edit made here goes through the same IConfigBoxSetter (the PresetInteractor) and therefore
 * marks the preset dirty, is saved and is discarded exactly like an edit in those dialogs. Keys
 * the bound config boxes do not have are skipped.
 */
class SlaSettingsRows : public Yoga::Item
{
public:
    struct Key {
        std::string key;
        /// Empty to use the label of the config def. A non empty label replaces it.
        std::string label;
        /// Prefix the label with the row group of the def, e.g. "Head diameter (Light)".
        bool label_with_row_group{false};
    };

    SlaSettingsRows(std::vector<Key> keys, Biz::IConfigBoxSetter& cb_setter);

    /// Binds the rows to the config items of the currently selected preset and builds them.
    void set_items(const std::vector<Biz::ConfigItemContext>* items);

    /// Re-reads the values of the bound items into the already built rows.
    void refresh();

private:
    const Biz::ConfigItemContext* find_item(const std::string& key) const;
    /// The item of that key, or nullptr when it is missing or hidden.
    const Biz::ConfigItemContext* shown_item(const std::string& key) const;
    bool is_dirty(size_t row_index) const;
    void rebuild();

private:
    std::vector<Key> m_keys;
    Biz::IConfigBoxSetter& m_cb_setter;
    const std::vector<Biz::ConfigItemContext>* m_items{nullptr};
    std::vector<ConfigRowItem*> m_rows;
    /// Index into m_keys for every built row, so the rows survive a repopulation of m_items.
    std::vector<size_t> m_row_keys;
};

} // namespace Slic3r::App
