#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <libslic3r/SLAPrint.hpp>
#include <Slic3r/Domain/ConfigDefsSLA.hpp>

using namespace Slic3r;
using namespace SLASlicingSync;
using Catch::Approx;

namespace {

std::vector<std::string> get_all_sla_config_keys() {
    std::vector<std::string> keys;

    keys.push_back("absolute_correction");
    keys.push_back("area_fill");
    keys.push_back("bed_custom_model");
    keys.push_back("bed_custom_texture");
    keys.push_back("bed_shape");
    keys.push_back("bottle_cost");
    keys.push_back("bottle_volume");
    keys.push_back("bottle_weight");
    keys.push_back("branchingsupport_base_diameter");
    keys.push_back("branchingsupport_base_height");
    keys.push_back("branchingsupport_base_safety_distance");
    keys.push_back("branchingsupport_buildplate_only");
    keys.push_back("branchingsupport_critical_angle");
    keys.push_back("branchingsupport_head_front_diameter");
    keys.push_back("branchingsupport_head_penetration");
    keys.push_back("branchingsupport_head_width");
    keys.push_back("branchingsupport_max_bridge_length");
    keys.push_back("branchingsupport_max_bridges_on_pillar");
    keys.push_back("branchingsupport_max_pillar_link_distance");
    keys.push_back("branchingsupport_max_weight_on_model");
    keys.push_back("branchingsupport_object_elevation");
    keys.push_back("branchingsupport_pillar_connection_mode");
    keys.push_back("branchingsupport_pillar_diameter");
    keys.push_back("branchingsupport_pillar_widening_factor");
    keys.push_back("branchingsupport_small_pillar_diameter_percent");
    keys.push_back("delay_after_exposure");
    keys.push_back("delay_before_exposure");
    keys.push_back("display_height");
    keys.push_back("display_mirror_x");
    keys.push_back("display_mirror_y");
    keys.push_back("display_orientation");
    keys.push_back("display_pixels_x");
    keys.push_back("display_pixels_y");
    keys.push_back("display_width");
    keys.push_back("elefant_foot_compensation");
    keys.push_back("elefant_foot_min_width");
    keys.push_back("exposure_time");
    keys.push_back("faded_layers");
    keys.push_back("fast_tilt_time");
    keys.push_back("gamma_correction");
    keys.push_back("high_viscosity_tilt_time");
    keys.push_back("hollowing_closing_distance");
    keys.push_back("hollowing_enable");
    keys.push_back("hollowing_min_thickness");
    keys.push_back("hollowing_quality");
    keys.push_back("initial_exposure_time");
    keys.push_back("initial_layer_height");
    keys.push_back("layer_height");
    keys.push_back("material_colour");
    keys.push_back("material_correction");
    keys.push_back("material_correction_x");
    keys.push_back("material_correction_y");
    keys.push_back("material_correction_z");
    keys.push_back("material_density");
    keys.push_back("material_notes");
    keys.push_back("material_print_speed");
    keys.push_back("material_type");
    keys.push_back("material_vendor");
    keys.push_back("max_exposure_time");
    keys.push_back("max_initial_exposure_time");
    keys.push_back("max_print_height");
    keys.push_back("min_exposure_time");
    keys.push_back("min_initial_exposure_time");
    keys.push_back("output_filename_format");
    keys.push_back("pad_around_object");
    keys.push_back("pad_around_object_everywhere");
    keys.push_back("pad_brim_size");
    keys.push_back("pad_enable");
    keys.push_back("pad_max_merge_distance");
    keys.push_back("pad_object_connector_penetration");
    keys.push_back("pad_object_connector_stride");
    keys.push_back("pad_object_connector_width");
    keys.push_back("pad_object_gap");
    keys.push_back("pad_wall_height");
    keys.push_back("pad_wall_slope");
    keys.push_back("pad_wall_thickness");
    keys.push_back("raft_type");
    keys.push_back("printer_model");
    keys.push_back("printer_notes");
    keys.push_back("printer_technology");
    keys.push_back("printer_variant");
    keys.push_back("relative_correction");
    keys.push_back("relative_correction_x");
    keys.push_back("relative_correction_y");
    keys.push_back("relative_correction_z");
    keys.push_back("sla_archive_format");
    keys.push_back("sla_output_precision");
    keys.push_back("slice_closing_radius");
    keys.push_back("slicing_mode");
    keys.push_back("slow_tilt_time");
    keys.push_back("support_base_diameter");
    keys.push_back("support_base_height");
    keys.push_back("support_base_safety_distance");
    keys.push_back("support_buildplate_only");
    keys.push_back("support_critical_angle");
    keys.push_back("support_enforcers_only");
    keys.push_back("support_head_front_diameter");
    keys.push_back("support_head_penetration");
    keys.push_back("support_head_width");
    keys.push_back("support_max_bridge_length");
    keys.push_back("support_max_bridges_on_pillar");
    keys.push_back("support_max_pillar_link_distance");
    keys.push_back("support_max_weight_on_model");
    keys.push_back("support_object_elevation");
    keys.push_back("support_pillar_connection_mode");
    keys.push_back("support_pillar_diameter");
    keys.push_back("support_pillar_widening_factor");
    keys.push_back("support_points_density_relative");
    keys.push_back("support_small_pillar_diameter_percent");
    keys.push_back("support_tree_type");
    keys.push_back("supports_enable");
    keys.push_back("thumbnails");
    keys.push_back("thumbnails_format");
    keys.push_back("tilt_down_cycles");
    keys.push_back("tilt_down_delay");
    keys.push_back("tilt_down_finish_speed");
    keys.push_back("tilt_down_initial_speed");
    keys.push_back("tilt_down_offset_delay");
    keys.push_back("tilt_down_offset_steps");
    keys.push_back("tilt_up_cycles");
    keys.push_back("tilt_up_delay");
    keys.push_back("tilt_up_finish_speed");
    keys.push_back("tilt_up_initial_speed");
    keys.push_back("tilt_up_offset_delay");
    keys.push_back("tilt_up_offset_steps");
    keys.push_back("tower_hop_height");
    keys.push_back("tower_speed");
    keys.push_back("use_tilt");
    keys.push_back("zcorrection_layers");

    keys.push_back("bottom_layer_count");
    keys.push_back("bottom_light_pwm");
    keys.push_back("bottom_lift_height");
    keys.push_back("bottom_lift_height_2");
    keys.push_back("bottom_lift_speed");
    keys.push_back("bottom_lift_speed_2");
    keys.push_back("bottom_retract_speed");
    keys.push_back("bottom_retract_speed_2");
    keys.push_back("bottom_wait_after_lift");
    keys.push_back("bottom_wait_after_retract");
    keys.push_back("bottom_wait_before_lift");
    keys.push_back("light_pwm");
    keys.push_back("lift_height");
    keys.push_back("lift_height_2");
    keys.push_back("lift_speed");
    keys.push_back("lift_speed_2");
    keys.push_back("material_source_note");
    keys.push_back("retract_speed");
    keys.push_back("retract_speed_2");
    keys.push_back("wait_after_lift");
    keys.push_back("wait_after_retract");
    keys.push_back("wait_before_lift");

    return keys;
}

std::vector<Step> get_expected_steps_for_key(const std::string& key) {
    const auto& map = get_invalidated_by_map();
    auto it = map.find(key);
    if (it == map.end()) {
        return {};
    }
    return it->second;
}

bool steps_equal(const std::vector<Step>& a, const std::vector<Step>& b) {
    if (a.size() != b.size()) return false;
    std::set<Step> set_a(a.begin(), a.end());
    std::set<Step> set_b(b.begin(), b.end());
    return set_a == set_b;
}

std::string steps_to_string(const std::vector<Step>& steps) {
    std::string result = "[";
    for (size_t i = 0; i < steps.size(); ++i) {
        if (i > 0) result += ", ";
        std::visit([&](auto&& step) {
            using T = std::decay_t<decltype(step)>;
            if constexpr (std::is_same_v<T, SLAPrintStep>) {
                switch (step) {
                    case slapsMergeSlicesAndEval: result += "slapsMergeSlicesAndEval"; break;
                    case slapsRasterize: result += "slapsRasterize"; break;
                    default: result += "unknown_print_step";
                }
            } else {
                switch (step) {
                    case slaposAssembly: result += "slaposAssembly"; break;
                    case slaposHollowing: result += "slaposHollowing"; break;
                    case slaposDrillHoles: result += "slaposDrillHoles"; break;
                    case slaposObjectSlice: result += "slaposObjectSlice"; break;
                    case slaposSupportPoints: result += "slaposSupportPoints"; break;
                    case slaposSupportTree: result += "slaposSupportTree"; break;
                    case slaposPad: result += "slaposPad"; break;
                    case slaposSliceSupports: result += "slaposSliceSupports"; break;
                    default: result += "unknown_object_step";
                }
            }
        }, steps[i]);
    }
    result += "]";
    return result;
}

PrintAndObjectSteps invoke_diff_to_invalidated_steps(const std::vector<std::string>& diff) {
    return diff_to_invalidated_steps(diff);
}

} // namespace

TEST_CASE("SLAInvalidation: every SLA config key has an invalidation entry", "[SLAInvalidation]") {
    const auto& invalidated_by = get_invalidated_by_map();
    const auto keys = get_all_sla_config_keys();

    for (const auto& key : keys) {
        INFO("Key: " << key);
        REQUIRE(invalidated_by.contains(key));
    }
}

TEST_CASE("SLAInvalidation: table-driven expected steps for representative keys", "[SLAInvalidation]") {
    struct TestCase {
        std::string key;
        std::vector<Step> expected;
    };

    std::vector<TestCase> test_cases = {
        {"absolute_correction", all_steps()},
        {"area_fill", steps({propagate(slapsMergeSlicesAndEval)})},
        {"bed_custom_model", all_steps()},
        {"bed_custom_texture", steps({})},
        {"bed_shape", steps({})},
        {"bottle_cost", steps({})},
        {"branchingsupport_base_diameter", steps({propagate(slaposSupportTree)})},
        {"branchingsupport_object_elevation", steps({propagate(slaposObjectSlice)})},
        {"display_width", steps({propagate(slapsMergeSlicesAndEval)})},
        {"elefant_foot_compensation", all_steps()},
        {"exposure_time", steps({propagate(slapsMergeSlicesAndEval)})},
        {"faded_layers", steps({propagate(slaposObjectSlice)})},
        {"gamma_correction", all_steps()},
        {"hollowing_enable", steps({propagate(slaposHollowing)})},
        {"initial_layer_height", all_steps()},
        {"layer_height", steps({propagate(slaposObjectSlice)})},
        {"material_colour", steps({})},
        {"material_correction", all_steps()},
        {"pad_brim_size", steps({propagate(slaposPad)})},
        {"pad_enable", steps({propagate(slaposObjectSlice)})},
        {"raft_type", steps({propagate(slaposObjectSlice), propagate(slaposPad)})},
        {"relative_correction", all_steps()},
        {"support_base_diameter", steps({propagate(slaposSupportTree)})},
        {"support_object_elevation", steps({propagate(slaposObjectSlice)})},
        {"support_points_density_relative", steps({propagate(slaposSupportPoints)})},
        {"support_tree_type", steps({propagate(slaposObjectSlice)})},
        {"supports_enable", steps({propagate(slaposObjectSlice)})},
        {"tilt_down_cycles", steps({propagate(slapsMergeSlicesAndEval)})},
        {"use_tilt", steps({propagate(slapsMergeSlicesAndEval)})},
        {"zcorrection_layers", all_steps()},
    };

    for (const auto& tc : test_cases) {
        INFO("Key: " << tc.key);
        const auto actual = get_expected_steps_for_key(tc.key);
        REQUIRE(steps_equal(actual, tc.expected));
    }
}

TEST_CASE("SLAInvalidation: diff_to_invalidated_steps returns union for multiple keys and empty for empty diff", "[SLAInvalidation]") {
    SECTION("empty diff returns empty sets") {
        auto result = invoke_diff_to_invalidated_steps({});
        REQUIRE(std::get<PrintSteps>(result.first).empty());
        REQUIRE(std::get<PrintObjectSteps>(result.second).empty());
    }

    SECTION("single key returns its steps") {
        auto result = invoke_diff_to_invalidated_steps({"layer_height"});
        const auto& print_steps = std::get<PrintSteps>(result.first);
        const auto& object_steps = std::get<PrintObjectSteps>(result.second);
        REQUIRE(print_steps.size() == 1);
        REQUIRE(print_steps.count(slapsMergeSlicesAndEval) == 1);
        REQUIRE(object_steps.size() == 5);
        REQUIRE(object_steps.count(slaposObjectSlice) == 1);
        REQUIRE(object_steps.count(slaposSupportPoints) == 1);
        REQUIRE(object_steps.count(slaposSupportTree) == 1);
        REQUIRE(object_steps.count(slaposPad) == 1);
        REQUIRE(object_steps.count(slaposSliceSupports) == 1);
    }

    SECTION("multiple keys returns union of steps") {
        auto result = invoke_diff_to_invalidated_steps({"layer_height", "support_base_diameter"});
        const auto& print_steps = std::get<PrintSteps>(result.first);
        const auto& object_steps = std::get<PrintObjectSteps>(result.second);

        REQUIRE(print_steps.size() == 1);
        REQUIRE(print_steps.count(slapsMergeSlicesAndEval) == 1);
        // support_base_diameter only adds steps that layer_height already reaches, so the union
        // is the same five as layer_height alone (see the section above), not six.
        REQUIRE(object_steps.size() == 5);
        REQUIRE(object_steps.count(slaposObjectSlice) == 1);
        REQUIRE(object_steps.count(slaposSupportPoints) == 1);
        REQUIRE(object_steps.count(slaposSupportTree) == 1);
        REQUIRE(object_steps.count(slaposPad) == 1);
        REQUIRE(object_steps.count(slaposSliceSupports) == 1);
    }

    SECTION("keys with all_steps propagate to print steps") {
        auto result = invoke_diff_to_invalidated_steps({"absolute_correction", "layer_height"});
        const auto& print_steps = std::get<PrintSteps>(result.first);
        const auto& object_steps = std::get<PrintObjectSteps>(result.second);

        REQUIRE(print_steps.size() == 2);
        REQUIRE(print_steps.count(slapsMergeSlicesAndEval) == 1);
        REQUIRE(print_steps.count(slapsRasterize) == 1);

        REQUIRE(object_steps.size() == 8);
        REQUIRE(object_steps.count(slaposAssembly) == 1);
        REQUIRE(object_steps.count(slaposHollowing) == 1);
        REQUIRE(object_steps.count(slaposDrillHoles) == 1);
        REQUIRE(object_steps.count(slaposObjectSlice) == 1);
        REQUIRE(object_steps.count(slaposSupportPoints) == 1);
        REQUIRE(object_steps.count(slaposSupportTree) == 1);
        REQUIRE(object_steps.count(slaposPad) == 1);
        REQUIRE(object_steps.count(slaposSliceSupports) == 1);
    }
}