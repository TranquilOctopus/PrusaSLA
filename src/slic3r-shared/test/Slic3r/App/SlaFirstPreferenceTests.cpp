#include <catch2/catch_test_macros.hpp>

#include <Slic3r/App/SlaFirstPreference.hpp>
#include <Slic3r/Domain/PrinterTechnology.hpp>

using namespace Slic3r::App;
using namespace Slic3r::Domain;

TEST_CASE("SlaFirstPreference - SLA present and flag on", "[SlaFirstPreference]")
{
    std::vector<PrinterTechnology> technologies = {
        PrinterTechnology::FFF,
        PrinterTechnology::SLA,
        PrinterTechnology::FFF
    };

    size_t index = select_preselected_printer_index(technologies, true);
    REQUIRE(index == 1); // First SLA at index 1
}

TEST_CASE("SlaFirstPreference - SLA absent and flag on", "[SlaFirstPreference]")
{
    std::vector<PrinterTechnology> technologies = {
        PrinterTechnology::FFF,
        PrinterTechnology::FFF
    };

    size_t index = select_preselected_printer_index(technologies, true);
    REQUIRE(index == 0); // No SLA, falls back to first
}

TEST_CASE("SlaFirstPreference - flag off", "[SlaFirstPreference]")
{
    std::vector<PrinterTechnology> technologies = {
        PrinterTechnology::SLA,
        PrinterTechnology::FFF
    };

    size_t index = select_preselected_printer_index(technologies, false);
    REQUIRE(index == 0); // Flag off, always first
}

TEST_CASE("SlaFirstPreference - empty list", "[SlaFirstPreference]")
{
    std::vector<PrinterTechnology> technologies = {};

    size_t index = select_preselected_printer_index(technologies, true);
    REQUIRE(index == 0); // Empty list returns 0
}

TEST_CASE("SlaFirstPreference - multiple SLA printers", "[SlaFirstPreference]")
{
    std::vector<PrinterTechnology> technologies = {
        PrinterTechnology::FFF,
        PrinterTechnology::SLA,
        PrinterTechnology::SLA,
        PrinterTechnology::FFF
    };

    size_t index = select_preselected_printer_index(technologies, true);
    REQUIRE(index == 1); // First SLA at index 1
}

TEST_CASE("SlaFirstPreference - SLA first in list", "[SlaFirstPreference]")
{
    std::vector<PrinterTechnology> technologies = {
        PrinterTechnology::SLA,
        PrinterTechnology::FFF
    };

    size_t index = select_preselected_printer_index(technologies, true);
    REQUIRE(index == 0); // First printer is SLA
}

TEST_CASE("SlaFirstPreference - single FFF printer", "[SlaFirstPreference]")
{
    std::vector<PrinterTechnology> technologies = {
        PrinterTechnology::FFF
    };

    size_t index = select_preselected_printer_index(technologies, true);
    REQUIRE(index == 0); // Only one printer
}

TEST_CASE("SlaFirstPreference - single SLA printer", "[SlaFirstPreference]")
{
    std::vector<PrinterTechnology> technologies = {
        PrinterTechnology::SLA
    };

    size_t index = select_preselected_printer_index(technologies, true);
    REQUIRE(index == 0); // Only one printer which is SLA
}