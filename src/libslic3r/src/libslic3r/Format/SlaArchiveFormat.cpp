#include "SlaArchiveFormat.hpp"

#include <algorithm>
#include <cctype>

namespace Slic3r::sla {

SlaArchiveFormatRegistry& SlaArchiveFormatRegistry::instance()
{
    static SlaArchiveFormatRegistry registry;
    return registry;
}

void SlaArchiveFormatRegistry::register_format(const std::string& name, CreatorFn creator)
{
    auto fmt = creator();
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    std::vector<std::string> lower_exts;
    for (const auto& ext : fmt->extensions()) {
        std::string lower_ext = ext;
        std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        lower_exts.push_back(std::move(lower_ext));
    }

    m_formats.emplace(std::move(lower_name), Entry{std::move(creator), std::move(lower_exts)});
}

std::unique_ptr<ISlaArchiveFormat> SlaArchiveFormatRegistry::get(const std::string& name) const
{
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    auto it = m_formats.find(lower_name);
    if (it == m_formats.end())
        return nullptr;
    return it->second.creator();
}

std::vector<std::string> SlaArchiveFormatRegistry::names() const
{
    std::vector<std::string> result;
    result.reserve(m_formats.size());
    for (const auto& [name, entry] : m_formats) {
        result.push_back(name);
    }
    return result;
}

std::unique_ptr<ISlaArchiveFormat> SlaArchiveFormatRegistry::find_by_extension(const std::string& ext) const
{
    std::string lower_ext = ext;
    std::transform(lower_ext.begin(), lower_ext.end(), lower_ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    for (const auto& [name, entry] : m_formats) {
        for (const auto& e : entry.extensions) {
            if (e == lower_ext)
                return entry.creator();
        }
    }
    return nullptr;
}

} // namespace Slic3r::sla