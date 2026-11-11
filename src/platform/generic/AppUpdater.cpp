#include "platform/UpdaterBackend.h"

namespace {
class UnsupportedUpdater final : public UpdaterBackend {
public:
    bool available() const override { return false; }
    void start() override {}
    bool canCheck() const override { return false; }
    void check() override {}
    bool automaticChecksEnabled() const override { return false; }
    void setAutomaticChecksEnabled(bool) override {}
};
}

std::unique_ptr<UpdaterBackend> createUpdaterBackend(AppUpdater &)
{
    return std::make_unique<UnsupportedUpdater>();
}
