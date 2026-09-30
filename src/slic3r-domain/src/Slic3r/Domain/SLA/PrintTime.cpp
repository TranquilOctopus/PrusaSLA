#include "Slic3r/Domain/SLA/PrintTime.hpp"

#include "Slic3r/Domain/SlaLayerHeight.hpp"

#include <algorithm>
#include <string>

namespace Slic3r::Domain {

namespace {

// ConfigView::get() asserts on unknown keys, but a view may carry only a subset of the options
// (a printer-only view has no resin settings), so the lookups are lenient, as in
// SlaLayerHeight.cpp and in the exporters.
double lenient_double(const ConfigView& cfg, const std::string& key)
{
    const auto& values{cfg.values()};
    const auto  it{values.find(key)};
    if (it == values.end() || !it->second.holds_alternative<double>())
        return 0.;
    return it->second.get<double>();
}

// One move of the build plate: a distance at a speed. A speed of zero means the stage is not
// set, and an unset stage takes no time rather than dividing by zero.
double move_s(double distance_mm, double speed_mm_s)
{
    if (distance_mm <= 0. || speed_mm_s <= 0.)
        return 0.;
    return distance_mm / speed_mm_s;
}

// The separation of one layer: the lift up, the retract back down, and the second stage of each
// when the print sets one. The *_2 settings hold the second stage, whose default of 0 is the
// single-stage separation of every printer that does not use the two-stage lift.
double separation_s(double lift_mm, double lift_speed, double retract_speed,
                    double second_lift_mm, double second_lift_speed, double second_retract_speed)
{
    return move_s(lift_mm, lift_speed) + move_s(lift_mm, retract_speed)
         + move_s(second_lift_mm, second_lift_speed) + move_s(second_lift_mm, second_retract_speed);
}

} // namespace

std::vector<double> SlaPrintTimeEstimate::running_total_s() const
{
    std::vector<double> ret;
    ret.reserve(layers.size());
    double running = 0.;
    for (const SlaLayerTime& layer : layers) {
        running += layer.total_s();
        ret.push_back(running);
    }
    return ret;
}

SlaPrintTimeEstimate sla_estimate_print_time(const ConfigView& cfg, int layer_count)
{
    SlaPrintTimeEstimate ret;

    if (layer_count <= 0)
        return ret;

    const double normal_exposure = lenient_double(cfg, "exposure_time");
    const double bottom_exposure = lenient_double(cfg, "initial_exposure_time");
    const int    bottom_layers = std::min(std::max(0, sla_bottom_layer_count(cfg)), layer_count);
    const int    transition_layers = std::max(0, sla_effective_faded_layers(cfg));
    // The engine exposes the first layer at the bottom exposure and fades to the normal one over
    // the transition layers plus that first one (merge_slices_and_eval_stats()), so the step of
    // the ramp is the difference spread over that many layers. An initial exposure at or below
    // the normal one is no ramp at all.
    const double fade_step = bottom_exposure > normal_exposure
        ? (bottom_exposure - normal_exposure) / (transition_layers + 1)
        : 0.;

    const RaftInterface raft_interface = sla_raft_interface(cfg, layer_count);

    const double light_off_s = lenient_double(cfg, "wait_before_lift")
                             + lenient_double(cfg, "wait_after_lift")
                             + lenient_double(cfg, "wait_after_retract");
    const double bottom_light_off_s = lenient_double(cfg, "bottom_wait_before_lift")
                                   + lenient_double(cfg, "bottom_wait_after_lift")
                                   + lenient_double(cfg, "bottom_wait_after_retract");

    // There is no retract distance setting, so the plate returns over the distance it was lifted.
    const double motion_s = separation_s(lenient_double(cfg, "lift_height"),
                                         lenient_double(cfg, "lift_speed"),
                                         lenient_double(cfg, "retract_speed"),
                                         lenient_double(cfg, "lift_height_2"),
                                         lenient_double(cfg, "lift_speed_2"),
                                         lenient_double(cfg, "retract_speed_2"));
    const double bottom_motion_s = separation_s(lenient_double(cfg, "bottom_lift_height"),
                                                lenient_double(cfg, "bottom_lift_speed"),
                                                lenient_double(cfg, "bottom_retract_speed"),
                                                lenient_double(cfg, "bottom_lift_height_2"),
                                                lenient_double(cfg, "bottom_lift_speed_2"),
                                                lenient_double(cfg, "bottom_retract_speed_2"));

    ret.layers.reserve(static_cast<std::size_t>(layer_count));
    for (int i = 0; i < layer_count; ++i) {
        const bool bottom = i < bottom_layers;
        const auto index = static_cast<std::size_t>(i);

        double exposure = normal_exposure;
        if (bottom) {
            exposure = bottom_exposure;
        } else if (raft_interface.contains(index)) {
            // The band never reaches into the burn-in (sla_raft_interface), so this is not a
            // layer that is both.
            exposure = raft_interface.exposure_for(normal_exposure);
        } else {
            exposure = std::max(normal_exposure, bottom_exposure - i * fade_step);
        }

        ret.layers.push_back(SlaLayerTime{
            .exposure_s = exposure,
            .light_off_s = bottom ? bottom_light_off_s : light_off_s,
            .motion_s   = bottom ? bottom_motion_s : motion_s,
        });
    }

    for (const SlaLayerTime& layer : ret.layers)
        ret.total_s += layer.total_s();
    ret.valid = true;

    return ret;
}

} // namespace Slic3r::Domain
