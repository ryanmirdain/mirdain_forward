#include "platform/NotificationSender.h"

#include <QApplication>
#include <QDebug>
#include <QMessageBox>

#import <UserNotifications/UserNotifications.h>

@interface ForwardNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

@implementation ForwardNotificationDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
      willPresentNotification:(UNNotification *)notification
        withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler
{
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList);
}
@end

struct NotificationSender::Platform {
    UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
    // The notification center keeps only a weak reference to its delegate.
    ForwardNotificationDelegate *delegate = [ForwardNotificationDelegate new];
};

NotificationSender::NotificationSender(QSystemTrayIcon &)
    : platform(std::make_unique<Platform>())
{
    platform->center.delegate = platform->delegate;
}

NotificationSender::~NotificationSender()
{
    platform->center.delegate = nil;
}

void NotificationSender::requestPermission()
{
    auto *center = platform->center;
    [center getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings *settings) {
        if (settings.authorizationStatus == UNAuthorizationStatusDenied) {
            QMetaObject::invokeMethod(qApp, [] {
                QMessageBox::information(nullptr, "Mirdain Forward",
                    "Enable notifications for Mirdain Forward in System Settings > Notifications.");
            }, Qt::QueuedConnection);
            return;
        }
        [center requestAuthorizationWithOptions:UNAuthorizationOptionAlert
            completionHandler:^(BOOL granted, NSError *error) {
                if (error) qWarning() << "Notification permission:" << QString::fromNSString(error.localizedDescription);
            }];
    }];
}

void NotificationSender::send(const QString &title, const QString &message)
{
    auto *center = platform->center;
    auto *content = [UNMutableNotificationContent new];
    content.title = title.toNSString();
    content.body = message.toNSString();
    auto *request = [UNNotificationRequest requestWithIdentifier:NSUUID.UUID.UUIDString
        content:content trigger:nil];
    [center addNotificationRequest:request withCompletionHandler:^(NSError *error) {
        if (error) qWarning() << "Notification delivery:" << QString::fromNSString(error.localizedDescription);
    }];
}
