// M5.4d: the .pm5 we write and the one Photon Workshop wrote for the same model on the same
// printer, compared layer by layer to see whether they put the model on the display the same way
// round. Nothing of the reference is in the repository: it is read in place, and only when the
// git-ignored local-samples folder is named in the environment.
//
// set SLA_LOCAL_SAMPLES=<repo>\local-samples
// build-default\src\slic3r-shared\Release\slic3r-shared-tests.exe "[.local]"
//
// The reference is Photon Workshop's slice of the maintainer's own `5.stl` for the Photon Mono M5,
// and the model is the one it was sliced from, so a layer of one file and a layer of the other at
// the same height are cross sections of the same shape. The two layers are lined up on their
// centroids (the two slicers put the model wherever they like on the plate) and then compared
// under identity, a mirror in X, a mirror in Y and a turn of 180 degrees. Identity is what the
// shipped community-sla profile is supposed to produce with its display_mirror_x: true (M5.4c);
// when another transform matches instead, the test names it, and the mirroring is what is wrong.

#include <catch2/catch_test_macros.hpp>

#include "Slic3r/App/Platform/StdMainThreadDispatcher.hpp"
#include "Slic3r/Biz/Algorithms/ModelObject.hpp"
#include "Slic3r/Biz/Format/STL.hpp"
#include "Slic3r/Biz/Preset/IO/BundlePaths.hpp"
#include "Slic3r/Biz/Preset/PresetInteractor.hpp"
#include "Slic3r/Biz/ProjectInteractor.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaArchiveFormat.hpp"
#include "Slic3r/Biz/ResultExport/SLA/SlaLayerDecoders.hpp"
#include "Slic3r/Biz/SlaFixture.hpp"
#include "Slic3r/Directories.hpp"
#include "Slic3r/Domain/BoundingBox.hpp"
#include "Slic3r/Domain/Config.hpp"
#include "Slic3r/Domain/ConfigContainer.hpp"
#include "Slic3r/Domain/ConfigDefsSLA.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/ModelObject.hpp"
#include "Slic3r/Domain/Preset/SelectedPreset.hpp"
#include "Slic3r/Domain/SlaLayerHeight.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"
#include "Slic3r/Domain/Types.hpp"
#include "Slic3r/Domain/Workbench.hpp"
#include "Slic3r/TestUtils/TestData.hpp"
#include "libslic3r/SLAResult.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Slic3r::Biz::PrintHost::Sla::register_sla_archive_formats;
using Slic3r::Biz::PrintHost::Sla::SlaArchiveFormatRegistry;
using Slic3r::Biz::Slicing::Sla::FileDataType;
using Slic3r::Domain::SLADisplayOrientation;
using Slic3r::Test::Sla::decode_pw0_layer;

// The reference file, inside the folder SLA_LOCAL_SAMPLES names, and the model it is a slice of.
constexpr const char* reference_file = "anycubic-photon-mono-m5/01_5.pm5";

// The printer both files are for, by the name the shipped bundle gives the printer preset.
constexpr std::string_view m5_printer_name = "Photon Mono M5";

// A grey value of half the light or more counts as lit, the rule the other decoder users in this
// folder follow. PW0 has 16 grey levels, so this is the middle of the range and neither file's
// antialiased edge counts.
constexpr uint8_t lit_grey = 128;

namespace {

// ---------------------------------------------------------------------------
// The .pm5 container, read the way doc/sla-fork/formats/pm5.md describes it: the address table in
// the file mark, the HEADER resolution, and the LAYERDEF entry per layer. The layer images stay on
// disk, one is pulled in when a layer is compared, so a whole file is never in memory.
// ---------------------------------------------------------------------------

// Catch2's INFO takes a single streamed expression, so the longer messages are built here first:
// that keeps them readable in the source and in the failure output.
template <typename T>
std::string message(const T& value)
{
    std::ostringstream stream;
    stream << value;

    return stream.str();
}

uint32_t read_le32(const uint8_t* data)
{
    return uint32_t(data[0])
        | (uint32_t(data[1]) << 8)
        | (uint32_t(data[2]) << 16)
        | (uint32_t(data[3]) << 24);
}

float read_le_float(const uint8_t* data)
{
    const uint32_t bits = read_le32(data);
    float value         = 0.f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool is_tag(const uint8_t* data, const char* tag)
{
    return std::memcmp(data, tag, 12) == 0;
}

struct Pm5Layer
{
    size_t offset    = 0;
    size_t size      = 0;
    double height_mm = 0.;
};

class Pm5Archive
{
public:
    void open(const fs::path& path)
    {
        m_file.open(path.string(), std::ios::binary);
        REQUIRE(m_file.is_open());

        // The file mark: the 12-byte magic, the format version, the number of addresses, and the
        // addresses themselves (the fifth of which is LAYERDEF, the only one this test needs).
        std::array<uint8_t, 20 + 9 * 4> intro{};
        read_at(0, intro.data(), intro.size());
        REQUIRE(is_tag(intro.data(), "ANYCUBIC\0\0\0\0"));
        REQUIRE(read_le32(intro.data() + 12) == 517u);
        REQUIRE(read_le32(intro.data() + 16) == 9u);
        const size_t addresses[9] = {
            read_le32(intro.data() + 20),
            read_le32(intro.data() + 24),
            read_le32(intro.data() + 28),
            read_le32(intro.data() + 32),
            read_le32(intro.data() + 36),
            read_le32(intro.data() + 40),
            read_le32(intro.data() + 44),
            read_le32(intro.data() + 48),
            read_le32(intro.data() + 52)
        };

        // HEADER: a 12-byte name, a u32 length, then the resolution at +44 and +48 of the body.
        std::array<uint8_t, 16 + 52> header{};
        read_at(addresses[0], header.data(), header.size());
        REQUIRE(is_tag(header.data(), "HEADER\0\0\0\0\0\0"));
        m_res_x = read_le32(header.data() + 16 + 44);
        m_res_y = read_le32(header.data() + 16 + 48);

        // LAYERDEF: a 12-byte name, a u32 length, the layer count, then 32 bytes per layer, of
        // which +0 is where the image starts, +4 how long it is, and +20 how thick the layer is.
        std::array<uint8_t, 20> head{};
        read_at(addresses[4], head.data(), head.size());
        REQUIRE(is_tag(head.data(), "LAYERDEF\0\0\0\0"));
        const size_t count = read_le32(head.data() + 16);
        REQUIRE(count > 0u);
        std::vector<uint8_t> table(count * 32);
        read_at(addresses[4] + 20, table.data(), table.size());
        m_layers.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            Pm5Layer layer;
            layer.offset    = read_le32(table.data() + i * 32);
            layer.size      = read_le32(table.data() + i * 32 + 4);
            layer.height_mm = read_le_float(table.data() + i * 32 + 20);
            m_layers.push_back(layer);
        }
    }

    uint32_t res_x() const
    {
        return m_res_x;
    }

    uint32_t res_y() const
    {
        return m_res_y;
    }

    const std::vector<Pm5Layer>& layers() const
    {
        return m_layers;
    }

    // The run-length bytes of one layer, still encoded.
    std::vector<uint8_t> layer_image(size_t index) const
    {
        const Pm5Layer& layer = m_layers.at(index);
        std::vector<uint8_t> bytes(layer.size);
        read_at(layer.offset, bytes.data(), bytes.size());
        return bytes;
    }

private:
    void read_at(size_t offset, uint8_t* out, size_t size) const
    {
        m_file.clear();
        m_file.seekg(std::streamoff(offset));
        m_file.read(reinterpret_cast<char*>(out), std::streamsize(size));
        REQUIRE(static_cast<size_t>(m_file.gcount()) == size);
    }

    mutable boost::nowide::ifstream m_file;
    uint32_t m_res_x = 0;
    uint32_t m_res_y = 0;
    std::vector<Pm5Layer> m_layers;
};

// ---------------------------------------------------------------------------
// A layer as a binary mask, and the four ways one image can be put on top of another.
// ---------------------------------------------------------------------------

// One layer image read as lit and unlit, with the centroid of the lit part, which is what the two
// layers are lined up on: the two slicers place the model wherever they like on the plate.
struct Mask
{
    size_t width  = 0;
    size_t height = 0;
    std::vector<uint8_t> lit;
    size_t count    = 0;
    double centre_x = 0.;
    double centre_y = 0.;
};

Mask mask_from_pw0(const std::vector<uint8_t>& encoded, size_t width, size_t height)
{
    std::vector<uint8_t> pixels = decode_pw0_layer(encoded, width * height);
    REQUIRE(pixels.size() == width * height);

    Mask mask;
    mask.width  = width;
    mask.height = height;
    mask.lit    = std::move(pixels);

    double sum_x = 0.;
    double sum_y = 0.;
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            uint8_t& pixel = mask.lit[y * width + x];
            pixel          = pixel >= lit_grey ? 1 : 0;
            if (pixel == 0)
                continue;
            mask.count += 1;
            sum_x += double(x);
            sum_y += double(y);
        }
    }
    if (mask.count > 0) {
        mask.centre_x = sum_x / double(mask.count);
        mask.centre_y = sum_y / double(mask.count);
    }

    return mask;
}

enum class Turn
{
    identity,
    mirror_x,
    mirror_y,
    rotate_180
};

constexpr std::array<Turn, 4>
    TURNS{Turn::identity, Turn::mirror_x, Turn::mirror_y, Turn::rotate_180};

const char* turn_name(Turn turn)
{
    switch (turn) {
    case Turn::identity:
        return "identity";
    case Turn::mirror_x:
        return "mirror_x";
    case Turn::mirror_y:
        return "mirror_y";
    case Turn::rotate_180:
        return "rotate_180";
    }
    return "unknown";
}

// The same pixel turned, which is what the printer's display does with the raster when a mirror
// flag is on: the image is mirrored about its own middle line.
void turn_pixel(
    Turn turn,
    size_t width,
    size_t height,
    size_t x,
    size_t y,
    long long& out_x,
    long long& out_y
)
{
    out_x = static_cast<long long>(x);
    out_y = static_cast<long long>(y);
    if (turn == Turn::mirror_x || turn == Turn::rotate_180)
        out_x = static_cast<long long>(width - 1 - x);
    if (turn == Turn::mirror_y || turn == Turn::rotate_180)
        out_y = static_cast<long long>(height - 1 - y);
}

// How much of ours is theirs, once ours has been turned and moved so that the two centroids land
// on the same pixel. Lit pixels of ours that end up outside theirs count into the union only,
// which is what keeps a layer that is not on the plate from matching by accident.
double iou_of_turned(const Mask& ours, const Mask& theirs, Turn turn)
{
    if (ours.count == 0 || theirs.count == 0)
        return 0.;

    // The centroid of the turned image mirrors the same way a pixel does.
    double centre_x = ours.centre_x;
    double centre_y = ours.centre_y;
    if (turn == Turn::mirror_x || turn == Turn::rotate_180)
        centre_x = double(ours.width - 1) - ours.centre_x;
    if (turn == Turn::mirror_y || turn == Turn::rotate_180)
        centre_y = double(ours.height - 1) - ours.centre_y;
    const long long shift_x = std::llround(theirs.centre_x - centre_x);
    const long long shift_y = std::llround(theirs.centre_y - centre_y);

    size_t intersection = 0;
    for (size_t y = 0; y < ours.height; ++y) {
        for (size_t x = 0; x < ours.width; ++x) {
            if (ours.lit[y * ours.width + x] == 0)
                continue;
            long long turned_x = 0;
            long long turned_y = 0;
            turn_pixel(turn, ours.width, ours.height, x, y, turned_x, turned_y);
            turned_x += shift_x;
            turned_y += shift_y;
            if (turned_x < 0 || turned_y < 0)
                continue;
            const size_t u_x = size_t(turned_x);
            const size_t u_y = size_t(turned_y);
            if (u_x >= theirs.width || u_y >= theirs.height)
                continue;
            if (theirs.lit[u_y * theirs.width + u_x] == 0)
                continue;
            intersection += 1;
        }
    }

    const size_t union_size = ours.count + theirs.count - intersection;

    return union_size == 0 ? 0. : double(intersection) / double(union_size);
}

// The height of the middle of every layer, from the thickness the file gives for each of them. The
// two files are slices of the same model, so matching a height is matching a cross section of the
// same shape however the two slicers count their layers.
std::vector<double> layer_middle_mm(const Pm5Archive& archive)
{
    std::vector<double> middles;
    middles.reserve(archive.layers().size());
    double z = 0.;
    for (const Pm5Layer& layer : archive.layers()) {
        z += layer.height_mm / 2.;
        middles.push_back(z);
        z += layer.height_mm / 2.;
    }

    return middles;
}

// The thickness of the first layer: the config's initial_layer_height, or the layer height when the
// resin leaves it at 0, which is what the community-sla resins do (M5.3.profiles-b2).
double first_layer_height(const Slic3r::Domain::ConfigView& view, double layer_height)
{
    const auto& values = view.values();
    const auto it      = values.find("initial_layer_height");
    if (it == values.end() || !it->second.holds_alternative<double>())
        return layer_height;
    const double first = it->second.get<double>();

    return first > 0. ? first : layer_height;
}

// Our own layer heights come from the config rather than from the container, because LAYERDEF's
// first entry carries the config's initial_layer_height, which the community-sla resin leaves at 0.
std::vector<double>
layer_middle_mm(size_t layer_count, const Slic3r::Domain::ConfigView& view, double layer_height)
{
    const double first = first_layer_height(view, layer_height);
    std::vector<double> middles;
    middles.reserve(layer_count);
    double z = 0.;
    for (size_t i = 0; i < layer_count; ++i) {
        const double height = i == 0 ? first : layer_height;
        z += height / 2.;
        middles.push_back(z);
        z += height / 2.;
    }

    return middles;
}

// The index of the layer whose middle is nearest to `z`.
size_t nearest_layer(const std::vector<double>& middles, double z)
{
    REQUIRE_FALSE(middles.empty());
    size_t best          = 0;
    double best_distance = std::abs(middles.front() - z);
    for (size_t i = 1; i < middles.size(); ++i) {
        const double distance = std::abs(middles[i] - z);
        if (distance < best_distance) {
            best_distance = distance;
            best          = i;
        }
    }

    return best;
}

// ---------------------------------------------------------------------------
// The Photon Mono M5 of the shipped preset bundle, loaded and evaluated the way the app does it.
// ---------------------------------------------------------------------------

// Loads the shipped preset bundle, selects the Photon Mono M5, and hands out the config the app
// would slice with. That is the point of the test: M5.4c found that every community-sla printer
// inherits display_mirror_x: true, and this is what the Photon Mono M5 inherits, so the mirroring
// is not something the test sets up itself. The bundle's local, user and config directories and the
// bundle cache it writes go into a scratch tree under the git-ignored test data dir, which is
// removed with the fixture.
struct M5ProfileFixture
{
    struct ScratchDir
    {
        fs::path path;
        std::string previous_data_dir;

        explicit ScratchDir(fs::path p) : path(std::move(p))
        {
            previous_data_dir = Slic3r::data_dir();
            Slic3r::set_data_dir(path.string());
        }

        ~ScratchDir()
        {
            Slic3r::set_data_dir(previous_data_dir);
            // The non-throwing overload: a throwing remove_all() out of a destructor would take the
            // whole test run with it.
            boost::system::error_code ec;
            fs::remove_all(path, ec);
        }

        ScratchDir(const ScratchDir&)            = delete;
        ScratchDir& operator=(const ScratchDir&) = delete;
    };

    // The scratch tree is the first member on purpose, so it is the last one destroyed: the
    // interactor owns a preset bundle that points into it, and it may only be removed once the
    // interactor is gone.
    ScratchDir scratch{Tests::get_datadir() / "datadir" / "pm5_workshop_orientation"};
    Slic3r::Test::SlaSlicingFixture slicer;
    Slic3r::Biz::Preset::IO::BundlePaths bundle_paths;

    M5ProfileFixture()
    {
        boost::nowide::nowide_filesystem();

        boost::system::error_code ec;
        fs::remove_all(scratch.path, ec);
        fs::create_directories(scratch.path / "local");
        fs::create_directories(scratch.path / "user");
        fs::create_directories(scratch.path / "config");

        // The slicing fixture's constructor pointed the data dir at the test data dir and loaded
        // the small test bundle, which carries only the SL1. This is the shipped bundle, and the
        // cache it writes belongs in the scratch tree as well.
        Slic3r::set_data_dir(scratch.path.string());
        bundle_paths = Slic3r::Biz::Preset::IO::BundlePaths{
            .app_bundle_path       = fs::path{TEST_APP_PRESETS_DIR}.string(),
            .local_bundle_path     = (scratch.path / "local").string(),
            .populate_local_bundle = false,
            .user_bundle_path      = (scratch.path / "user").string(),
            .user_config_path      = (scratch.path / "config").string(),
        };

        Slic3r::Biz::Preset::PresetInteractor& presets =
            slicer.project_interactor.preset_interactor();
        presets.set_use_hw_config_short_name(false);
        presets.load_preset_bundle(bundle_paths);
        slicer.project_interactor.new_project();
        select_printer(m5_printer_name);
        // SelectedPreset::config() needs a resin to build the SLA config out of; selecting a
        // printer is expected to pick one, and this only covers the case where it does not.
        if (presets.selected_printer_preset().materials.empty())
            select_first_material();
    }

    ~M5ProfileFixture()
    {
        // Selecting presets posted main-thread work, which the slicing interactors want drained
        // before they are gone. A destructor body runs before the members are destroyed, so this
        // is the last moment at which the interactor is still alive.
        slicer.dispatcher.dispatch_enqueued();
    }

    M5ProfileFixture(const M5ProfileFixture&)            = delete;
    M5ProfileFixture& operator=(const M5ProfileFixture&) = delete;

    void select_printer(std::string_view hw_config_name)
    {
        Slic3r::Biz::Preset::PresetInteractor& presets =
            slicer.project_interactor.preset_interactor();
        const Slic3r::Biz::Preset::PresetItemObservableList& printers = presets.printer_presets();
        std::optional<std::pair<std::string, std::string>> found; // hw config id, printer preset id
        for (size_t i = 0, n = printers.items().size(); i < n && !found.has_value(); ++i) {
            const Slic3r::Biz::Preset::PresetItem& item = printers.items().at(i);
            if (item.hw_printer_config_name == hw_config_name)
                found.emplace(item.hw_printer_config_id, item.id);
        }
        REQUIRE(found.has_value());
        presets.select_printer_preset(found->first, found->second);
    }

    void select_first_material()
    {
        Slic3r::Biz::Preset::PresetInteractor& presets =
            slicer.project_interactor.preset_interactor();
        const Slic3r::Domain::Preset::SelectedPreset& selected = presets.selected_printer_preset();
        for (const auto& entry : presets.get_material_presets(
                 slicer.project_interactor.selected_project_id(),
                 selected.hw_config.id,
                 selected.printer.id,
                 selected.print.id,
                 0
             ))
        {
            presets.select_material_preset(0, entry.first.get().id);
            return;
        }
    }

    // The config of the selected printer, print profile and resin, as the bundle evaluates them.
    Slic3r::Domain::ConfigPackSLA sla_config()
    {
        Slic3r::Biz::Preset::PresetInteractor& presets =
            slicer.project_interactor.preset_interactor();
        const Slic3r::Domain::Preset::SelectedPreset& selected = presets.selected_printer_preset();
        const std::string resin =
            selected.materials.empty() ? std::string("none") : selected.materials.front().name;
        INFO(message(
            "printer preset \""
            << selected.printer.name
            << "\", print profile \""
            << selected.print.name
            << "\", resin \""
            << resin
            << "\""
        ));

        const Slic3r::Domain::ConfigContainer* cc =
            slicer.workbench.project(slicer.project_interactor.selected_project_id())
                .find_config_container(
                    presets.selected_config_container_context().config_container_id
                );
        REQUIRE(cc != nullptr);
        const Slic3r::Domain::ConfigPack pack = cc->build_print_config();
        const auto* sla                       = std::get_if<Slic3r::Domain::ConfigPackSLA>(&pack);
        REQUIRE(sla != nullptr);

        return *sla;
    }
};

// The settings of the config that decide what the printer's display receives, so that a run says
// which profile it sliced with.
void report_profile(const Slic3r::Domain::ConfigPackSLA& config)
{
    const Slic3r::Domain::ConfigItems& printer = config.sla_printer_settings.items;
    const SLADisplayOrientation orientation =
        printer.opt("display_orientation").get<SLADisplayOrientation>();
    const char* orientation_name =
        orientation == SLADisplayOrientation::sladoPortrait ? "portrait" : "landscape";
    INFO(message("archive format: " << printer.opt("sla_archive_format").get<std::string>()));
    INFO(message(
        "display: "
        << printer.opt("display_pixels_x").get<int>()
        << " x "
        << printer.opt("display_pixels_y").get<int>()
        << " px, "
        << printer.opt("display_width").get<double>()
        << " x "
        << printer.opt("display_height").get<double>()
        << " mm, "
        << orientation_name
    ));
    // The two flags this todo is about: every community-sla printer inherits mirror_x.
    INFO(message(
        "mirroring: x "
        << printer.opt("display_mirror_x").get<bool>()
        << ", y "
        << printer.opt("display_mirror_y").get<bool>()
    ));
    INFO(message(
        "layer height: "
        << config.sla_print_settings.items.opt("layer_height").get<double>()
        << " mm, first layer "
        << config.sla_material_settings.items.opt("initial_layer_height").get<double>()
        << " mm"
    ));
    INFO(message(
        "supports "
        << config.sla_print_settings.items.opt("supports_enable").get<bool>()
        << ", pad "
        << config.sla_print_settings.items.opt("pad_enable").get<bool>()
    ));
}

// The model of the reference file, put in the middle of the plate. Where it sits is irrelevant to
// the comparison, which lines the layers up by their centroids, but it does have to be on the
// display for there to be anything to compare.
Slic3r::Domain::Model load_model(const fs::path& path, double plate_centre_x, double plate_centre_y)
{
    auto mesh = Slic3r::Biz::load_stl(path.string());
    REQUIRE(mesh.has_value());
    REQUIRE_FALSE(mesh->empty());

    const Slic3r::Domain::BoundingBox3d box = mesh->bounding_box();
    REQUIRE(box.defined);
    // The mesh translation takes floats and the bounding box is in doubles, so the offset is
    // computed in doubles and cast once, here.
    const Slic3r::Domain::Vec3f offset{
        float(plate_centre_x - (box.min.x() + box.max.x()) / 2.),
        float(plate_centre_y - (box.min.y() + box.max.y()) / 2.),
        float(-box.min.z())
    };
    mesh->translate(offset);

    Slic3r::Domain::Model model;
    Slic3r::Domain::ModelObject* object = model.add_object();
    Slic3r::Biz::Algorithms::add_volume(object, *mesh);
    object->add_instance();
    Slic3r::Biz::Algorithms::ensure_on_bed(*object);

    return model;
}

// The model of the reference file, wherever it is kept under the local-samples folder.
fs::path benchmark_model(const fs::path& samples)
{
    for (const fs::path& candidate : {samples / "benchmark models" / "5.stl", samples / "5.stl"}) {
        if (fs::exists(candidate))
            return candidate;
    }

    FAIL(message(
        "The model the reference file was sliced from was not found. Looked for "
        << (samples / "benchmark models" / "5.stl").string()
        << " and "
        << (samples / "5.stl").string()
        << " under SLA_LOCAL_SAMPLES."
    ));

    return fs::path{};
}

} // namespace

TEST_CASE(
    "The .pm5 we write and Photon Workshop's own one show the model the same way round",
    "[.local][export][sla][pm5][orientation]"
)
{
    const char* samples = std::getenv("SLA_LOCAL_SAMPLES");
    if (samples == nullptr || samples[0] == '\0') {
        SKIP(
            "SLA_LOCAL_SAMPLES is not set: it has to name the local-samples folder holding "
            "anycubic-photon-mono-m5/01_5.pm5 and the 5.stl it was sliced from"
        );
    }
    const fs::path samples_root(samples);
    const fs::path reference_path = samples_root / reference_file;
    REQUIRE(fs::exists(reference_path));
    const fs::path model_path = benchmark_model(samples_root);

    // The reference, read as the container it is: the resolution of the display it was rasterized
    // for, and one entry per layer saying where that layer's image is and how thick the layer is.
    Pm5Archive reference;
    reference.open(reference_path);
    REQUIRE(reference.res_x() > 0u);
    REQUIRE(reference.res_y() > 0u);
    REQUIRE(reference.layers().size() > 4u);
    INFO(message(
        "reference "
        << reference_path.string()
        << ": "
        << reference.res_x()
        << " x "
        << reference.res_y()
        << " px, "
        << reference.layers().size()
        << " layers"
    ));

    // Our own file: the same model, sliced with the Photon Mono M5 of the shipped bundle and
    // written by the .pm5 writer, so the two layer images are the pictures each printer's display
    // would get.
    M5ProfileFixture fixture;
    const Slic3r::Domain::ConfigPackSLA config = fixture.sla_config();
    report_profile(config);

    const double display_width =
        config.sla_printer_settings.items.opt("display_width").get<double>();
    const double display_height =
        config.sla_printer_settings.items.opt("display_height").get<double>();
    const Slic3r::Domain::Model model =
        load_model(model_path, display_width / 2., display_height / 2.);

    const std::shared_ptr<const Slic3r::Biz::Slicing::SLAResultData> sla_result =
        fixture.slicer.slice_sla_model(model, config);
    REQUIRE(sla_result != nullptr);
    REQUIRE(sla_result->files.type == FileDataType::pm5);
    REQUIRE(sla_result->files.data.size() > 4u);

    register_sla_archive_formats();
    const std::unique_ptr<Slic3r::Biz::PrintHost::Sla::ISlaArchiveFormat> format =
        SlaArchiveFormatRegistry::instance().find_by_file_data_type(FileDataType::pm5);
    REQUIRE(format != nullptr);
    const fs::path written_path = fixture.scratch.path / "5.pm5";
    REQUIRE_NOTHROW(format->store(written_path.string(), *sla_result));

    Pm5Archive ours;
    ours.open(written_path);
    INFO(message(
        "ours "
        << written_path.string()
        << ": "
        << ours.res_x()
        << " x "
        << ours.res_y()
        << " px, "
        << ours.layers().size()
        << " layers"
    ));
    // The two files have to be on the same pixel grid for a pixel-for-pixel comparison to mean
    // anything, and the reference is the one that says what the M5's display is.
    REQUIRE(ours.res_x() == reference.res_x());
    REQUIRE(ours.res_y() == reference.res_y());

    const std::vector<double> reference_z = layer_middle_mm(reference);
    const double layer_height = Slic3r::Domain::sla_effective_layer_height(sla_result->config);
    REQUIRE(layer_height > 0.);
    const std::vector<double> our_z =
        layer_middle_mm(sla_result->files.data.size(), sla_result->config, layer_height);

    // Three layers up the model, where the cross sections differ from each other.
    const std::array<double, 3> fractions{0.25, 0.5, 0.75};
    for (const double fraction : fractions) {
        const size_t reference_index = nearest_layer(reference_z, fraction * reference_z.back());
        const size_t our_index       = nearest_layer(our_z, reference_z[reference_index]);
        INFO(message(
            "at "
            << fraction * 100.
            << "% of the height: reference layer "
            << reference_index
            << " at z "
            << reference_z[reference_index]
            << " mm, our layer "
            << our_index
            << " at z "
            << our_z[our_index]
            << " mm"
        ));
        if (std::abs(our_z[our_index] - reference_z[reference_index]) > layer_height / 2.) {
            WARN(
                "the two layers are more than half a layer height apart, so they are not the same "
                "cross section of the model"
            );
        }

        const Mask theirs = mask_from_pw0(
            reference.layer_image(reference_index),
            reference.res_x(),
            reference.res_y()
        );
        const Mask mine = mask_from_pw0(ours.layer_image(our_index), ours.res_x(), ours.res_y());
        REQUIRE(theirs.count > 0u);
        REQUIRE(mine.count > 0u);
        INFO(message(
            "lit pixels: reference "
            << theirs.count
            << " at centroid "
            << theirs.centre_x
            << ", "
            << theirs.centre_y
            << ", ours "
            << mine.count
            << " at centroid "
            << mine.centre_x
            << ", "
            << mine.centre_y
        ));

        // Identity first, and a later turn only wins on a strictly better score, so a layer that
        // happens to be symmetric counts for identity.
        Turn best       = Turn::identity;
        double best_iou = 0.;
        for (const Turn turn : TURNS) {
            const double iou = iou_of_turned(mine, theirs, turn);
            INFO(message("IoU under " << turn_name(turn) << ": " << iou));
            if (iou > best_iou) {
                best_iou = iou;
                best     = turn;
            }
        }
        if (best != Turn::identity) {
            WARN(message(
                "our layer matches Photon Workshop's under "
                << turn_name(best)
                << " (IoU "
                << best_iou
                << "), not under identity"
            ));
        }

        CHECK(best == Turn::identity);
    }
}
