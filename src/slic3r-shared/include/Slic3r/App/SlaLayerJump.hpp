#pragma once

#include <optional>

#include "Slic3r/Domain/SlicingId.hpp"

namespace Slic3r::App {

/**
 * @brief A pending "show this layer" request for the SLA layer image window.
 *
 * The sidebar that lists the issues of a slice and the layer image window live in two
 * different render modules and neither knows the other, so a click in the list cannot be a
 * direct call: the sidebar posts the request here and the window picks it up while it
 * updates. The request names the build plate it belongs to, so a request made for one bed is
 * never answered by the window of another one, and it stays pending until the window of that
 * bed can honour it.
 */
class SlaLayerJump
{
public:
    /// Ask the layer image window to show @p layer of the given build plate.
    void request(const Domain::SlicingId& slicing_id, size_t layer)
    {
        m_slicing_id = slicing_id;
        m_layer      = layer;
    }

    /**
     * @brief The layer of the pending request of @p slicing_id, clearing the request.
     *
     * Empty when nothing was requested, or when the pending request belongs to another build
     * plate, in which case it is left pending.
     */
    std::optional<size_t> take(const Domain::SlicingId& slicing_id)
    {
        if (!m_layer || !m_slicing_id || !(*m_slicing_id == slicing_id)) {
            return std::nullopt;
        }
        const size_t layer = *m_layer;
        clear();
        return layer;
    }

    void clear()
    {
        m_slicing_id = std::nullopt;
        m_layer      = std::nullopt;
    }

    bool empty() const
    {
        return !m_layer;
    }

private:
    std::optional<Domain::SlicingId> m_slicing_id;
    std::optional<size_t> m_layer;
};

} // namespace Slic3r::App
