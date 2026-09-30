#pragma once

#include <cstddef>

#include "Slic3r/Domain/Config.hpp"

namespace Slic3r::Domain {

/// Layer height the SLA pipeline must use: the resin's own layer height when the resin
/// defines one, otherwise the print preset's layer height (Supports & raft).
/// @param cfg A finalized config view. Options missing from the view count as 0, so a
///             printer-only view (no resin settings) falls back to "layer_height".
/// @return The effective layer height in mm, or 0 when neither option is present.
double sla_effective_layer_height(const ConfigView& cfg);

/// Number of layers over which the SLA pipeline fades the exposure from initial_exposure_time
/// down to exposure_time: the resin's own transition layer count when the resin defines one,
/// otherwise the print preset's "faded_layers" (Supports & raft).
/// @param cfg A finalized config view. Options missing from the view count as "not set", so a
///             printer-only view (no resin settings) falls back to "faded_layers".
/// @return The effective transition layer count, or 0 when neither option is present.
int sla_effective_faded_layers(const ConfigView& cfg);

/// Number of layers an exported file has to expose at initial_exposure_time. The engine has no
/// burn-in count of its own: it exposes the first layer at initial_exposure_time and fades to
/// exposure_time over the transition layers, so the default is the transition layers plus that
/// first one. A resin that states its own burn-in (bottom_layer_count, the number of layers
/// using the bottom_* parameters, which the engine does not read) wins over that default.
/// @return The burn-in layer count, never negative.
int sla_bottom_layer_count(const ConfigView& cfg);

/// The band of layers the raft interface is printed as: the skin between the raft and the object,
/// with a thickness of its own and, where the file format has room for one, an exposure of its own.
struct RaftInterface
{
    /// First layer of the band, counted from the build plate.
    int first_layer = 0;
    /// Last layer of the band, -1 when there is no band.
    int last_layer  = -1;
    /// Exposure of the band in seconds, 0 when it is exposed like the rest of the print.
    double exposure_s = 0.;

    /// How many layers the band has, 0 when it is off.
    int count() const { return last_layer < first_layer ? 0 : last_layer - first_layer + 1; }
    bool empty() const { return count() == 0; }

    /// Whether the given layer, counted from the build plate, is one of the interface layers.
    bool contains(std::size_t layer) const
    {
        return !empty() && layer >= static_cast<std::size_t>(first_layer)
            && layer <= static_cast<std::size_t>(last_layer);
    }

    /// The exposure of the band, or the given normal exposure when it brings none of its own.
    double exposure_for(double normal_exposure_s) const
    {
        return exposure_s > 0. ? exposure_s : normal_exposure_s;
    }

    /// The exposure of one layer: the interface exposure inside the band, the normal one outside.
    double layer_exposure_s(std::size_t layer, double normal_exposure_s) const
    {
        return contains(layer) ? exposure_for(normal_exposure_s) : normal_exposure_s;
    }

    /// What the band spends on top of the normal exposure over the whole print, for the print time
    /// the formats write. Zero while the interface is off, which leaves that print time as it was.
    double print_time_delta_s(double normal_exposure_s) const
    {
        return empty() ? 0. : count() * (exposure_for(normal_exposure_s) - normal_exposure_s);
    }
};

/// The layers the raft interface covers: the top that many millimetres of the raft, rounded to
/// whole layers. A thickness of zero, a raft of no height or a layer height of zero gives an
/// empty band, which is the raft as it prints without an interface.
/// @param raft_height_mm Height of the whole raft from the build plate in mm.
/// @param interface_thickness_mm How deep the interface reaches down into the raft in mm.
/// @param layer_height_mm Layer height of the print in mm.
/// @return The band, empty when the interface is off.
RaftInterface raft_interface_band(double raft_height_mm,
                                  double interface_thickness_mm,
                                  double layer_height_mm);

/// The raft interface of a print, read from the config: the band of raft_interface_band() for the
/// raft the config describes (its floor thickness, or its wall thickness when the floor has none of
/// its own, plus the height of its cavity), with
/// raft_interface_exposure as the exposure of the band. A band that reaches into the bottom layers
/// is cut back to them, because the bottom exposure wins there: the burn-in is what holds the raft
/// to the build plate, so it is not the interface's to decide.
/// @param cfg A finalized config view. Options missing from the view count as 0, so a view with no
///             raft settings (a printer-only view) has no interface.
/// @param layer_count How many layers the file has, or a negative number when that is not known;
///                   the band is cut to it either way.
/// @return The band, empty when the interface is off or reaches nowhere but the bottom layers.
RaftInterface sla_raft_interface(const ConfigView& cfg, int layer_count = -1);

} // namespace Slic3r::Domain
