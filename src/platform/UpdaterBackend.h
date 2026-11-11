#pragma once

#include <memory>

class AppUpdater;

class UpdaterBackend {
public:
    virtual ~UpdaterBackend() = default;
    virtual bool available() const = 0;
    virtual void start() = 0;
    virtual bool canCheck() const = 0;
    virtual void check() = 0;
    virtual bool automaticChecksEnabled() const = 0;
    virtual void setAutomaticChecksEnabled(bool enabled) = 0;
    virtual void transferFinished() {}
};

std::unique_ptr<UpdaterBackend> createUpdaterBackend(AppUpdater &updater);
