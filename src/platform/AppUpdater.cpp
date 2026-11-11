#include "platform/AppUpdater.h"
#include "platform/UpdaterBackend.h"

#include <QAction>
#include <QMenu>

AppUpdater::AppUpdater(QMenu &menu) : backend(createUpdaterBackend(*this))
{
    auto *action = menu.addAction("Check for Updates…", this, [this] { backend->check(); });
    action->setMenuRole(QAction::NoRole);
    action->setEnabled(false);
    connect(&menu, &QMenu::aboutToShow, this, [this, action] {
        action->setEnabled(available() && backend->canCheck());
    });
    if (!available()) action->setToolTip("Updates are not configured for this build.");
}

AppUpdater::~AppUpdater() = default;
void AppUpdater::start() { if (available()) backend->start(); }
bool AppUpdater::available() const { return backend->available(); }
bool AppUpdater::automaticChecksEnabled() const { return available() && backend->automaticChecksEnabled(); }
void AppUpdater::setAutomaticChecksEnabled(bool enabled)
{
    if (available() && enabled != automaticChecksEnabled()) backend->setAutomaticChecksEnabled(enabled);
}

bool AppUpdater::beginTransfer()
{
    int idle = 0;
    return state.compare_exchange_strong(idle, Transferring);
}

void AppUpdater::endTransfer()
{
    state.fetch_and(~Transferring);
    backend->transferFinished();
}

bool AppUpdater::beginInstallation() { return !(state.fetch_or(Installing) & Transferring); }
void AppUpdater::cancelInstallation() { state.fetch_and(~Installing); }
