#pragma once

#include <QElapsedTimer>
#include <QSslSocket>
#include <QTimer>
#include <QUrl>

// Bambu's implicit FTPS needs TLS session reuse and model-specific data shutdown.
class BambuFtpsUpload : public QObject {
    Q_OBJECT

public:
    explicit BambuFtpsUpload(QObject *parent = nullptr);
    void start(const QUrl &url, const QString &accessCode, const QString &directory,
        const QString &filename, const QByteArray &archive);
    void stop();

signals:
    void progress(const QString &message);
    void completed();
    void failed(const QString &message);

private:
    enum class Step { Welcome, User, Password, Pbsz, Prot, Type, Directory, Passive, Store, Complete };
    QSslSocket control;
    QSslSocket data;
    QTimer timeout;
    QTimer progressTimer;
    QElapsedTimer progressClock;
    QUrl url;
    QString accessCode;
    QString directory;
    QString filename;
    QByteArray archive;
    QByteArray multilineCode;
    Step step = Step::Welcome;
    qsizetype offset = 0;
    qint64 lastProgressBytes = 0;
    bool vsFtpd = false;
    bool dataClosed = false;
    bool stopped = true;

    void command(Step next, const QByteArray &value);
    void readReplies();
    void reply(int code, const QByteArray &line);
    void writeData();
    void reportProgress();
    void fail(const QString &message);
};
