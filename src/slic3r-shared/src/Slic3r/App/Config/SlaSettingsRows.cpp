#include "Slic3r/App/Config/SlaSettingsRows.hpp"

#include "Slic3r/App/Config/ConfigRowItem.hpp"
#include "Slic3r/App/Yoga/Namespace.hpp"

#include "Slic3r/Biz/IConfigBoxSetter.hpp"
#include "Slic3r/Biz/I18N/I18N.hpp"

using namespace Slic3r::App::Yoga;

namespace Slic3r::App {

using Yoga::operator""_fpx;

SlaSettingsRows::SlaSettingsRows(std::vector<Key> keys, Biz::IConfigBoxSetter& cb_setter) :
    Yoga::Item(), m_keys(std::move(keys)), m_cb_setter(cb_setter)
{
    set_object_name("SlaSettingsRows");
    set_orientation(Orientation::Vertical);
    set_gap(5_fpx);
    set_flex_shrink(0);
}

void SlaSettingsRows::set_items(const std::vector<Biz::ConfigItemContext>* items)
{
    m_items = items;
    rebuild();
}

void SlaSettingsRows::refresh()
{
    size_t wanted = 0;
    if (m_items != nullptr) {
        for (const Key& key : m_keys) {
            if (shown_item(key.key) != nullptr) {
                ++wanted;
            }
        }
    }

    if (wanted != m_rows.size()) {
        // A key appeared or disappeared, only this happens when the printer or the preset changed
        rebuild();
        return;
    }

    size_t index = 0;
    for (const Key& key : m_keys) {
        const Biz::ConfigItemContext* context = shown_item(key.key);
        if (context == nullptr) {
            continue;
        }
        m_rows.at(index)->set_state(*context->config_item);
        ++index;
    }
}

const Biz::ConfigItemContext* SlaSettingsRows::find_item(const std::string& key) const
{
    if (m_items == nullptr) {
        return nullptr;
    }
    for (const Biz::ConfigItemContext& context : *m_items) {
        if (context.name == key) {
            return &context;
        }
    }
    return nullptr;
}

const Biz::ConfigItemContext* SlaSettingsRows::shown_item(const std::string& key) const
{
    const Biz::ConfigItemContext* context = find_item(key);
    if (context == nullptr || context->config_item == nullptr) {
        return nullptr;
    }
    // A hidden setting is not shown by any other settings panel either, so it is not shown here.
    if (context->config_item->def().category == Domain::ConfigItemDef::Category::Hidden) {
        return nullptr;
    }
    return context;
}

bool SlaSettingsRows::is_dirty(size_t row_index) const
{
    if (row_index >= m_row_keys.size()) {
        return false;
    }
    const Biz::ConfigItemContext* context = shown_item(m_keys.at(m_row_keys.at(row_index)).key);
    if (context == nullptr) {
        return false;
    }
    return context->original_config_item != nullptr
        && context->config_item->value() != context->original_config_item->value();
}

void SlaSettingsRows::rebuild()
{
    for (ConfigRowItem* row : m_rows) {
        remove(row);
    }
    m_rows.clear();
    m_row_keys.clear();

    if (m_items == nullptr) {
        return;
    }

    for (size_t key_index = 0; key_index < m_keys.size(); ++key_index) {
        const Key& key                        = m_keys.at(key_index);
        const Biz::ConfigItemContext* context = shown_item(key.key);
        if (context == nullptr) {
            continue;
        }

        std::optional<std::string> label;
        if (!key.label.empty()) {
            label = key.label;
        } else if (key.label_with_row_group && !context->config_item->def().row_group.empty()) {
            label = Biz::_u8(context->config_item->def().row_group) + " ("
                  + Biz::_u8(context->config_item->def().label) + ")";
        }

        const size_t row_index = m_row_keys.size();
        m_row_keys.push_back(key_index);
        ConfigRowItem* row = emplace_back<ConfigRowItem>(
            0,
            *context->config_item,
            m_cb_setter,
            [this, row_index]() { return is_dirty(row_index); },
            0,
            label
        );
        m_rows.push_back(row);
    }
}

} // namespace Slic3r::App
