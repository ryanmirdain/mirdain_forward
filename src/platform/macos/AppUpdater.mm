#include "platform/AppUpdater.h"
#include "platform/UpdaterBackend.h"
#include "UpdaterConfig.h"

#import <Sparkle/Sparkle.h>

@interface ForwardUpdaterDelegate : NSObject <SPUUpdaterDelegate>
@property (nonatomic, assign) AppUpdater *owner;
@property (nonatomic, copy) void (^pendingInstall)(void);
@end

@implementation ForwardUpdaterDelegate
- (BOOL)updater:(SPUUpdater *)updater shouldPostponeRelaunchForUpdate:(SUAppcastItem *)item
    untilInvokingBlock:(void (^)(void))installHandler
{
    if (self.owner->beginInstallation()) return NO;
    self.pendingInstall = installHandler;
    return YES;
}
- (void)updater:(SPUUpdater *)updater didAbortWithError:(NSError *)error
{
    self.pendingInstall = nil;
    self.owner->cancelInstallation();
}
@end

namespace {
class SparkleUpdater final : public UpdaterBackend {
public:
    explicit SparkleUpdater(AppUpdater &owner)
    {
        delegate = [ForwardUpdaterDelegate new];
        delegate.owner = &owner;
        controller = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:NO
            updaterDelegate:delegate userDriverDelegate:nil];
    }
    bool available() const override { return ForwardUpdatesConfigured; }
    void start() override { [controller startUpdater]; }
    bool canCheck() const override { return controller.updater.canCheckForUpdates; }
    void check() override { if (canCheck()) [controller checkForUpdates:nil]; }
    bool automaticChecksEnabled() const override { return controller.updater.automaticallyChecksForUpdates; }
    void setAutomaticChecksEnabled(bool enabled) override { controller.updater.automaticallyChecksForUpdates = enabled; }
    void transferFinished() override
    {
        auto install = delegate.pendingInstall;
        delegate.pendingInstall = nil;
        if (install) install();
    }
private:
    ForwardUpdaterDelegate *delegate;
    SPUStandardUpdaterController *controller;
};
}

std::unique_ptr<UpdaterBackend> createUpdaterBackend(AppUpdater &owner)
{
    return std::make_unique<SparkleUpdater>(owner);
}
