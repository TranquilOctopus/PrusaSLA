#include "Slic3r/App/Plater/SlaSupportPreviewSchedule.hpp"

#include "Slic3r/Biz/Platform/PlatformServices.hpp"
#include "Slic3r/Biz/Platform/TimerQueue.hpp"

#include <memory>
#include <utility>

namespace Slic3r::App::Plater {

namespace {

/// The platform debounce: one entry of the timer queue the platform services hold, which runs it
/// on the main thread. The queue is asked for on every call, like everything else here does, so a
/// dispatcher the platform replaces leaves nothing dangling.
class PlatformDebounceTimer final : public ISlaSupportPreviewTimer
{
public:
    TimerId set(std::chrono::milliseconds delay, std::function<void()> callback) override
    {
        return Biz::Platform::PlatformServices::instance().timer_queue().set_timer(delay, std::move(callback)).id;
    }

    void cancel(TimerId timer) override
    {
        if (timer == ISlaSupportPreviewTimer::no_timer) {
            return;
        }
        Biz::Platform::TimerQueue& queue = Biz::Platform::PlatformServices::instance().timer_queue();
        if (!queue.is_timer_running(Biz::Platform::TimerQueue::TimerID{timer})) {
            return; // it already ran, or was cancelled before
        }
        queue.cancel_timer(Biz::Platform::TimerQueue::TimerID{timer});
    }
};

} // namespace

std::unique_ptr<ISlaSupportPreviewTimer> SlaSupportPreviewSchedule::platform_timer()
{
    return std::make_unique<PlatformDebounceTimer>();
}

SlaSupportPreviewSchedule::SlaSupportPreviewSchedule(ISlaSupportPreviewTimer& timer, BuildFn build) :
    m_timer(timer),
    m_build(std::move(build))
{}

SlaSupportPreviewSchedule::~SlaSupportPreviewSchedule()
{
    clear();
}

void SlaSupportPreviewSchedule::request(const Request& request)
{
    const std::size_t object_id = request.object_id.id;

    // Whatever is waiting for this object is stale now: only the newest key is worth building.
    drop_waiting(object_id);

    // And whatever is building it right now is building a key that does not exist anymore. It is
    // stopped through the stop the caller gave in, the one the support tool already reads.
    if (m_building.has_value() && m_building->object_id == request.object_id && m_cancel_building) {
        m_cancel_building();
    }

    m_signatures[object_id] = request.signature;

    Waiting waiting;
    waiting.request = request;
    waiting.serial  = ++m_request_serial;
    waiting.timer   = m_timer.set(
        debounce,
        [this, object_id, serial = waiting.serial, life = std::weak_ptr<int>(m_life)]() {
            on_window_elapsed(object_id, serial, life);
        }
    );
    m_waiting[object_id] = std::move(waiting);
}

void SlaSupportPreviewSchedule::forget(Domain::ObjectID object_id)
{
    // A signature nobody asked for any more: what is waiting and what is running for this object
    // is dead, and no result of it can come in as current afterwards.
    const auto signature = m_signatures.find(object_id.id);
    if (signature != m_signatures.end()) {
        ++signature->second;
    }
    drop_waiting(object_id.id);

    if (m_building.has_value() && m_building->object_id == object_id && m_cancel_building) {
        m_cancel_building();
    }
}

void SlaSupportPreviewSchedule::clear()
{
    // The caller joins its worker right after this, so asking for the stop is all that is left to
    // do here.
    if (m_building.has_value() && m_cancel_building) {
        m_cancel_building();
    }

    m_waiting.clear();

    // The counters keep counting up over a project change, so a debounce armed before it can
    // never fire for the project after it.
    for (auto& entry : m_signatures) {
        ++entry.second;
    }

    m_building.reset();
    m_cancel_building = nullptr;
}

void SlaSupportPreviewSchedule::begin_build(const Request& request, CancelFn cancel)
{
    m_building        = request;
    m_cancel_building = std::move(cancel);
}

void SlaSupportPreviewSchedule::finish_build()
{
    m_building.reset();
    m_cancel_building = nullptr;
}

bool SlaSupportPreviewSchedule::is_current(const Request& request) const
{
    const auto it = m_signatures.find(request.object_id.id);
    return it != m_signatures.end() && it->second == request.signature;
}

std::size_t SlaSupportPreviewSchedule::waiting_count() const
{
    return m_waiting.size();
}

bool SlaSupportPreviewSchedule::building() const
{
    return m_building.has_value();
}

void SlaSupportPreviewSchedule::on_window_elapsed(
    std::size_t object_id, std::uint64_t serial, const std::weak_ptr<int>& life
)
{
    if (life.expired()) {
        return; // the service is gone, its destructor dropped everything already
    }

    const auto it = m_waiting.find(object_id);
    if (it == m_waiting.end() || it->second.serial != serial) {
        return; // a newer request took this object's place while the window ran
    }

    const Request request = it->second.request;
    m_timer.cancel(it->second.timer);
    m_waiting.erase(it);

    if (m_build) {
        m_build(request);
    }
}

void SlaSupportPreviewSchedule::drop_waiting(std::size_t object_id)
{
    const auto it = m_waiting.find(object_id);
    if (it == m_waiting.end()) {
        return;
    }
    m_timer.cancel(it->second.timer);
    m_waiting.erase(it);
}

} // namespace Slic3r::App::Plater
