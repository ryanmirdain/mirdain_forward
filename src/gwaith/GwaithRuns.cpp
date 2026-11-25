#include "gwaith/GwaithRuns.h"
#include "app/Notifications.h"

#include <QAction>
#include <QMenu>
#include <QPointer>

#include <algorithm>

static QString notificationId(GwaithAutoSlice *run) { return "gwaith:" + run->conversationId(); }

GwaithRuns::GwaithRuns(Account &account, QNetworkAccessManager &network, Notifications &notifications, QMenu &menu, QAction *before,
    std::function<void(const GwaithAutoSlice &)> open, Send send)
    : account(account), network(network), notifications(notifications), open(std::move(open)), send(std::move(send))
{
    showAll = new QAction(&menu);
    showAll->setVisible(false);
    menu.insertAction(before, showAll);
    connect(showAll, &QAction::triggered, this, [this] {
        dismissed.clear();
        for (auto *run : runs) post(run);
    });
}

void GwaithRuns::start(qint64 organization, const QStringList &paths, const GwaithAutoSlice::Printer &printer,
    const QString &notes)
{
    auto *run = new GwaithAutoSlice(account, network, organization, paths, printer, notes, this);
    runs.push_back(run);
    states[run] = {};
    connect(run, &GwaithAutoSlice::changed, this, [this, run] {
        if (run->phase() == GwaithAutoSlice::Phase::Done && states[run].delivery == Delivery::Pending) deliver(run);
        if (!dismissed.count(run)) post(run);
        updateMenu();
    });
    post(run);
    updateMenu();
    run->start();
}

// Gwaith is done and the plate has gone, or can't go without the user.
bool GwaithRuns::settled(GwaithAutoSlice *run) const
{
    const auto delivery = states.at(run).delivery;
    return run->phase() == GwaithAutoSlice::Phase::Failed
        || (run->phase() == GwaithAutoSlice::Phase::Done && delivery != Delivery::Pending && delivery != Delivery::Sending);
}

void GwaithRuns::deliver(GwaithAutoSlice *run)
{
    auto &state = states[run];
    if (!run->hasArchive() || run->target().id.isEmpty()) {
        // Only a session that's still open can be sent from the slicer.
        const auto problem = run->hasArchive() ? QString("No printer was chosen.")
            : run->sessionEnded() ? QString("Gwaith closed the slicing session without saving a plate.")
            : QString("Gwaith didn’t save a sliced plate.");
        state = {Delivery::NeedsUser, run->sessionId().isEmpty() ? problem : problem + " Open it in the slicer to send it."};
        return;
    }
    state = {Delivery::Sending, "Sending to " + run->target().name};
    // The run may be dismissed and gone before the transfer ends.
    QPointer<GwaithAutoSlice> guard(run);
    send(*run, [this, guard](const QString &status) {
        if (!guard) return;
        states[guard].status = status;
        if (!dismissed.count(guard)) post(guard);
    }, [this, guard](const QString &error) {
        if (!guard) return;
        states[guard] = error.isEmpty() ? State{Delivery::Sent, "Print requested on " + guard->target().name}
                                        : State{Delivery::Failed, error};
        if (!dismissed.count(guard)) post(guard);
        updateMenu();
    });
}

// Runs don't time out yet: they stay until dismissed.
void GwaithRuns::post(GwaithAutoSlice *run)
{
    using Reason = Notifications::Reason;
    using Indicator = Notifications::Indicator;
    const auto &state = states[run];
    Reason reason = Reason::GwaithProgress;
    Indicator indicator = Indicator::Spinner;
    QString status = run->status();
    if (run->phase() == GwaithAutoSlice::Phase::Failed) {
        reason = Reason::GwaithFailed;
        indicator = Indicator::Failed;
    } else if (run->phase() == GwaithAutoSlice::Phase::Done) {
        status = state.status;
        switch (state.delivery) {
        case Delivery::Pending:
        case Delivery::Sending: break;
        case Delivery::Sent: reason = Reason::GwaithDone; indicator = Indicator::Done; break;
        case Delivery::NeedsUser: reason = Reason::GwaithNeedsUser; indicator = Indicator::Attention; break;
        case Delivery::Failed: reason = Reason::GwaithFailed; indicator = Indicator::Failed; break;
        }
    }
    // Clickable only while there is a session to open: not before Gwaith creates one, nor after it
    // ends it.
    std::function<void()> activated;
    if (!run->sessionId().isEmpty()) activated = [this, run] { open(*run); };
    notifications.post({notificationId(run), reason, run->title(), status, indicator, 0, -1,
        activated, [this, run] { dismiss(run); }});
}

void GwaithRuns::dismiss(GwaithAutoSlice *run)
{
    if (settled(run)) {
        runs.erase(std::find(runs.begin(), runs.end(), run));
        states.erase(run);
        dismissed.erase(run);
        run->deleteLater();
    } else {
        dismissed.insert(run);
    }
    updateMenu();
}

void GwaithRuns::updateMenu()
{
    const auto working = std::count_if(runs.begin(), runs.end(), [this](GwaithAutoSlice *run) { return !settled(run); });
    showAll->setText(working ? QString("Gwaith Activity (%1 working)…").arg(working) : "Gwaith Activity…");
    showAll->setVisible(!runs.empty());
}
