#pragma once

#include "Slic3r/Domain/ElementRef.hpp"
#include "Slic3r/Domain/Project.hpp"
#include "Slic3r/Domain/Model.hpp"
#include "Slic3r/Domain/TriangleMesh.hpp"

#include <functional>
#include <string>
#include <vector>
#include <boost/filesystem/path.hpp>
#include <tl/expected.hpp>

namespace Slic3r::Biz {
class IMessageDialogProvider;
class ProjectInteractor;
namespace Scene { class SceneInteractor; }
} // namespace Slic3r::Biz

namespace Slic3r::Biz::Platform::JobManager {
class ProgressTracker;
} // namespace Slic3r::Biz::Platform::JobManager

namespace Slic3r::Biz::JThread {
class StopToken;
} // namespace Slic3r::Biz::JThread

namespace Slic3r::Domain::Preset {
struct Bundle;
} // namespace Slic3r::Domain::Preset

namespace Slic3r::Biz::FileLoadingLogic {

/**
 * Loading project from file.
 *
 * @note During loading, the file will be scanned for zero-volume objects.
 * Any found will be automatically removed, and the user will be notified.
 */
Domain::Project load_file_as_project(
    const boost::filesystem::path& project_file_path,
    const Domain::Preset::Bundle& bundle,
    IMessageDialogProvider* dialog_provider
);

/**
 * Load meshes (e.g., STL, OBJ) and complex models (e.g., 3MF) from multiple source files
 * and insert them into the scene graph.
 *
 * @return Instance-level element refs of all newly added objects.
 */
Domain::ElementRefs import_files_and_add_to_scene(
    const std::vector<boost::filesystem::path>& input_file_paths,
    int tool_count,
    Scene::SceneInteractor& scene_interactor,
    const Domain::Vec2d& bed_center,
    IMessageDialogProvider* dialog_provider
);

/**
 * Load meshes from multiple source files and add them into selected object
 */
void import_volumes_into_selected_object(
    const std::vector<boost::filesystem::path>& input_file_paths,
    const Domain::ModelVolumeType& type,
    Scene::SceneInteractor& scene_interactor,
    IMessageDialogProvider* dialog_provider
);

/**
 * Reads a 3D model from the specified input file and processes it.
 *
 * The input file may be a simple geometry file (e.g., STL, OBJ) or a more complex project file.
 * This function attempts to load the file, process its data, and construct a `Domain::Model` object.
 * If the file contains valid model data, a `Domain::Model` is returned. If it contains a valid mesh,
 * the mesh will be added to the model and default instances are created. If neither valid model nor mesh
 * data is found, an error message is returned instead.
 *
 * @param input_file A string specifying the path to the input file to be read and processed.
 * @return A `tl::expected` object containing the `Domain::Model` object on success, or an error string
 *         on failure.
 */
tl::expected<Domain::Model, std::string>
read_model_from_file(const std::string& input_file, IMessageDialogProvider* dialog_provider);

/**
 * Returns all file extensions that the slicer can import (projects and models).
 *
 * Extensions are lowercase and dot-prefixed (e.g. ".stl", ".3mf").
 * This is the single source of truth for importable formats; both
 * is_supported_file() and the Wildcards file-dialog strings are derived from it.
 */
const std::vector<std::string>& get_import_extensions();

/**
 * Checks if the given file is a project file.
 *
 * A project file is defined as a file with a ".3mf" extension.
 * The check is case-insensitive.
 *
 * @param input_file The name of the file to check.
 * @return True if the input_file has a ".3mf" extension, false otherwise.
 */
bool is_project_file(const std::string& input_file);

/**
 * Checks if the given file is a supported input file (project or model).
 *
 * Delegates to get_import_extensions() — no separate extension list to maintain.
 * The check is case-insensitive.
 *
 * @param input_file The name of the file to check.
 * @return True if the file can be loaded by the slicer, false otherwise.
 */
bool is_supported_file(const std::string& input_file);

/**
 * Returns the file extensions of a foreign resin profile (Chitubox .cfg/.cfgx, Lychee .lyr).
 *
 * Extensions are lowercase and dot-prefixed, the same shape as get_import_extensions(). A profile
 * is not a model: a file with one of these extensions goes to the resin import review dialog and
 * not to the scene.
 */
const std::vector<std::string>& get_resin_profile_extensions();

/**
 * Checks if the given file is a foreign resin profile rather than something the model loader reads.
 * Delegates to get_resin_profile_extensions().
 * The check is case-insensitive.
 *
 * @param input_file The name of the file to check.
 * @return True if the input_file carries a resin profile extension, false otherwise.
 */
bool is_resin_profile_file(const std::string& input_file);

/**
 * @brief What a drop of files is made of, split by what is done with each part.
 *
 * A drop is one gesture and may carry both a foreign resin profile and models, which go to two
 * different places: the profile to the review dialog, the models to the scene. This is the split,
 * without the loading, so what a drop of a given set of files ends up doing can be tested.
 */
struct DropRouting
{
    /// The files the model loader takes: the models of the drop, and its project files.
    std::vector<boost::filesystem::path> files_to_load;
    /// @brief The first resin profile of the drop, the one whose review dialog is opened. Empty
    /// when the drop carries no profile.
    boost::filesystem::path profile_to_review;
    /// @brief The profiles after the first one. The review dialog is for a single profile, so
    /// they are named to the user rather than silently dropped.
    std::vector<boost::filesystem::path> profiles_left_out;
};

/**
 * @brief Split a drop of files into the models to load, the profile to review and the profiles
 * that are left out.
 *
 * Pure: it reads the extension of every file and nothing else, so the routing of a drop is what
 * the tests check. A file neither is_resin_profile_file() nor is_supported_file() is left out of
 * every list, exactly as it was before this split.
 *
 * @param files The paths of the drop, in the order they were dropped.
 */
DropRouting route_dropped_files(const std::vector<boost::filesystem::path>& files);

/**
 * @brief Whether a path is one of the sliced-print archives the import reads as a job
 * (.sl1 and .sl1s).
 *
 * Case-insensitive, suffix only, the same test the import path makes. An archive is not a model
 * file format: it is read off the main thread and carries the print settings of the print it was
 * written for, so the import path splits on this.
 */
bool is_sla_archive_file(const std::string& input_file);

/**
 * @brief One archive as it came back from the reader on the worker thread.
 *
 * The mesh is moved onto the build plate on the main thread, so the worker keeps nothing but
 * plain data. @ref error is empty on success; then the mesh is not empty either.
 */
struct SlaArchiveImport
{
    boost::filesystem::path path;
    std::string             file_name;
    Domain::TriangleMesh    mesh;  ///< empty when the read was cancelled
    std::string             error; ///< empty on success
};

/**
 * @brief What reading one archive gives back.
 *
 * A cancelled read is neither an error nor a mesh: @ref cancelled is set and nothing else is,
 * which is what keeps a cancelled archive from reaching the build plate.
 */
struct SlaArchiveReadResult
{
    Domain::TriangleMesh mesh;
    std::string          error; ///< empty unless @ref cancelled, then why the read failed
    bool                 cancelled{false};
};

/**
 * @brief How one archive is read off the main thread.
 *
 * Replaceable so a test can drive the progress and the cancel of read_sla_archives() without
 * building a 200 MB archive; the production path leaves it empty and gets default_sla_archive_read().
 */
using SlaArchiveRead = std::function<SlaArchiveReadResult(const boost::filesystem::path &path,
                                                          const std::function<bool()> &stop,
                                                          const std::function<void(double)> &progress)>;

/// The reader read_sla_archives() uses when the caller names none: import_sl1_archive().
SlaArchiveRead default_sla_archive_read();

/**
 * @brief Read a list of archives, reporting the progress and honouring the stop.
 *
 * The body of the import job with the threads taken out, so what the tests check is what the job
 * runs. @p stop is asked before each archive and the reader is given it as well, so a cancel lands
 * within one layer image rather than at the end of the archive. @p progress is called with the
 * share of the archives done, from 0 to 1, and never decreases.
 *
 * An empty result means nothing was read: the caller cancelled, or stop was already true. A
 * cancelled archive contributes no entry at all, which is how a cancelled import adds nothing.
 *
 * @param paths The archives to read. A file that is not an archive is an error entry.
 * @param read The reader to use; default_sla_archive_read() when empty.
 */
std::vector<SlaArchiveImport> read_sla_archives(
    const std::vector<boost::filesystem::path>& paths,
    const std::function<bool()>&              stop,
    const std::function<void(double)>&        progress,
    SlaArchiveRead                             read = {});

/**
 * @brief read_sla_archives() as the JobManager wants a job function: a stop token and a progress
 * tracker, then the archives. This is the function the import job is created with, so it takes
 * exactly the arguments that job is created with: the paths by value, the way every other job
 * function takes its data, so the job owns them on its worker thread.
 */
std::vector<SlaArchiveImport> import_sla_archives_local(
    Biz::JThread::StopToken stop_token,
    Biz::Platform::JobManager::ProgressTracker progress,
    std::vector<boost::filesystem::path> paths);

/**
 * @brief Put one archive mesh on the build plate, as import_files_and_add_to_scene() does for a
 * mesh file. Separate from the read because only the main thread may touch the scene.
 */
Domain::ElementRefs add_sla_archive_to_scene(Domain::TriangleMesh&& mesh,
                                             const boost::filesystem::path& file_path,
                                             Scene::SceneInteractor& scene_interactor,
                                             const Domain::Vec2d& bed_center);

} // namespace Slic3r::Biz::FileLoadingLogic
