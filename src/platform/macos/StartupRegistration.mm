#include "platform/StartupRegistration.h"

#include <QString>

#import <ServiceManagement/ServiceManagement.h>

#include <stdexcept>

StartupRegistration::Status StartupRegistration::status() const
{
    if (@available(macOS 13.0, *)) {
        switch (SMAppService.mainAppService.status) {
        case SMAppServiceStatusEnabled: return Status::Enabled;
        case SMAppServiceStatusRequiresApproval: return Status::RequiresApproval;
        default: return Status::Disabled;
        }
    }
    return Status::Unsupported;
}

void StartupRegistration::setEnabled(bool enabled)
{
    if (@available(macOS 13.0, *)) {
        NSError *error = nil;
        const BOOL success = enabled
            ? [SMAppService.mainAppService registerAndReturnError:&error]
            : [SMAppService.mainAppService unregisterAndReturnError:&error];
        if (!success)
            throw std::runtime_error(QString::fromNSString(error.localizedDescription).toStdString());
    }
}

void StartupRegistration::openSettings() const
{
    if (@available(macOS 13.0, *)) [SMAppService openSystemSettingsLoginItems];
}
