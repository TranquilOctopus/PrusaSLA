#pragma once

#include "Slic3r/Domain/ObjectID.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>

namespace Slic3r::App::Plater {

/**
 * @brief A callback on the main thread, later.
 *
 * The platform runs these on Biz::Platform::TimerQueue, a test fires them when it says so, which
 * is what makes the debounce of SlaSupportPreviewSchedule testable without waiting for it.
 */
class ISlaSupportPreviewTimer
{
public:
    using TimerId = int;

    static constexpr TimerId no_timer{-1};

    virtual ~ISlaSupportPreviewTimer() = default;

    /// @brief Runs @p callback on the main thread in @p delay and returns its handle.
    virtual TimerId set(std::chrono::milliseconds delay, std::function<void()> callback) = 0;

    /// @brief Drops the callback behind @p timer. Nothing happens if it already ran or was
    /// cancelled before.
    virtual void cancel(TimerId timer) = 0;
};

/**
 * @brief Turns a burst of preview edits into one build per object, and stops the build a newer
 * edit made pointless.
 *
 * Every slicing input, selection or point edit of SlaSupportPreviewService changes the key of the
 * objects it touches and asks for their tree again. Done naively that is one build (and one mesh
 * clone) per event, so dragging a point or moving a slider queues dozens of them. Here every
 * request waits for a fixed window and only the last one of a burst survives: the build is started
 * once the window passed without a newer key. A build that is already running when its key changes
 * is stopped through the cancel the caller handed in at begin_build(), the same stop the support
 * tool accepts, instead of being finished and thrown away.
 *
 * The service keeps the debounce per object, so a big model and a small one do not wait for each
 * other, and nothing here touches the scene: results are installed by the caller on the main
 * thread, and only while is_current() still holds.
 */
class SlaSupportPreviewSchedule
{
public:
    /// @brief How long one object's edits are collected before its tree is built. Long enough to
    /// swallow a drag or a slider sweep, short enough that the preview still feels alive.
    static constexpr std::chrono::milliseconds debounce{200};

    /// @brief One build: the object and the fingerprint of everything its tree is built from.
    /// The signature changes with the key, and it is what makes an older result stale: a request
    /// of the same signature for the same object is the very same build.
    struct Request
    {
        Domain::ObjectID object_id;
        std::uint64_t    signature{0};

        bool operator==(const Request& rhs) const = default;
    };

    /// @brief Runs on the main thread once the debounce window of @p request passed. The caller
    /// starts the build from here, on a worker thread.
    using BuildFn = std::function<void(const Request& request)>;

    /// @brief Stops the build in flight. The caller hands in the stop of its worker thread here.
    using CancelFn = std::function<void()>;

    SlaSupportPreviewSchedule(ISlaSupportPreviewTimer& timer, BuildFn build);
    ~SlaSupportPreviewSchedule();

    SlaSupportPreviewSchedule(const SlaSupportPreviewSchedule&) = delete;
    SlaSupportPreviewSchedule& operator=(const SlaSupportPreviewSchedule&) = delete;

    /// @brief The key of @p request's object changed. Its build waits for the debounce window, a
    /// waiting build of the same object is dropped and a running one is cancelled right away.
    void request(const Request& request);

    /// @brief The object left the plate or lost its preview: whatever waits for it is dropped and
    /// whatever builds it is cancelled.
    void forget(Domain::ObjectID object_id);

    /// @brief Another project was selected: every waiting build is dropped and the build in
    /// flight is cancelled. Anything that was armed before is dead.
    void clear();

    /// @brief @p request starts building on a worker thread, @p cancel stops it.
    void begin_build(const Request& request, CancelFn cancel);

    /// @brief The worker is done with @p request, whether it built a tree or was cancelled.
    void finish_build();

    /// @brief True while no newer key arrived for this object since @p request was made: a result
    /// that is not current is thrown away, a queued build that is not current is not run at all.
    bool is_current(const Request& request) const;

    /// @brief How many objects wait for their window.
    std::size_t waiting_count() const;

    /// @brief True while a build is in flight.
    bool building() const;

    /// @brief The platform debounce: an entry of the timer queue the platform services hold, which
    /// runs it on the main thread.
    static std::unique_ptr<ISlaSupportPreviewTimer> platform_timer();

private:
    struct Waiting
    {
        Request                          request;
        std::uint64_t                    serial{0};
        ISlaSupportPreviewTimer::TimerId timer{ISlaSupportPreviewTimer::no_timer};
    };

    void on_window_elapsed(std::size_t object_id, std::uint64_t serial, const std::weak_ptr<int>& life);
    void drop_waiting(std::size_t object_id);

    ISlaSupportPreviewTimer& m_timer;
    BuildFn                 m_build;

    // The newest signature per object. It only ever counts up, also over clear(), so a debounce
    // that was armed before a project change can never fire for the one after it.
    std::unordered_map<std::size_t, std::uint64_t> m_signatures;
    std::unordered_map<std::size_t, Waiting>       m_waiting;
    // Counts the requests, so that a debounce knows it is the newest one and not an older one that
    // happens to carry the same signature.
    std::uint64_t m_request_serial{0};

    std::optional<Request> m_building;
    CancelFn               m_cancel_building;

    // A timer callback may still be on its way to the main thread when this goes away; it carries
    // a weak reference to it and does nothing once it expired.
    std::shared_ptr<int> m_life{std::make_shared<int>(0)};
};

} // namespace Slic3r::App::Plater
