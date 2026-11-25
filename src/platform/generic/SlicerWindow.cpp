#include "platform/SlicerWindow.h"
#include "platform/SupportedBrowser.h"

struct SlicerWindow::Impl {
    QUrl url;
};

SlicerWindow::SlicerWindow(const QUrl &url, Handler) : url(url), impl(new Impl{url}) {}
SlicerWindow::~SlicerWindow() = default;
void SlicerWindow::show() { openInSupportedBrowser(impl->url); }
void SlicerWindow::load(const QUrl &) {}
// The next show() opens the new page; the browser's open tabs stay as they are.
void SlicerWindow::setSlicerUrl(const QUrl &slicer) { url = slicer; impl->url = slicer; }
bool SlicerWindow::evaluate(const QString &) { return false; }
// The slicer in a browser keeps its own session.
void SlicerWindow::accountChanged() {}
// The browser keeps whatever Forward opened; show() opens the slicer in a new tab.
bool SlicerWindow::showingSlicer() const { return true; }
void SlicerWindow::showPage(const QUrl &page) { openInSupportedBrowser(page); }
// The slicer opens in a browser, which Forward cannot hand files to.
void SlicerWindow::openFiles(const QStringList &paths) { if (!paths.isEmpty()) show(); }
void SlicerWindow::sendToGwaith(const QStringList &paths, const QJsonObject &) { openFiles(paths); }
