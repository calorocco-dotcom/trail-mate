#include "ui/support/native_navigation_session.h"
#include <cassert>
#include <cstring>
#include <memory>

namespace nav = platform::ui::navigation;
using Session = ui::support::NativeNavigationSession;
static nav::Snapshot response;
static nav::Request requested;
static unsigned submitted = 0, cancelled = 0, leases = 0;
static bool submit_allowed = true, save_allowed = true;
namespace platform::ui::navigation
{
bool supported() { return true; }
bool submit(const Request& request)
{
    ++submitted;
    if (!submit_allowed) return false;
    requested = request;
    response = {};
    response.status = Status::Calculating;
    response.revision = 1;
    return true;
}
bool poll(Snapshot& snapshot)
{
    if (snapshot.revision == response.revision) return false;
    snapshot = response;
    return true;
}
bool save()
{
    if (!save_allowed || response.status != Status::Ready) return false;
    response.status = Status::Saved;
    ++response.revision;
    std::strcpy(response.path, "/routes/navigation/new.tmroute");
    return true;
}
void cancel()
{
    ++cancelled;
    response = {};
}
} // namespace platform::ui::navigation
namespace platform::ui::gps
{
void acquire_power_lease(const char*) { ++leases; }
void release_power_lease(const char*)
{
    assert(leases);
    --leases;
}
} // namespace platform::ui::gps
int main()
{
    auto session = std::make_unique<Session>();
    assert(leases == 1);
    session->destination({0, 20000});
    navigation::Guidance guide;
    guide.status = navigation::GuidanceStatus::OffRoute;
    guide.observed_at = 1000;
    assert(session->tick(guide, {0, 10000}, true, 0, 1000) == Session::Action::None);
    assert(session->phase() == Session::Phase::Replanning && submitted == 1);
    assert(requested.origin.longitude_e7 == 10000 && requested.destination.longitude_e7 == 20000);
    response.status = nav::Status::Ready;
    ++response.revision;
    save_allowed = false;
    session->tick(guide, {0, 10000}, true, 0, 1100);
    assert(session->phase() == Session::Phase::Replanning);
    save_allowed = true;
    // Same Ready revision: still retry the worker-completion/save race.
    assert(session->tick(guide, {0, 10000}, true, 0, 1200) == Session::Action::ApplyCandidate);
    assert(session->phase() == Session::Phase::Applying && std::strstr(session->candidatePath(), "new.tmroute"));
    session->applied(false);
    assert(session->phase() == Session::Phase::Failed && leases == 1);
    guide.observed_at = 50000;
    session->tick(guide, {0, 10000}, true, 0, 50000);
    assert(submitted == 1); // Continuous off-route failure cannot loop automatically.
    assert(!session->replan({0, 10000}, true, 6000, 50000));
    assert(session->replan({0, 10000}, true, 0, 50000) && submitted == 2);
    response.status = nav::Status::NoPath;
    ++response.revision;
    session->tick(guide, {0, 10000}, true, 0, 50000);
    assert(session->phase() == Session::Phase::Failed && session->failure() == nav::Status::NoPath);
    assert(session->replan({0, 10000}, true, 0, 51000));
    const auto before_cancel = cancelled;
    session->cancelReplan();
    assert(cancelled == before_cancel + 1 && session->phase() == Session::Phase::Following);
    guide.status = navigation::GuidanceStatus::Following;
    guide.observed_at = 60000;
    session->tick(guide, {0, 10000}, true, 0, 60000);
    guide.status = navigation::GuidanceStatus::OffRoute;
    guide.observed_at = 90000;
    session->setAutomatic(false);
    const auto before_disabled = submitted;
    session->tick(guide, {0, 10000}, true, 0, 90000);
    assert(submitted == before_disabled);
    session->setAutomatic(true);
    session->tick(guide, {0, 10000}, true, 0, 90000);
    assert(submitted == before_disabled + 1);
    guide.status = navigation::GuidanceStatus::Arrived;
    guide.observed_at = 90000;
    assert(session->tick(guide, {0, 20000}, true, 6000, 90000) == Session::Action::None);
    assert(session->tick(guide, {0, 20000}, true, 0, 96000) == Session::Action::None); // stale guide
    guide.observed_at = 97000;
    assert(session->tick(guide, {0, 20000}, true, 0, 97000) == Session::Action::Finish);
    assert(session->phase() == Session::Phase::Arrived && leases == 0);
    assert(session->tick(guide, {0, 20000}, true, 0, 97000) == Session::Action::None);
    session.reset();
    assert(leases == 0);
    session = std::make_unique<Session>();
    session->destination({0, 20000});
    submit_allowed = false;
    assert(!session->replan({0, 0}, true, 0, 100000));
    assert(session->phase() == Session::Phase::Failed);
    session->stop();
    assert(leases == 0 && session->phase() == Session::Phase::Stopped);
}
