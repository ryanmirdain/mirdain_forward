#include "app/AppPreferences.h"
#include "app/AppConfig.h"
#include "platform/NotificationSender.h"

#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include <exception>
#include <utility>

namespace {
class PreferenceSwitch : public QCheckBox {
public:
    explicit PreferenceSwitch(QWidget *parent) : QCheckBox(parent)
    {
        setFixedSize(40, 24);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
    }

protected:
    bool hitButton(const QPoint &point) const override { return rect().contains(point); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (!isEnabled()) painter.setOpacity(0.4);
        if (hasFocus()) {
            painter.setPen(QPen(QColor("#e7eef4"), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(0.5, 0.5, 39, 23), 11.5, 11.5);
        }
        painter.setPen(QPen(QColor("#3b4e57"), 1));
        painter.setBrush(QColor(isChecked() ? "#e7eef4" : "#172429"));
        painter.drawRoundedRect(QRectF(2.5, 2.5, 35, 19), 9.5, 9.5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(isChecked() ? "#010305" : "#e7eef4"));
        painter.drawEllipse(QRectF(isChecked() ? 21 : 5, 5, 14, 14));
    }
};
}

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

    // Match mirdain_client's modal typography, surfaces, controls, and spacing.
    static const auto fontFamily = [] {
        const auto regular = QFontDatabase::addApplicationFont(":/assets/fonts/Aileron-Regular.otf");
        QFontDatabase::addApplicationFont(":/assets/fonts/Aileron-SemiBold.otf");
        return QFontDatabase::applicationFontFamilies(regular).first();
    }();
    QDialog dialog;
    dialog.setWindowTitle("Mirdain Forward");
    dialog.setFixedWidth(480);
    QFont font(fontFamily);
    font.setPixelSize(14);
    dialog.setFont(font);
    auto palette = dialog.palette();
    palette.setColor(QPalette::Window, QColor("#0a0e11"));
    palette.setColor(QPalette::WindowText, QColor("#e7eef4"));
    dialog.setPalette(palette);
    dialog.setStyleSheet(R"(
        QDialog { background: #0a0e11; }
        QLabel { color: #e7eef4; }
        QLabel[muted="true"] { color: #9dacbc; }
        QFrame#divider { background: #29383f; border: none; }
        QPushButton { min-height: 38px; padding: 0 16px; border-radius: 3px;
            border: 1px solid #3b4e57; background: transparent;
            color: #e7eef4; font-weight: 600; }
        QPushButton:hover { border-color: #687985; background: #111518; }
        QPushButton:focus { border-color: #e967c8; }
        QPushButton:default { background: #e967c8; border-color: #e967c8; color: #010305; }
        QPushButton:default:hover { background: #f18bd8; border-color: #f18bd8; }
        QPushButton:default:focus { border-color: #e7eef4; }
        QPushButton#startupSettings { min-height: 20px; padding: 0; border: none;
            color: #e7eef4; text-align: left; font-size: 12px; }
        QPushButton#startupSettings:hover { background: transparent; text-decoration: underline; }
        QPushButton#startupSettings:focus { color: #e967c8; text-decoration: underline; }
    )");
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(0);

    auto *title = new QLabel(firstLaunch ? "Set up Mirdain Forward" : "Preferences", &dialog);
    auto titleFont = font;
    titleFont.setPixelSize(20);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    layout->addWidget(title);
    layout->addSpacing(24);

    const auto addDivider = [&] {
        auto *divider = new QFrame(&dialog);
        divider->setObjectName("divider");
        divider->setFixedHeight(1);
        layout->addWidget(divider);
    };
    const auto addDetail = [&](QVBoxLayout *row, const QString &text) {
        auto *detail = new QLabel(text, &dialog);
        detail->setProperty("muted", true);
        detail->setWordWrap(true);
        auto detailFont = font;
        detailFont.setPixelSize(12);
        detail->setFont(detailFont);
        row->addWidget(detail);
    };
    const auto addOption = [&](const QString &name, const QString &description) {
        auto *row = new QVBoxLayout;
        row->setSpacing(6);
        auto *top = new QHBoxLayout;
        top->setSpacing(16);
        auto *label = new QLabel(name, &dialog);
        auto labelFont = font;
        labelFont.setWeight(QFont::DemiBold);
        label->setFont(labelFont);
        auto *toggle = new PreferenceSwitch(&dialog);
        toggle->setAccessibleName(name);
        toggle->setAccessibleDescription(description);
        label->setBuddy(toggle);
        top->addWidget(label, 1);
        top->addWidget(toggle);
        row->addLayout(top);
        addDetail(row, description);
        layout->addLayout(row);
        return std::make_pair(toggle, row);
    };
    const auto [launch, launchLayout] = addOption("Launch at sign-in", supported
        ? "Start Forward automatically when you sign in."
        : "Automatic startup requires macOS 13+ or Windows.");
    launch->setChecked(registered);
    launch->setEnabled(supported);
    if (supported) {
        auto *settings = new QPushButton("Open startup settings", &dialog);
        settings->setObjectName("startupSettings");
        settings->setAutoDefault(false);
        settings->setCursor(Qt::PointingHandCursor);
        launchLayout->addSpacing(2);
        launchLayout->addWidget(settings, 0, Qt::AlignLeft);
        QObject::connect(settings, &QPushButton::clicked, &dialog, [this] { startup.openSettings(); });
    }
    if (status == StartupRegistration::Status::RequiresApproval) {
        auto *approval = new QLabel("Approve Forward in Login Items to allow automatic startup.", &dialog);
        approval->setWordWrap(true);
        approval->setStyleSheet("color: #d46a4a; font-size: 12px;");
        launchLayout->addWidget(approval);
    }
    layout->addSpacing(20);
    addDivider();
    layout->addSpacing(20);
    const auto [notify, notifyLayout] = addOption("Plate notifications",
        "Notify me when a plate is received.");
    notify->setChecked(previouslyNotifying);
    layout->addSpacing(24);
    addDivider();
    layout->addSpacing(16);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    buttons->setStyleSheet("QDialogButtonBox { button-layout: 1; }");
    buttons->button(QDialogButtonBox::Save)->setText("Save preferences");
    buttons->button(QDialogButtonBox::Save)->setDefault(true);
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
