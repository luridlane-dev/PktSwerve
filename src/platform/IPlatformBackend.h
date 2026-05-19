#pragma once
#include <QString>
#include <QObject>
#include <QStringList>

enum class BypassMode {
    Disabled,
    BlacklistMode,
    WhitelistMode,
    GlobalMode
};


struct BypassConfig {
    BypassMode mode = BypassMode::GlobalMode;

    bool fragmentHttpRequests  = true;
    bool fragmentHttpsPersist  = true;
    bool nativeFrag            = true;
    bool reverseFrag           = false;

    bool wrongChecksum         = false;
    bool wrongSeq              = false;
    bool fakePackets           = true;

    int httpFragmentSize  = 2;
    int httpsFragmentSize = 2;
    int fakePacketTTL     = 8;

    bool    dnsRedirect = true;
    QString dnsServer   = "9.9.9.10";
    int     dnsPort     = 9953;

    QStringList blacklist;
    QStringList whitelist;
};

class IPlatformBackend : public QObject {
    Q_OBJECT
public:
    explicit IPlatformBackend(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IPlatformBackend() = default;

    virtual bool    start(const BypassConfig& config) = 0;
    virtual bool    stop()                            = 0;
    virtual bool    isRunning()   const               = 0;
    virtual QString lastError()   const               = 0;

    virtual bool hasRequiredPrivileges()    const = 0;
    virtual bool requestPrivilegeElevation()      = 0;

signals:
    void started();
    void stopped();
    void errorOccurred(const QString& error);
    void statsUpdated(quint64 packetsProcessed, quint64 bytesProcessed);
};