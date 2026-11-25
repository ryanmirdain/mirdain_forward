#pragma once

#include "gwaith/GwaithAutoSlice.h"

#include <QObject>

#include <functional>
#include <map>
#include <set>
#include <vector>

class Notifications;
class QAction;
class QMenu;

// The Gwaith auto-slice runs, any number at once, each a notification that follows its progress
// (on macOS, a row in the card under the menu bar icon). When Gwaith finishes, a run sends the
// last sliced plate (.mirdain) Gwaith attached to the printer its models were dropped on. With
// no plate or no printer, it asks the user to open the run in the slicer instead.
// Dismissing a settled run removes it; dismissing one still working only hides it. Gwaith
// Activity… in the menu, present while there are runs, shows them all again.
class GwaithRuns : public QObject {
public:
    // Sends a run's plate to its printer, reporting the transfer's progress, then done with an
    // error, or with an empty one once the printer accepted the print.
    using Send = std::function<void(GwaithAutoSlice &run, std::function<void(const QString &status)> progress,
        std::function<void(const QString &error)> done)>;
    // open shows a run in the slicer.
    GwaithRuns(Account &account, QNetworkAccessManager &network, Notifications &notifications, QMenu &menu, QAction *before,
        std::function<void(const GwaithAutoSlice &)> open, Send send);
    // notes are the user's instructions for Gwaith, if any.
    void start(qint64 organization, const QStringList &paths, const GwaithAutoSlice::Printer &printer, const QString &notes);

private:
    // What happens to the plate after Gwaith finishes.
    enum class Delivery { Pending, Sending, Sent, NeedsUser, Failed };
    struct State { Delivery delivery = Delivery::Pending; QString status; };

    Account &account;
    QNetworkAccessManager &network;
    Notifications &notifications;
    std::function<void(const GwaithAutoSlice &)> open;
    Send send;
    std::vector<GwaithAutoSlice *> runs;
    std::map<GwaithAutoSlice *, State> states;
    std::set<GwaithAutoSlice *> dismissed;
    QAction *showAll;

    bool settled(GwaithAutoSlice *run) const;
    void deliver(GwaithAutoSlice *run);
    void post(GwaithAutoSlice *run);
    void dismiss(GwaithAutoSlice *run);
    void updateMenu();
};
