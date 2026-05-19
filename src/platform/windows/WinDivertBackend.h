#pragma once
#ifdef PLATFORM_WINDOWS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <windivert.h>

#undef __in
#undef __out
#undef __inout
#undef __in_opt
#undef __out_opt
#undef __inout_opt

#include <atomic>
#include <QThread>
#include <QMap>
#include <QMutex>
#include "platform/IPlatformBackend.h"

struct DnsState {
    quint32 origDstIp   = 0;
    quint16 origDstPort = 0;
};

class WinDivertWorker;

// WinDivertBackend
class WinDivertBackend : public IPlatformBackend {
    Q_OBJECT
public:
    explicit WinDivertBackend(QObject* parent = nullptr);
    ~WinDivertBackend() override;

    bool    start(const BypassConfig& config) override;
    bool    stop()                            override;
    bool    isRunning()   const               override;
    QString lastError()   const               override;
    bool    hasRequiredPrivileges()    const  override;
    bool    requestPrivilegeElevation()       override;

private:
    WinDivertWorker*  m_worker     = nullptr;
    QThread*          m_thread     = nullptr;
    QString           m_lastError;
    std::atomic<bool> m_running    { false };
};

// WinDivertWorker
class WinDivertWorker : public QObject {
    Q_OBJECT
public:
    explicit WinDivertWorker(QObject* parent = nullptr);
    ~WinDivertWorker() override;

    void setConfig(const BypassConfig& cfg) { m_config = cfg; }
    void requestStop()  { m_stopRequested = true; }
    void closeHandle();

public slots:
    void run();

signals:
    void finished();
    void errorOccurred(const QString& error);
    void statsUpdated(quint64 packets, quint64 bytes);

private:
    bool fragmentPacket(
        HANDLE h, uint8_t* packet, WINDIVERT_ADDRESS* addr,
        WINDIVERT_IPHDR* ip4, WINDIVERT_IPV6HDR* ip6, WINDIVERT_TCPHDR* tcp,
        uint8_t* payload, uint32_t payloadLen,
        int fragSize, bool reverse);

    void sendFakePacket(
        HANDLE h, uint8_t* packet, uint32_t packetLen, WINDIVERT_ADDRESS* addr,
        WINDIVERT_IPHDR* ip4, WINDIVERT_IPV6HDR* ip6, WINDIVERT_TCPHDR* tcp);

    void sendWrongChecksumPacket(
        HANDLE h, uint8_t* packet, uint32_t packetLen, WINDIVERT_ADDRESS* addr,
        WINDIVERT_IPHDR* ip4, WINDIVERT_IPV6HDR* ip6, WINDIVERT_TCPHDR* tcp);

    void sendWrongSeqPacket(
        HANDLE h, uint8_t* packet, uint32_t packetLen, WINDIVERT_ADDRESS* addr,
        WINDIVERT_IPHDR* ip4, WINDIVERT_IPV6HDR* ip6, WINDIVERT_TCPHDR* tcp);

    bool redirectDnsOutbound(
        HANDLE h, uint8_t* packet, uint32_t packetLen, WINDIVERT_ADDRESS* addr,
        WINDIVERT_IPHDR* ip4, WINDIVERT_UDPHDR* udp);

    bool rewriteDnsInbound(
        HANDLE h, uint8_t* packet, uint32_t packetLen, WINDIVERT_ADDRESS* addr,
        WINDIVERT_IPHDR* ip4, WINDIVERT_UDPHDR* udp,
        quint32 origSrcIp, quint16 origSrcPort);

    BypassConfig        m_config;
    std::atomic<bool>   m_stopRequested { false };
    std::atomic<HANDLE> m_handle        { nullptr };

    QMap<quint16, DnsState> m_dnsState;
    QMutex                  m_dnsMutex;

    static constexpr uint32_t MAX_PACKET_SIZE = 65535;

    std::vector<uint8_t> m_fragBuf1;
    std::vector<uint8_t> m_fragBuf2;
    std::vector<uint8_t> m_modBuf;
};

#endif // PLATFORM_WINDOWS