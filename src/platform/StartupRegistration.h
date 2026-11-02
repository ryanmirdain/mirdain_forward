#pragma once

// Each platform provides this class's implementation, as with NotificationSender.
class StartupRegistration {
public:
    enum class Status { Unsupported, Disabled, Enabled, RequiresApproval };

    Status status() const;
    void setEnabled(bool enabled);
    void openSettings() const;
};
