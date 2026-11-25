#pragma once

#include <QUrl>

// Forward's HTTP server, which the slicer reaches and which serves Forward's own pages.
constexpr quint16 ForwardPort = 18744;

// A page Forward serves, such as onboarding at http://127.0.0.1:18744/.
inline QUrl forwardPage(const QString &fragment = {})
{
    QUrl url(QString("http://127.0.0.1:%1/").arg(ForwardPort));
    if (!fragment.isEmpty()) url.setFragment(fragment);
    return url;
}

inline bool isForwardPage(const QUrl &url)
{
    return url.scheme() == "http" && (url.host() == "127.0.0.1" || url.host() == "localhost") && url.port() == ForwardPort;
}
