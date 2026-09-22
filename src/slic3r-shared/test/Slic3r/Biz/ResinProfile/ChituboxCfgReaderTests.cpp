#include <catch2/catch_test_macros.hpp>

#include "Slic3r/Biz/ResinProfile/ChituboxCfgReader.hpp"
#include "Slic3r/Biz/ResinProfile/ResinProfileReaderRegistry.hpp"

#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>

#include <memory>
#include <string>

using namespace Slic3r::Biz::ResinProfile;

namespace {

// Writes bytes verbatim: the BOM, CRLF and lone-CR cases depend on nothing being translated.
struct TempCfg
{
    explicit TempCfg(const std::string& contents)
        : path{boost::filesystem::temp_directory_path() / boost::filesystem::unique_path()}
    {
        boost::filesystem::ofstream out{path, std::ios::binary};
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    ~TempCfg()
    {
        boost::system::error_code ec;
        boost::filesystem::remove(path, ec);
    }

    TempCfg(const TempCfg&) = delete;
    TempCfg& operator=(const TempCfg&) = delete;

    boost::filesystem::path path;
};

const std::string MINIMAL =
    "normalExposureTime:2.5\n"
    "layerHeight:0.05\n";

ForeignResinProfile read_ok(const std::string& contents)
{
    const TempCfg file{contents};
    const ChituboxCfgReader reader;
    const auto result = reader.read(file.path);
    REQUIRE(result.has_value());
    return *result;
}

} // namespace

TEST_CASE("ChituboxCfgReader - reads key/value lines", "[resin_profile][chitubox]")
{
    const ForeignResinProfile profile = read_ok(MINIMAL);

    REQUIRE(profile.source_format == "chitubox-cfg");
    REQUIRE(profile.raw_values.at("normalExposureTime") == "2.5");
    REQUIRE(profile.raw_values.at("layerHeight") == "0.05");
    REQUIRE(profile.warnings.empty());
}

TEST_CASE("ChituboxCfgReader - old and new key spellings both survive", "[resin_profile][chitubox]")
{
    const ForeignResinProfile profile = read_ok(
        "normalExposureTime:2.5\n"
        "exposureTime:3.0\n"
    );

    // Neither spelling may be dropped in favour of the other: mapping decides later which wins.
    REQUIRE(profile.raw_values.at("normalExposureTime") == "2.5");
    REQUIRE(profile.raw_values.at("exposureTime") == "3.0");
}

TEST_CASE("ChituboxCfgReader - a value may contain colons", "[resin_profile][chitubox]")
{
    const ForeignResinProfile profile = read_ok(
        "normalExposureTime:2.5\n"
        "machineName:Mono X:4K\n"
    );

    // Splitting on the last colon, or on every colon, would corrupt this.
    REQUIRE(profile.raw_values.at("machineName") == "Mono X:4K");
}

TEST_CASE("ChituboxCfgReader - CRLF and a lone CR are both accepted", "[resin_profile][chitubox]")
{
    const ForeignResinProfile crlf = read_ok("normalExposureTime:2.5\r\nlayerHeight:0.05\r\n");
    REQUIRE(crlf.raw_values.at("normalExposureTime") == "2.5");
    REQUIRE(crlf.raw_values.at("layerHeight") == "0.05");

    const ForeignResinProfile cr = read_ok("normalExposureTime:2.5\rlayerHeight:0.05\r");
    REQUIRE(cr.raw_values.at("normalExposureTime") == "2.5");
    REQUIRE(cr.raw_values.at("layerHeight") == "0.05");
}

TEST_CASE("ChituboxCfgReader - a UTF-8 BOM does not hide the first key", "[resin_profile][chitubox]")
{
    const std::string bom = "\xEF\xBB\xBF";
    const ForeignResinProfile profile = read_ok(bom + MINIMAL);

    // With the BOM left on, the first key parses as "\xEF\xBB\xBFnormalExposureTime".
    REQUIRE(profile.raw_values.count("normalExposureTime") == 1);
    REQUIRE(profile.raw_values.at("normalExposureTime") == "2.5");
}

TEST_CASE("ChituboxCfgReader - sniff accepts a BOM'd file", "[resin_profile][chitubox]")
{
    const ChituboxCfgReader reader;
    const std::string bom = "\xEF\xBB\xBF";

    // sniff() runs before read(), so a BOM missed here rejects the file as an unknown format
    // however well read() copes with it.
    REQUIRE(reader.sniff(MINIMAL));
    REQUIRE(reader.sniff(bom + MINIMAL));
    REQUIRE(reader.sniff("\xEF\xBB\xBF" "normalExposureTime:2.5\r\n"));
}

TEST_CASE("ChituboxCfgReader - sniff rejects what is not a Chitubox file", "[resin_profile][chitubox]")
{
    const ChituboxCfgReader reader;

    REQUIRE_FALSE(reader.sniff(""));
    REQUIRE_FALSE(reader.sniff("just some text\nwith no keys at all\n"));
    REQUIRE_FALSE(reader.sniff("[section]\nkey=value\n"));
}

TEST_CASE("ChituboxCfgReader - a quoted value may span lines", "[resin_profile][chitubox]")
{
    const ForeignResinProfile profile = read_ok(
        "normalExposureTime:2.5\n"
        "gcodeBefore:\"G28\nG1 Z10 F600\nM106 S255\"\n"
        "layerHeight:0.05\n"
    );

    const std::string& gcode = profile.raw_values.at("gcodeBefore");
    // Truncating at the first newline is the failure this guards against.
    REQUIRE(gcode.find("G28") != std::string::npos);
    REQUIRE(gcode.find("G1 Z10 F600") != std::string::npos);
    REQUIRE(gcode.find("M106 S255") != std::string::npos);

    // The key after the block must still be read, and its lines must not be logged as junk.
    REQUIRE(profile.raw_values.at("layerHeight") == "0.05");
    REQUIRE(profile.warnings.empty());
}

TEST_CASE("ChituboxCfgReader - an unterminated quote is reported", "[resin_profile][chitubox]")
{
    const ForeignResinProfile profile = read_ok(
        "normalExposureTime:2.5\n"
        "gcodeBefore:\"G28\nG1 Z10\n"
    );

    REQUIRE_FALSE(profile.warnings.empty());
}

TEST_CASE("ChituboxCfgReader - unknown keys are kept, not an error", "[resin_profile][chitubox]")
{
    const ForeignResinProfile profile = read_ok(
        MINIMAL +
        "someKeyWeHaveNeverSeen:42\n"
    );

    REQUIRE(profile.raw_values.at("someKeyWeHaveNeverSeen") == "42");
}

TEST_CASE("ChituboxCfgReader - a garbage numeric value warns but still parses", "[resin_profile][chitubox]")
{
    const ForeignResinProfile profile = read_ok("normalExposureTime:abc\n");

    REQUIRE(profile.raw_values.at("normalExposureTime") == "abc");
    REQUIRE_FALSE(profile.warnings.empty());
}

TEST_CASE("ChituboxCfgReader - an empty file reads as an empty profile", "[resin_profile][chitubox]")
{
    const ForeignResinProfile profile = read_ok("");

    REQUIRE(profile.raw_values.empty());
}

TEST_CASE("ChituboxCfgReader - missing keys are not an error here", "[resin_profile][chitubox]")
{
    // Completeness is the mapper's problem (M3.5), not the reader's.
    const ForeignResinProfile profile = read_ok("normalExposureTime:2.5\n");

    REQUIRE(profile.raw_values.size() == 1);
}

TEST_CASE("ResinProfileReaderRegistry - dispatches by content, not extension", "[resin_profile]")
{
    ResinProfileReaderRegistry registry;
    registry.register_reader(std::make_unique<ChituboxCfgReader>());

    const TempCfg chitubox{MINIMAL};
    const auto ok = registry.read_file(chitubox.path);
    REQUIRE(ok.has_value());
    REQUIRE(ok->source_format == "chitubox-cfg");

    const TempCfg foreign{"[general]\nsomething=1\n"};
    const auto rejected = registry.read_file(foreign.path);
    REQUIRE_FALSE(rejected.has_value());
}

TEST_CASE("ResinProfileReaderRegistry - an empty registry matches nothing", "[resin_profile]")
{
    const ResinProfileReaderRegistry registry;
    const TempCfg chitubox{MINIMAL};

    const auto result = registry.read_file(chitubox.path);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("ResinProfileReaderRegistry - a missing file is an error, not a crash", "[resin_profile]")
{
    ResinProfileReaderRegistry registry;
    registry.register_reader(std::make_unique<ChituboxCfgReader>());

    const boost::filesystem::path missing =
        boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();

    const auto result = registry.read_file(missing);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("ChituboxCfgReader - a two-stage lift profile keeps both stages", "[resin_profile][chitubox]")
{
    // These key names are our best understanding of Chitubox's two-stage lift keys
    // and must be checked against the real profiles of M3.1 when they arrive.
    const std::string two_stage =
        "normalExposureTime:2.5\n"
        "bottomLiftHeight:5\n"
        "bottomLiftSpeed:60\n"
        "bottomLiftHeight2:3\n"
        "bottomLiftSpeed2:180\n"
        "liftHeight:5\n"
        "liftSpeed:65\n"
        "liftHeight2:3\n"
        "liftSpeed2:180\n"
        "retractSpeed:150\n"
        "retractSpeed2:60\n";

    const ForeignResinProfile profile = read_ok(two_stage);

    REQUIRE(profile.raw_values.at("normalExposureTime") == "2.5");
    REQUIRE(profile.raw_values.at("bottomLiftHeight") == "5");
    REQUIRE(profile.raw_values.at("bottomLiftSpeed") == "60");
    REQUIRE(profile.raw_values.at("bottomLiftHeight2") == "3");
    REQUIRE(profile.raw_values.at("bottomLiftSpeed2") == "180");
    REQUIRE(profile.raw_values.at("liftHeight") == "5");
    REQUIRE(profile.raw_values.at("liftSpeed") == "65");
    REQUIRE(profile.raw_values.at("liftHeight2") == "3");
    REQUIRE(profile.raw_values.at("liftSpeed2") == "180");
    REQUIRE(profile.raw_values.at("retractSpeed") == "150");
    REQUIRE(profile.raw_values.at("retractSpeed2") == "60");
    REQUIRE(profile.warnings.empty());
}

TEST_CASE("ChituboxCfgReader - a file over the size limit is refused, not read", "[resin_profile][chitubox]")
{
    constexpr std::size_t LIMIT = 8 * 1024 * 1024;
    ChituboxCfgReader reader;

    // Build a file of exactly LIMIT bytes that starts with a valid Chitubox line.
    // "normalExposureTime:2.5\n" = 22 bytes. Pad with "someKey:1\n" = 10 bytes each.
    const std::string header = "normalExposureTime:2.5\n";
    const std::string pad_line = "someKey:1\n";
    const std::size_t header_size = header.size();
    const std::size_t pad_line_size = pad_line.size();

    // Exactly LIMIT bytes
    {
        std::size_t remaining = LIMIT - header_size;
        std::size_t pad_count = remaining / pad_line_size;
        std::size_t extra = remaining % pad_line_size;

        std::string content = header;
        content.reserve(LIMIT);
        for (std::size_t i = 0; i < pad_count; ++i) {
            content += pad_line;
        }
        if (extra > 0) {
            content.append(extra, 'x');
        }

        REQUIRE(content.size() == LIMIT);

        TempCfg file{content};
        const auto result = reader.read(file.path);
        // The check in ChituboxCfgReader::read is file_size > MAX_CFG_FILE_SIZE (strict).
        // So exactly LIMIT should be accepted.
        REQUIRE(result.has_value());
        REQUIRE(result->raw_values.at("normalExposureTime") == "2.5");
    }

    // LIMIT + 1 byte -> refused by ChituboxCfgReader::read
    {
        std::size_t remaining = (LIMIT + 1) - header_size;
        std::size_t pad_count = remaining / pad_line_size;
        std::size_t extra = remaining % pad_line_size;

        std::string content = header;
        content.reserve(LIMIT + 1);
        for (std::size_t i = 0; i < pad_count; ++i) {
            content += pad_line;
        }
        if (extra > 0) {
            content.append(extra, 'x');
        }

        REQUIRE(content.size() == LIMIT + 1);

        TempCfg file{content};
        const auto result = reader.read(file.path);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == "File too large (max 8 MB)");
    }
}

TEST_CASE("ResinProfileReaderRegistry - a file over the size limit is refused before sniffing", "[resin_profile]")
{
    constexpr std::size_t LIMIT = 8 * 1024 * 1024;
    ResinProfileReaderRegistry registry;
    registry.register_reader(std::make_unique<ChituboxCfgReader>());

    // Build a file of exactly LIMIT bytes that starts with a valid Chitubox line.
    const std::string header = "normalExposureTime:2.5\n";
    const std::string pad_line = "someKey:1\n";
    const std::size_t header_size = header.size();
    const std::size_t pad_line_size = pad_line.size();

    // Exactly LIMIT bytes -> accepted by registry (check is file_size > MAX_FILE_SIZE, strict)
    {
        std::size_t remaining = LIMIT - header_size;
        std::size_t pad_count = remaining / pad_line_size;
        std::size_t extra = remaining % pad_line_size;

        std::string content = header;
        content.reserve(LIMIT);
        for (std::size_t i = 0; i < pad_count; ++i) {
            content += pad_line;
        }
        if (extra > 0) {
            content.append(extra, 'x');
        }

        REQUIRE(content.size() == LIMIT);

        TempCfg file{content};
        const auto result = registry.read_file(file.path);
        REQUIRE(result.has_value());
        REQUIRE(result->source_format == "chitubox-cfg");
        REQUIRE(result->raw_values.at("normalExposureTime") == "2.5");
    }

    // LIMIT + 1 byte -> refused by registry before it ever calls a reader
    {
        std::size_t remaining = (LIMIT + 1) - header_size;
        std::size_t pad_count = remaining / pad_line_size;
        std::size_t extra = remaining % pad_line_size;

        std::string content = header;
        content.reserve(LIMIT + 1);
        for (std::size_t i = 0; i < pad_count; ++i) {
            content += pad_line;
        }
        if (extra > 0) {
            content.append(extra, 'x');
        }

        REQUIRE(content.size() == LIMIT + 1);

        TempCfg file{content};
        const auto result = registry.read_file(file.path);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error() == "File too large (max 8 MB)");
    }
}
