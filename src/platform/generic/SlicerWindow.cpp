#include "platform/SlicerWindow.h"
#include "platform/SupportedBrowser.h"

struct SlicerWindow::Impl {
    QUrl url;
};

SlicerWindow::SlicerWindow(const QUrl &url, Handler) : url(url), impl(new Impl{url}) {}
SlicerWindow::~SlicerWindow() = default;
void SlicerWindow::show() { openInSupportedBrowser(impl->url); }
void SlicerWindow::load(const QUrl &) {}
