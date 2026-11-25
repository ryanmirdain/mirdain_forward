#pragma once

// Each platform provides this class's implementation, as with NotificationOverlay.
class StartupRegistration {
public:
    enum class Status { Unsupported, Disabled, Enabled, RequiresApproval };

    Status status() const;
    void setEnabled(bool enabled);
    void openSettings() const;
};
