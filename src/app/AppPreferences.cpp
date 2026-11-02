#include "app/AppPreferences.h"
#include "app/AppConfig.h"
#include "platform/NotificationSender.h"

#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <exception>

AppPreferences::AppPreferences(AppConfig &config, NotificationSender &notifications, QMenu &menu)
    : config(config), notifications(notifications)
{
    menu.addAction("Preferences…", &menu, [this] { showDialog(false); });
}

bool AppPreferences::notificationsEnabled() const
{
    return config.section("preferences").value("notifications_enabled").toBool();
}

void AppPreferences::promptOnFirstLaunch()
{
    if (!config.section("preferences").value("setup_complete").toBool()) showDialog(true);
}

void AppPreferences::showDialog(bool firstLaunch)
{
    const auto status = startup.status();
    const bool registered = status == StartupRegistration::Status::Enabled
        || status == StartupRegistration::Status::RequiresApproval;
    const bool supported = status != StartupRegistration::Status::Unsupported;
    const bool previouslyNotifying = notificationsEnabled();

    QDialog dialog;
    dialog.setWindowTitle(firstLaunch ? "Welcome to Mirdain Forward" : "Mirdain Forward preferences");
    auto *layout = new QVBoxLayout(&dialog);
    auto *intro = new QLabel("Choose how Mirdain Forward runs. You can change these choices anytime\n"
                            "from Preferences in the menu bar / system tray.", &dialog);
    layout->addWidget(intro);
    auto *launch = new QCheckBox("Launch Mirdain Forward when I sign in", &dialog);
    launch->setChecked(registered);
    launch->setEnabled(supported);
    layout->addWidget(launch);
    auto *startupNote = new QLabel(supported
        ? "Keep the app in its permanent location before enabling startup.\n"
          "Your system's startup settings can also prevent automatic launch."
        : "Automatic startup is supported on macOS 13+ and Windows.", &dialog);
    layout->addWidget(startupNote);
    if (supported) {
        auto *settings = new QPushButton("Open startup settings…", &dialog);
        layout->addWidget(settings);
        QObject::connect(settings, &QPushButton::clicked, &dialog, [this] { startup.openSettings(); });
    }
    if (status == StartupRegistration::Status::RequiresApproval)
        layout->addWidget(new QLabel("Approve Mirdain Forward in Login Items to allow automatic startup.", &dialog));
    auto *notify = new QCheckBox("Notify me when a plate is sent through Mirdain Forward", &dialog);
    notify->setChecked(previouslyNotifying);
    layout->addWidget(notify);
    layout->addWidget(new QLabel("A notification confirms receipt of a plate, before downloading it.\n"
                                "It does not confirm that printing has started. System permission may be required.", &dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    if (firstLaunch) buttons->button(QDialogButtonBox::Cancel)->setText("Skip");
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (!accepted && !firstLaunch) return;

    try {
        const bool launchEnabled = accepted ? launch->isChecked() : registered;
        const bool notifyEnabled = accepted ? notify->isChecked() : previouslyNotifying;
        if (supported && launchEnabled != registered) startup.setEnabled(launchEnabled);
        auto preferences = config.section("preferences");
        preferences.insert("setup_complete", true);
        preferences.insert("launch_at_sign_in", launchEnabled);
        preferences.insert("notifications_enabled", notifyEnabled);
        config.setSection("preferences", preferences);
        if (accepted && notifyEnabled) notifications.requestPermission();
        if (accepted && launchEnabled && startup.status() == StartupRegistration::Status::RequiresApproval) {
            QMessageBox::information(nullptr, "Mirdain Forward",
                "Approve Mirdain Forward in System Settings > General > Login Items to allow automatic startup.");
            startup.openSettings();
        }
    } catch (const std::exception &error) {
        QMessageBox::critical(nullptr, "Mirdain Forward", QString::fromUtf8(error.what()));
    }
}
