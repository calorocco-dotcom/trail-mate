#include "host_worker.h"
#include "platform/esp/arduino_common/navigation/sd_route_file.h"
#include "platform/ui/navigation_runtime.h"
#include <iostream>
namespace nav = platform::ui::navigation;
namespace sd = platform::esp::arduino_common::storage;
int main(int argc, char** argv)
{
    assert(argc == 3 || argc == 4);
    const bool london = argc == 4;
    sd::paths["/maps/tmap/osm/host.tmap"] = argv[1];
    sd::paths["/maps/navigation/legacy.tmap"] = argv[1];
    sd::output_prefix = argv[2];
    nav::Request request;
    request.origin.latitude_e7 = london ? 515009000 : 0;
    request.origin.longitude_e7 = london ? -1245000 : 0;
    request.destination.latitude_e7 = london ? 514036000 : 0;
    request.destination.longitude_e7 = london ? -3377000 : 20000;
    std::strcpy(request.origin.name, "Start");
    std::strcpy(request.destination.name, "Finish");
    request.origin.name[0] = 0;
    assert(!nav::submit(request));
    std::strcpy(request.origin.name, "Start");
    request.destination.name[0] = 0;
    assert(!nav::submit(request));
    std::strcpy(request.destination.name, "Finish");
    sd::ready = false;
    assert(!nav::submit(request));
    sd::ready = true;
    host_worker::fail_mutex_create = true;
    assert(!nav::submit(request));
    host_worker::fail_mutex_create = false;
    host_worker::fail_allocation = true;
    assert(!nav::submit(request));
    host_worker::fail_allocation = false;
    host_worker::deny_lock = true;
    assert(!nav::submit(request));
    host_worker::deny_lock = false;
    host_worker::fail_task_create = true;
    assert(!nav::submit(request) && !host_worker::task && host_worker::live == 0);
    host_worker::fail_task_create = false;
    // A file occupying the work-directory path must not pass the directory check.
    sd::paths[nav::directory()] = argv[1];
    assert(nav::submit(request));
    host_worker::run();
    nav::Snapshot collision;
    assert(nav::poll(collision) && collision.status == nav::Status::StorageUnavailable);
    nav::cancel();
    sd::paths.erase(nav::directory());
    assert(host_worker::live == 0 && sd::open_files == 0);
    // The existing /routes parent matches the real card; mkdir alone fails on it.
    assert(sd::sd_is_directory("/routes") && !sd::sd_mkdir("/routes", sd::media));
    assert(nav::submit(request));
    assert(!nav::submit(request));
    nav::Snapshot snapshot;
    assert(nav::poll(snapshot) && snapshot.status == nav::Status::Calculating);
    host_worker::run();
    assert(nav::poll(snapshot) && snapshot.status == nav::Status::Ready);
    assert(sd::sd_is_directory(nav::directory()) && !sd::sd_mkdir(nav::directory(), sd::media));
    const auto peak = host_worker::peak;
    const auto read_bytes = sd::read_bytes;
    assert(snapshot.points > 2 && snapshot.distance_m > (london ? 20000 : 300));
    std::vector<std::string> names;
    assert(nav::list(names) && names.empty()); // Pending files never enter the user list.
    assert(nav::save());
    assert(nav::poll(snapshot) && snapshot.status == nav::Status::Saved);
    const std::string saved = snapshot.path;
    assert(saved.find(".tmroute") != std::string::npos);
    assert(nav::list(names) && names.size() == 1);
    const auto saved_name = names.front();
    nav::cancel();
    assert(sd::sd_exists(saved.c_str()) && host_worker::live == 0 && sd::open_files == 0);
    platform::esp::navigation::SdRouteFile file;
    assert(file.openRead(saved.c_str(), sd::media) == tmap::Status::Ok);
    tmap::RouteArchiveWorkspace workspace;
    tmap::RouteArchiveReader reader(workspace);
    assert(reader.open(file) == tmap::Result::Ok);
    tmap::RouteArchiveValidationCursor validation;
    auto result = reader.beginValidation(validation);
    while (result == tmap::Result::Ok || result == tmap::Result::More)
    {
        result = reader.validationStep(validation, 64);
        if (result == tmap::Result::Ok) break;
    }
    assert(result == tmap::Result::Ok);
    const auto archive_bytes = file.size();
    file.close();
    assert(nav::submit(request));
    host_worker::delay_hook = []
    {
        host_worker::delay_hook = nullptr;
        host_worker::deny_cancel_lock = true;
        nav::cancel(); // Lost UI mutex admission must not lose the cancellation.
        host_worker::deny_cancel_lock = false;
    };
    host_worker::run();
    assert(host_worker::live == 0 && sd::open_files == 0);
    assert(nav::list(names) && names.size() == 1 && names.front() == saved_name);
    host_worker::refuse = true;
    assert(!nav::supported() && !nav::submit(request));
    host_worker::refuse = false;
    assert(!nav::remove("../invalid.tmroute"));
    assert(nav::remove(saved_name.c_str()));
    assert(nav::list(names) && names.empty());
    assert(sd::paths.size() == 2 && host_worker::live == 0 && sd::open_files == 0);
    std::cout << "{\"production_worker\":true,\"length_m\":" << snapshot.distance_m
              << ",\"points\":" << snapshot.points << ",\"external_payload_peak_bytes\":" << peak
              << ",\"map_and_spool_read_bytes\":" << read_bytes << ",\"archive_bytes\":" << archive_bytes
              << ",\"saved_list_reopen_validate_remove\":true,\"cancel_with_denied_mutex\":true,\"live_after_close\":0}\n";
}
