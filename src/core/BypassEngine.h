#pragma once
#include "Profile.h"
#include "platform/IPlatformBackend.h"
#include <QObject>
#include <QList>
#include <QSettings>

class BypassEngine : public QObject {
    Q_OBJECT
public:
    explicit BypassEngine(QObject* parent = nullptr);
    ~BypassEngine() override;

    bool start();
    bool stop();
    bool isRunning() const;

    bool m_elevationRequested = false;


    void                  loadConfig();
    void                  saveConfig();
    const BypassConfig&   config()     const { return m_config; }
    void                  setConfig(const BypassConfig& cfg);

    bool autoStart() const;
    void setAutoStart(bool enabled);

    QString lastError() const;

signals:
    void started();
    void stopped();
    void errorOccurred(const QString& error);
    void statsUpdated(quint64 packets, quint64 bytes);
    void configChanged();
    void configLoadFailed(const QString& reason);

private:
    IPlatformBackend* m_backend  = nullptr;
    BypassConfig      m_config;
    QString           m_lastError;

    IPlatformBackend* createBackend();
    void              setupAutoStart(bool enabled);
};