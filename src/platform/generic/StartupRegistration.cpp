#include "platform/StartupRegistration.h"

StartupRegistration::Status StartupRegistration::status() const
{
    return Status::Unsupported;
}

void StartupRegistration::setEnabled(bool) {}
void StartupRegistration::openSettings() const {}
