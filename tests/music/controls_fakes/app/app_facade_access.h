#pragma once
namespace app
{
struct AppConfig
{
    bool gps_enabled = true;
};
struct AppConfigChangeSet
{
    static int gps() { return 1; }
};
inline AppConfig config;
inline bool edit_available = true;
inline unsigned edits = 0, commits = 0, projections = 0;
struct Edit
{
    explicit operator bool() const { return edit_available; }
    AppConfig& config() { return app::config; }
    void commit(int) { ++commits; }
};
struct Facade
{
    Edit beginConfigEdit()
    {
        ++edits;
        return {};
    }
    void applyPositionConfig() { ++projections; }
};
inline Facade facade;
inline bool hasAppFacade() { return true; }
inline Facade& configFacade() { return facade; }
} // namespace app
