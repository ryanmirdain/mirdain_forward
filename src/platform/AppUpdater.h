#pragma once

#include <QObject>
#include <atomic>
#include <memory>

class QMenu;
class UpdaterBackend;

class AppUpdater : public QObject {
public:
    explicit AppUpdater(QMenu &menu);
    ~AppUpdater();
    void start();
    bool available() const;
    bool automaticChecksEnabled() const;
    void setAutomaticChecksEnabled(bool enabled);
    bool beginTransfer();
    void endTransfer();

    // Native updater callbacks reserve shutdown before another request can start.
    bool beginInstallation();
    void cancelInstallation();

private:
    enum { Transferring = 1, Installing = 2 };
    std::atomic<int> state{0};
    std::unique_ptr<UpdaterBackend> backend;
};
