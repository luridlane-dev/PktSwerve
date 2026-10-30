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
#include <QByteArray>
#include <QHash>
#include <QThread>
#include <QMap>
#include "platform/IPlatformBackend.h"
#include "ProcessResolver.h"


struct DnsState {
    bool    isIPv6       = false;
    quint32 origDstIp4   = 0;          // IPv4
    uint8_t origDstIp6[16] = {};       // IPv6
    quint16 origDstPort  = 0;
    quint64 createdAt    = 0;
};

class WinDivertWorker;

// ---------------------------------------------------------------------------
// WinDivertBackend
// ---------------------------------------------------------------------------
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
    quint64            m_workerGeneration = 0;
};

// ---------------------------------------------------------------------------
// WinDivertWorker
// ---------------------------------------------------------------------------
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
        WINDIVERT_IPHDR* ip4, WINDIVERT_IPV6HDR* ip6, WINDIVERT_UDPHDR* udp);

    bool rewriteDnsInbound(
        HANDLE h, uint8_t* packet, uint32_t packetLen, WINDIVERT_ADDRESS* addr,
        WINDIVERT_IPHDR* ip4, WINDIVERT_IPV6HDR* ip6, WINDIVERT_UDPHDR* udp,
        const DnsState& st);

    // Decoys that expire in transit must die before the server but after the DPI.
    // The server's distance is read from its SYN-ACK and caps the configured TTL.
    void    learnHops(const WINDIVERT_IPHDR* ip4, const WINDIVERT_IPV6HDR* ip6);
    uint8_t fakeTtl(const WINDIVERT_IPHDR* ip4, const WINDIVERT_IPV6HDR* ip6) const;
    const QByteArray& fakePayload(const WINDIVERT_TCPHDR* tcp) const;

    // Voice calls start with one 74-byte "IP discovery" datagram.
    // A few zero-filled decoys ahead of it keep the DPI from classifying the flow.
    void sendVoiceDecoys(
        HANDLE h, uint8_t* packet, WINDIVERT_ADDRESS* addr,
        WINDIVERT_IPHDR* ip4, WINDIVERT_IPV6HDR* ip6);

    // Selected Sites & Apps mode: does this outbound connection belong to a listed app?
    bool fromSelectedApp(const WINDIVERT_IPV6HDR* ip6, const WINDIVERT_TCPHDR* tcp, QString* appName);

    BypassConfig        m_config;
    ProcessResolver     m_procs;
    std::atomic<bool>   m_stopRequested { false };
    std::atomic<HANDLE> m_handle        { nullptr };

    // A resolver may issue several DNS requests from one UDP source port.
    // The key therefore includes the DNS transaction ID and address family;
    // indexing by source port alone can rewrite a reply with another query's
    // original destination.
    QMap<quint64, DnsState> m_dnsState;

    QHash<QByteArray, uint8_t> m_hops;   // server address -> hops seen in its SYN-ACK
    QByteArray m_fakeTlsHello;
    QByteArray m_fakeHttp;

    static constexpr uint32_t MAX_PACKET_SIZE = 65535;

    std::vector<uint8_t> m_fragBuf1;
    std::vector<uint8_t> m_fragBuf2;
};

#endif // PLATFORM_WINDOWS
