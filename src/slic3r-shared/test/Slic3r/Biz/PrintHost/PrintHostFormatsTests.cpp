#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/PrintHost/PrintHostFormats.hpp"

#include <initializer_list>
#include <string>
#include <utility>

using namespace Slic3r::Biz::PrintHost;
using Slic3r::Biz::PhysicalPrinter::ConnectUpload;
using Slic3r::Biz::PhysicalPrinter::PhysicalPrinterConfig;
using Slic3r::Biz::PhysicalPrinter::PrinterUpload;

namespace {

PrinterUpload upload(Slic3r::Domain::PrintHostType type)
{
    PrinterUpload payload;
    payload.type = type;
    return payload;
}

PhysicalPrinterConfig printer_host(Slic3r::Domain::PrintHostType type, std::string name = {})
{
    PhysicalPrinterConfig destination;
    destination.payload = upload(type);
    destination.name    = std::move(name);
    return destination;
}

PhysicalPrinterConfig connect()
{
    PhysicalPrinterConfig destination;
    destination.payload = ConnectUpload{};
    return destination;
}

} // namespace

// The SLA bed is uploaded as the printer's own archive (.pm5, .goo, .sl1, ...). Only the hosts that
// can read it may be sent one: PrusaLink and Connect, because the SL1 runs their firmware.
TEST_CASE("Only the SLA print hosts take an SLA archive", "[export][sla][print-host]")
{
    CHECK(print_host_accepts_extension(Slic3r::Domain::PrintHostType::PrusaLink, ".sl1"));
    CHECK(print_host_accepts_extension(Slic3r::Domain::PrintHostType::PrusaLink, ".sl1s"));
    CHECK(print_host_accepts_extension(Slic3r::Domain::PrintHostType::PrusaLink, ".gcode"));
    CHECK(print_host_accepts_extension(Slic3r::Domain::PrintHostType::PrusaLink, ".bgcode"));
    // The SL1's own host entry speaks of nothing else.
    CHECK(print_host_accepts_extension(Slic3r::Domain::PrintHostType::SL1Host, ".sl1"));
    CHECK(print_host_accepts_extension(Slic3r::Domain::PrintHostType::SL1Host, ".sl1s"));
    CHECK_FALSE(print_host_accepts_extension(Slic3r::Domain::PrintHostType::SL1Host, ".gcode"));
    CHECK_FALSE(print_host_accepts_extension(Slic3r::Domain::PrintHostType::PrusaLink, ".pm5"));
    CHECK_FALSE(print_host_accepts_extension(Slic3r::Domain::PrintHostType::PrusaLink, ".goo"));
    CHECK_FALSE(print_host_accepts_extension(Slic3r::Domain::PrintHostType::SL1Host, ".pm5"));
}

TEST_CASE("The FFF print hosts take G-Code only", "[export][sla][print-host]")
{
    for (const Slic3r::Domain::PrintHostType type :
         {Slic3r::Domain::PrintHostType::OctoPrint,
          Slic3r::Domain::PrintHostType::Moonraker,
          Slic3r::Domain::PrintHostType::Duet,
          Slic3r::Domain::PrintHostType::FlashAir,
          Slic3r::Domain::PrintHostType::AstroBox,
          Slic3r::Domain::PrintHostType::Repetier,
          Slic3r::Domain::PrintHostType::MKS})
    {
        CHECK(print_host_accepts_extension(type, ".gcode"));
        CHECK(print_host_accepts_extension(type, ".bgcode"));
        CHECK_FALSE(print_host_accepts_extension(type, ".pm5"));
        CHECK_FALSE(print_host_accepts_extension(type, ".sl1"));
    }
    // A storage query uploads no file, and an extension we do not know is refused.
    CHECK_FALSE(
        print_host_accepts_extension(Slic3r::Domain::PrintHostType::PrusaLinkStorage, ".gcode")
    );
    CHECK_FALSE(print_host_accepts_extension(Slic3r::Domain::PrintHostType::PrusaLink, ".zip"));
    CHECK_FALSE(print_host_accepts_extension(Slic3r::Domain::PrintHostType::PrusaLink, ""));
}

TEST_CASE(
    "An extension is matched with or without the dot and in any case",
    "[export][sla][print-host]"
)
{
    CHECK(print_host_accepts_extension(Slic3r::Domain::PrintHostType::PrusaLink, "sl1"));
    CHECK(print_host_accepts_extension(Slic3r::Domain::PrintHostType::PrusaLink, ".SL1S"));
    CHECK_FALSE(print_host_accepts_extension(Slic3r::Domain::PrintHostType::OctoPrint, "PM5"));
}

TEST_CASE("A destination accepts what it can print", "[export][sla][print-host]")
{
    CHECK(destination_accepts_extension(
        printer_host(Slic3r::Domain::PrintHostType::PrusaLink),
        ".sl1"
    ));
    CHECK_FALSE(destination_accepts_extension(
        printer_host(Slic3r::Domain::PrintHostType::PrusaLink),
        ".pm5"
    ));
    CHECK_FALSE(destination_accepts_extension(
        printer_host(Slic3r::Domain::PrintHostType::OctoPrint),
        ".pm5"
    ));

    CHECK(destination_accepts_extension(connect(), ".gcode"));
    CHECK(destination_accepts_extension(connect(), ".sl1"));
    CHECK_FALSE(destination_accepts_extension(connect(), ".pm5"));

    // A drive only writes the file, so it takes the printer's own format and FFF alike.
    const PhysicalPrinterConfig drive{Slic3r::Biz::PhysicalPrinter::filesystem_export_removable()};
    CHECK(destination_accepts_extension(drive, ".pm5"));
    CHECK(destination_accepts_extension(drive, ".gcode"));
}

TEST_CASE(
    "A host that cannot take the file says which file it refuses",
    "[export][sla][print-host]"
)
{
    CHECK(
        destination_rejects_extension_message(
            printer_host(Slic3r::Domain::PrintHostType::OctoPrint),
            ".pm5"
        )
        == "OctoPrint does not accept .pm5 files"
    );
    // The printer's own name, when the user gave it one, is the clearer address.
    CHECK(
        destination_rejects_extension_message(
            printer_host(Slic3r::Domain::PrintHostType::PrusaLink, "Living room SL1"),
            "pm5"
        )
        == "Living room SL1 does not accept .pm5 files"
    );
    CHECK(
        destination_rejects_extension_message(connect(), ".goo")
        == "Prusa Connect does not accept .goo files"
    );
    // A name that lost its extension gets its own wording.
    CHECK_FALSE(
        destination_rejects_extension_message(
            printer_host(Slic3r::Domain::PrintHostType::PrusaLink),
            ""
        )
            .empty()
    );
    // Nothing to say when the file is accepted.
    CHECK(destination_rejects_extension_message(
              printer_host(Slic3r::Domain::PrintHostType::PrusaLink),
              ".sl1"
    )
              .empty());
    const PhysicalPrinterConfig local{Slic3r::Biz::PhysicalPrinter::filesystem_export_local()};
    CHECK(destination_rejects_extension_message(local, ".pm5").empty());
}
