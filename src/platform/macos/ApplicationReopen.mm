#include "platform/ApplicationReopen.h"

#import <AppKit/AppKit.h>

@interface ReopenHandler : NSObject
@end

@implementation ReopenHandler {
    std::function<void()> _callback;
}

- (instancetype)initWithCallback:(std::function<void()>)callback
{
    self = [super init];
    _callback = std::move(callback);
    return self;
}

- (void)handleReopen:(NSAppleEventDescriptor *)event withReply:(NSAppleEventDescriptor *)reply
{
    _callback();
}

@end

void onApplicationReopen(std::function<void()> callback)
{
    // The event manager does not retain its handler.
    static ReopenHandler *handler;
    handler = [[ReopenHandler alloc] initWithCallback:std::move(callback)];
    [NSAppleEventManager.sharedAppleEventManager setEventHandler:handler andSelector:@selector(handleReopen:withReply:)
        forEventClass:kCoreEventClass andEventID:kAEReopenApplication];
}
