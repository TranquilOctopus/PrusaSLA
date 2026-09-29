#include <catch2/catch_test_macros.hpp>

#include <Slic3r/App/Hints/SlaHints.hpp>
#include <Slic3r/Domain/PrinterTechnology.hpp>

#include <algorithm>
#include <string>

using namespace Slic3r::App::Hints;
using namespace Slic3r::Domain;

namespace {

bool mentions(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

} // namespace

TEST_CASE("SlaHints - SLA selection gets the SLA hints only", "[SlaHints]")
{
    const std::vector<PlaterHint> sla_hints = hints_for(PrinterTechnology::SLA);

    // Enough guidance to learn the workflow, but not a wall of text.
    REQUIRE(sla_hints.size() >= size_t(5));
    REQUIRE(sla_hints.size() <= size_t(8));

    for (const PlaterHint& hint : sla_hints) {
        REQUIRE(hint.audience == HintAudience::Sla);
    }
}

TEST_CASE("SlaHints - FFF hints never reach an SLA printer", "[SlaHints]")
{
    const std::vector<PlaterHint> sla_hints = hints_for(PrinterTechnology::SLA);

    for (const char* fff_topic : {"filament", "nozzle", "seam", "infill", "brim"}) {
        for (const PlaterHint& hint : sla_hints) {
            REQUIRE_FALSE(mentions(hint.text, fff_topic));
        }
    }
}

TEST_CASE("SlaHints - the workflow the hints teach is covered", "[SlaHints]")
{
    const std::vector<PlaterHint> sla_hints = hints_for(PrinterTechnology::SLA);

    std::vector<std::string> texts;
    texts.reserve(sla_hints.size());
    for (const PlaterHint& hint : sla_hints) {
        texts.push_back(hint.text);
    }

    const auto any_of = [&texts](const char* needle) {
        return std::any_of(texts.begin(), texts.end(), [needle](const std::string& t) { return mentions(t, needle); });
    };

    REQUIRE(any_of("support"));        // supports are an explicit step
    REQUIRE(any_of("Slice"));          // nothing is sliced before Slice is pressed
    REQUIRE(any_of("Export"));         // export writes the printer's own format
    REQUIRE(any_of(".sl1"));           // ... in the printer's file format
}

TEST_CASE("SlaHints - the first model hint points at the next step", "[SlaHints]")
{
    const PlaterHint& hint = first_model_on_sla_plate_hint();

    REQUIRE(hint.audience == HintAudience::Sla);
    REQUIRE(mentions(hint.text, "support"));
    REQUIRE(mentions(hint.text, "Slice"));
    REQUIRE(hint.text == hints_for(PrinterTechnology::SLA).front().text);
}

TEST_CASE("SlaHints - FFF hints are filtered out for an SLA printer", "[SlaHints]")
{
    const std::vector<PlaterHint> sla_hints = hints_for(PrinterTechnology::SLA);

    for (const PlaterHint& hint : all_hints()) {
        const bool shown_for_sla = std::any_of(
            sla_hints.begin(), sla_hints.end(), [&hint](const PlaterHint& shown) { return shown.text == hint.text; });
        REQUIRE(shown_for_sla == (hint.audience != HintAudience::Fff));
    }
}
