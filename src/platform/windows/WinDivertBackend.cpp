#ifdef PLATFORM_WINDOWS
#include "WinDivertBackend.h"
#include "core/PacketUtils.h"
#include "app/Logger.h"
#include <shellapi.h>
#include <QHostAddress>
#include <vector>
#include <cstdlib>
#include <QRandomGenerator>


WinDivertBackend::WinDivertBackend(QObject* parent) : IPlatformBackend(parent)
{
    LOG_INFO0("WinDivertBackend created");

}

WinDivertBackend::~WinDivertBackend()
{
    LOG_INFO0("WinDivertBackend destroying");
    WinDivertBackend::stop();
}

bool WinDivertBackend::hasRequiredPrivileges() const
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        LOG_ERROR("OpenProcessToken failed, error=%1", GetLastError());
        return false;
    }
    TOKEN_ELEVATION elev{};
    DWORD sz = sizeof(elev);
    bool elevated = false;
    if (GetTokenInformation(token, TokenElevation, &elev, sz, &sz))
        elevated = (elev.TokenIsElevated != 0);
    CloseHandle(token);
    LOG_INFO("Privilege check: %1", elevated ? "elevated" : "NOT elevated");
    return elevated;
}

bool WinDivertBackend::requestPrivilegeElevation()
{
    LOG_INFO0("Requesting privilege elevation via ShellExecute runas");
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.nShow  = SW_SHOWNORMAL;
    bool ok = ShellExecuteExW(&sei) != FALSE;
    if (!ok)
        LOG_ERROR("ShellExecuteExW failed, error=%1", GetLastError());
    else
        LOG_INFO0("Elevation process launched successfully");
    return ok;
}

bool WinDivertBackend::start(const BypassConfig& config)
{
    if (m_running) {
        LOG_WARN0("start() called but already running");
        return true;
    }

    LOG_INFO0("WinDivertBackend starting worker thread");

    m_worker = new WinDivertWorker();
    m_worker->setConfig(config);

    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started,
            m_worker, &WinDivertWorker::run);
    connect(m_worker, &WinDivertWorker::finished,
            m_thread, &QThread::quit);
    connect(m_worker, &WinDivertWorker::finished,
            m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished,
            m_thread, &QObject::deleteLater);
    connect(m_worker, &WinDivertWorker::errorOccurred,
            this,     &WinDivertBackend::errorOccurred);
    connect(m_worker, &WinDivertWorker::statsUpdated,
            this,     &WinDivertBackend::statsUpdated);
    connect(m_worker, &WinDivertWorker::finished, this, [this]() {
        LOG_INFO0("WinDivertWorker finished - backend marked stopped");
        m_worker  = nullptr;
        m_thread  = nullptr;
        m_running = false;
        emit stopped();
    });

    m_running = true;
    m_thread->start();
    LOG_INFO0("Worker thread started");
    emit started();
    return true;
}

// ---------------------------------------------------------------------------
bool WinDivertBackend::stop()
{
    if (!m_running) {
        LOG_WARN0("stop() called but not running");
        return true;
    }

    LOG_INFO0("WinDivertBackend stopping - requesting worker to stop");
    if (m_worker) {
        m_worker->requestStop();
        m_worker->closeHandle();
    }

    if (m_thread && m_thread->isRunning()) {
        m_thread->quit();
        LOG_INFO0("Waiting for worker thread to finish (timeout 5s)");

        if (!m_thread->wait(5000)) {
            LOG_WARN0("Worker thread timeout - abandoning thread to avoid deadlock");
        } else {
            LOG_INFO0("Worker thread finished cleanly");
        }
    }

    m_running = false;
    return true;
}

bool    WinDivertBackend::isRunning() const { return m_running; }
QString WinDivertBackend::lastError() const { return m_lastError; }


WinDivertWorker::WinDivertWorker(QObject* parent) : QObject(parent) {
    m_fragBuf1.resize(MAX_PACKET_SIZE);
    m_fragBuf2.resize(MAX_PACKET_SIZE);
    m_modBuf.resize(MAX_PACKET_SIZE);
}

WinDivertWorker::~WinDivertWorker()
{
    closeHandle();
}

void WinDivertWorker::closeHandle()
{
    HANDLE h = m_handle.exchange(nullptr);
    if (h && h != INVALID_HANDLE_VALUE) {
        LOG_DEBUG0("WinDivertClose called safely");
        WinDivertClose(h);
    }
}

void WinDivertWorker::run()
{
    QString filterStr =
        "(outbound and tcp and tcp.PayloadLength > 0 and tcp.PayloadLength < 16384"
        " and (tcp.DstPort == 80"
        "  or (tcp.DstPort == 443"
        "   and tcp.Payload[0] == 0x16"
        "   and tcp.Payload[1] == 0x03"
        "   and tcp.Payload[2] <= 0x03)))";

    filterStr += " or (outbound and udp and udp.DstPort == 443)";

    if (m_config.dnsRedirect) {
        filterStr += " or (outbound and udp and udp.DstPort == 53)";
        filterStr += QString(" or (inbound and udp and udp.SrcPort == %1)")
                         .arg(m_config.dnsPort);
    }

    LOG_INFO("WinDivertWorker starting | filter: %1", filterStr);

    m_handle = WinDivertOpen(filterStr.toStdString().c_str(),
                             WINDIVERT_LAYER_NETWORK, 0, 0);

    if (m_handle == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        QString msg = QString("WinDivert open failed, error code: %1").arg(err);
        LOG_ERROR("WinDivertOpen failed | error=%1 | filter=%2", err, filterStr);
        emit errorOccurred(msg);
        emit finished();
        return;
    }

    WinDivertSetParam(m_handle, WINDIVERT_PARAM_QUEUE_LENGTH, 8192);
    WinDivertSetParam(m_handle, WINDIVERT_PARAM_QUEUE_TIME,   2000);
    WinDivertSetParam(m_handle, WINDIVERT_PARAM_QUEUE_SIZE,   4194304);
    LOG_INFO0("WinDivert handle opened, queue params set — entering packet loop");

    std::vector<uint8_t> buf(MAX_PACKET_SIZE);
    WINDIVERT_ADDRESS addr{};
    uint32_t packetLen   = 0;
    quint64  pktCount    = 0;
    quint64  byteCount   = 0;
    quint64  bypassCount = 0;
    quint64  dnsCount    = 0;

    QMap<uint32_t, TlsFlowBuffer> tlsFlows;

    while (!m_stopRequested) {

        if (!WinDivertRecv(m_handle, buf.data(), MAX_PACKET_SIZE, &packetLen, &addr)) {
            if (m_stopRequested) break;
            DWORD err = GetLastError();
            if (err == ERROR_INSUFFICIENT_BUFFER) {
                LOG_WARN0("WinDivertRecv: buffer too small, skipping packet");
                continue;
            }
            if (err == ERROR_OPERATION_ABORTED) {
                LOG_INFO0("WinDivertRecv: operation aborted - exiting loop");
                break;
            }
            if (err == ERROR_NO_DATA) {
                LOG_INFO0("WinDivertRecv: no data - exiting loop");
                break;
            }
            LOG_WARN("WinDivertRecv error=%1 - continuing", err);
            continue;
        }

        uint8_t* packet  = buf.data();
        WINDIVERT_IPHDR* ip4     = nullptr;
        WINDIVERT_IPV6HDR* ip6     = nullptr;
        WINDIVERT_TCPHDR* tcp     = nullptr;
        WINDIVERT_UDPHDR* udp     = nullptr;
        uint8_t* payload = nullptr;
        uint32_t           pLen    = 0;

        WinDivertHelperParsePacket(
            packet, packetLen,
            &ip4, &ip6, nullptr, nullptr, nullptr,
            &tcp, &udp,
            reinterpret_cast<void**>(&payload), &pLen,
            nullptr, nullptr);

        bool modified = false;

        if (udp && ntohs(udp->DstPort) == 443) {
            modified = true;
            LOG_DEBUG0("QUIC (UDP/443) dropped - browser will fall back to TCP");

        } else if (tcp && payload && pLen > 0) {
            uint16_t dstPort = ntohs(tcp->DstPort);

            bool isTLSHello = (dstPort == 443 && pLen >= 3 &&
                               payload[0] == 0x16 &&
                               payload[1] == 0x03 &&
                               payload[2] <= 0x03);

            bool isHttpRequest = (dstPort == 80 && pLen >= 4 &&
                                  (memcmp(payload, "GET ",  4) == 0 ||
                                   memcmp(payload, "POST",  4) == 0 ||
                                   memcmp(payload, "HEAD",  4) == 0 ||
                                   memcmp(payload, "CONN",  4) == 0));

            if (isTLSHello && m_config.fragmentHttpsPersist) {
                uint32_t flowId = (static_cast<uint32_t>(ntohs(tcp->SrcPort)) << 16)
                | static_cast<uint32_t>(ntohs(tcp->DstPort));

                QString sni;
                QByteArray assembled;

                QString quickSni = extractSNI(payload, pLen);
                if (!quickSni.isEmpty()) {
                    sni            = quickSni;
                    assembled      = QByteArray(reinterpret_cast<const char*>(payload), static_cast<int>(pLen));
                    tlsFlows.remove(flowId);
                    LOG_INFO("TLS ClientHello complete (single pkt) | SNI=%1 | pLen=%2", sni, pLen);
                } else {
                    // Buffer fragmented handshake packets
                    TlsFlowBuffer& flow = tlsFlows[flowId];
                    flow.lastUpdate = GetTickCount64();

                    // Store the raw packet for precise replay
                    flow.rawPackets.append(QByteArray(reinterpret_cast<const char*>(packet), packetLen));

                    bool readyToProcess = accumulateTlsFragment(flow, payload, pLen, assembled);

                    if (readyToProcess) {
                        sni = extractSNI(reinterpret_cast<const uint8_t*>(assembled.constData()),
                                         static_cast<uint32_t>(assembled.size()));

                        bool bypass = sni.isEmpty() ? true : shouldBypass(sni, m_config);
                        LOG_INFO("TLS ClientHello reassembled | SNI=%1 | totalLen=%2",
                                 sni.isEmpty() ? "(none)" : sni, assembled.size());

                        // Process buffered packets sequentially
                        for (int i = 0; i < flow.rawPackets.size(); ++i) {
                            QByteArray& rawPkt = flow.rawPackets[i];
                            uint8_t* rpData = reinterpret_cast<uint8_t*>(rawPkt.data());
                            uint32_t rpLen  = static_cast<uint32_t>(rawPkt.size());

                            WINDIVERT_IPHDR* rIp4 = nullptr;
                            WINDIVERT_IPV6HDR* rIp6 = nullptr;
                            WINDIVERT_TCPHDR* rTcp = nullptr;
                            uint8_t* rPayload = nullptr;
                            uint32_t rPLen = 0;

                            WinDivertHelperParsePacket(rpData, rpLen, &rIp4, &rIp6, nullptr, nullptr, nullptr,
                                                       &rTcp, nullptr, reinterpret_cast<void**>(&rPayload), &rPLen, nullptr, nullptr);

                            if (bypass && i == 0) {
                                // Apply bypass techniques ONLY to the first segment
                                ++bypassCount;
                                if (m_config.fakePackets) sendFakePacket(m_handle, rpData, rpLen, &addr, rIp4, rIp6, rTcp);
                                if (m_config.wrongChecksum) sendWrongChecksumPacket(m_handle, rpData, rpLen, &addr, rIp4, rIp6, rTcp);
                                if (m_config.wrongSeq) sendWrongSeqPacket(m_handle, rpData, rpLen, &addr, rIp4, rIp6, rTcp);

                                fragmentPacket(m_handle, rpData, &addr, rIp4, rIp6, rTcp,
                                               rPayload, rPLen,
                                               m_config.httpsFragmentSize, m_config.reverseFrag);
                            } else {
                                // Forward subsequent fragments natively
                                WinDivertHelperCalcChecksums(rpData, rpLen, &addr, 0);
                                WinDivertSend(m_handle, rpData, rpLen, nullptr, &addr);
                            }
                        }
                        tlsFlows.remove(flowId);
                        goto next_packet;
                    } else {
                        // Do not send the partial packet.
                        // Let it buffer. Sending it now would expose the unfragmented stream.
                        goto next_packet;
                    }
                }

                if (pktCount % 1000 == 0 && !tlsFlows.isEmpty()) {
                    quint64 now = GetTickCount64();
                    for (auto it = tlsFlows.begin(); it != tlsFlows.end(); ) {
                        if (now - it.value().lastUpdate > 5000) {
                            // Flush stuck packets to prevent application hangs
                            for (QByteArray& rawPkt : it.value().rawPackets) {
                                uint8_t* rpData = reinterpret_cast<uint8_t*>(rawPkt.data());
                                WinDivertHelperCalcChecksums(rpData, static_cast<uint32_t>(rawPkt.size()), &addr, 0);
                                WinDivertSend(m_handle, rpData, static_cast<uint32_t>(rawPkt.size()), nullptr, &addr);
                            }
                            it = tlsFlows.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }

                {
                    bool bypass = sni.isEmpty() ? true : shouldBypass(sni, m_config);
                    if (bypass) {
                        ++bypassCount;

                        const uint8_t* workPayload = reinterpret_cast<const uint8_t*>(assembled.constData());
                        uint32_t       workPLen    = static_cast<uint32_t>(assembled.size());

                        uint32_t ipHdrLen  = ip4 ? (ip4->HdrLength * 4u) : 40u;
                        uint32_t tcpHdrLen = tcp->HdrLength * 4u;
                        uint32_t hdrLen    = ipHdrLen + tcpHdrLen;
                        uint32_t newPktLen = hdrLen + workPLen;

                        if (newPktLen <= MAX_PACKET_SIZE) {
                            memcpy(m_modBuf.data(), packet, hdrLen);
                            memcpy(m_modBuf.data() + hdrLen, workPayload, workPLen);

                            if (ip4)
                                reinterpret_cast<WINDIVERT_IPHDR*>(m_modBuf.data())->Length = htons(static_cast<uint16_t>(newPktLen));
                            else if (ip6)
                                reinterpret_cast<WINDIVERT_IPV6HDR*>(m_modBuf.data())->Length = htons(static_cast<uint16_t>(newPktLen - 40));

                            uint8_t* np   = m_modBuf.data();
                            WINDIVERT_IPHDR* nIp4 = ip4 ? reinterpret_cast<WINDIVERT_IPHDR*>(np) : nullptr;
                            WINDIVERT_IPV6HDR*nIp6 = ip6 ? reinterpret_cast<WINDIVERT_IPV6HDR*>(np) : nullptr;
                            WINDIVERT_TCPHDR* nTcp = reinterpret_cast<WINDIVERT_TCPHDR*>(np + ipHdrLen);
                            uint8_t* nPld = np + hdrLen;

                            if (m_config.fakePackets) {
                                sendFakePacket(m_handle, np, newPktLen, &addr, nIp4, nIp6, nTcp);
                            }
                            if (m_config.wrongChecksum) {
                                sendWrongChecksumPacket(m_handle, np, newPktLen, &addr, nIp4, nIp6, nTcp);
                            }
                            if (m_config.wrongSeq) {
                                sendWrongSeqPacket(m_handle, np, newPktLen, &addr, nIp4, nIp6, nTcp);
                            }

                            modified = fragmentPacket(m_handle, np, &addr, nIp4, nIp6, nTcp,
                                                      nPld, workPLen,
                                                      m_config.httpsFragmentSize, m_config.reverseFrag);
                        }
                    }
                }

            } else if (isHttpRequest && m_config.fragmentHttpRequests) {
                QString host   = extractHttpHost(payload, pLen);
                bool    bypass = shouldBypass(host, m_config);

                LOG_INFO("HTTP request | Host=%1 | bypass=%2 | pLen=%3",
                         host.isEmpty() ? "(none)" : host, bypass ? "yes" : "no", pLen);

                if (bypass) {
                    ++bypassCount;

                    if (m_config.fakePackets)
                        sendFakePacket(m_handle, packet, packetLen, &addr, ip4, ip6, tcp);
                    if (m_config.wrongChecksum)
                        sendWrongChecksumPacket(m_handle, packet, packetLen, &addr, ip4, ip6, tcp);
                    if (m_config.wrongSeq)
                        sendWrongSeqPacket(m_handle, packet, packetLen, &addr, ip4, ip6, tcp);

                    modified = fragmentPacket(m_handle, packet, &addr, ip4, ip6, tcp,
                                              payload, pLen,
                                              m_config.httpFragmentSize, m_config.reverseFrag);
                }
            }

        } else if (udp && m_config.dnsRedirect) {

            if (addr.Outbound) {
                QString queryDomain = extractDnsQuery(payload, pLen);
                bool    bypass      = shouldBypass(queryDomain, m_config);

                if (bypass && ip4) {
                    quint16 clientPort = ntohs(udp->SrcPort);
                    quint32 origDstIp  = ntohl(ip4->DstAddr);

                    DnsState st;
                    st.origDstIp   = origDstIp;
                    st.origDstPort = ntohs(udp->DstPort);

                    {
                        QMutexLocker lock(&m_dnsMutex);
                        m_dnsState[clientPort] = st;
                    }

                    LOG_DEBUG("DNS outbound | domain=%1 | redirecting to %2:%3",
                              queryDomain, m_config.dnsServer, m_config.dnsPort);
                    modified = redirectDnsOutbound(m_handle, packet, packetLen, &addr, ip4, udp);
                    if (modified) ++dnsCount;
                }

            } else {
                quint16 clientPort = ntohs(udp->DstPort);
                DnsState st;
                bool found = false;
                {
                    QMutexLocker lock(&m_dnsMutex);
                    if (m_dnsState.contains(clientPort)) {
                        st    = m_dnsState.take(clientPort);
                        found = true;
                    }
                }
                if (found) {
                    modified = rewriteDnsInbound(m_handle, packet, packetLen,
                                                 &addr, ip4, udp,
                                                 st.origDstIp, st.origDstPort);
                }
            }
        }

        if (!modified) {
            WinDivertHelperCalcChecksums(packet, packetLen, &addr, 0);
            WinDivertSend(m_handle, packet, packetLen, nullptr, &addr);
        }

    next_packet:
        ++pktCount;
        byteCount += packetLen;

        if (pktCount % 500 == 0) {
            LOG_INFO("Stats | packets=%1 | bytes=%2 | bypassed=%3 | dns=%4",
                     pktCount, byteCount, bypassCount, dnsCount);
        }
        if (pktCount % 50 == 0)
            emit statsUpdated(pktCount, byteCount);

        if (pktCount % 10000 == 0) {
            QMutexLocker lock(&m_dnsMutex);
            m_dnsState.clear();
            LOG_DEBUG0("DNS state map cleared");
        }
    }

    LOG_INFO("Packet loop exited | totalPackets=%1 | totalBytes=%2 | bypassed=%3 | dns=%4",
             pktCount, byteCount, bypassCount, dnsCount);

    closeHandle();
    emit finished();
}


bool WinDivertWorker::fragmentPacket(
    HANDLE h, uint8_t* packet, WINDIVERT_ADDRESS* addr,
    WINDIVERT_IPHDR* ip4, WINDIVERT_IPV6HDR* ip6, WINDIVERT_TCPHDR* tcp,
    uint8_t* payload, uint32_t payloadLen,
    int fragSize, bool reverse)
{
    if (fragSize <= 0 || static_cast<uint32_t>(fragSize) >= payloadLen)
        fragSize = 1;

    uint32_t ipHdrLen  = ip4 ? (ip4->HdrLength * 4u) : 40u;
    uint32_t tcpHdrLen = tcp->HdrLength * 4u;
    uint32_t headerLen = ipHdrLen + tcpHdrLen;
    uint32_t remaining = payloadLen - static_cast<uint32_t>(fragSize);

    uint32_t f1Len = headerLen + fragSize;
    uint32_t f2Len = headerLen + remaining;

    if (f1Len > MAX_PACKET_SIZE || f2Len > MAX_PACKET_SIZE) return false;

    memcpy(m_fragBuf1.data(), packet, headerLen);
    memcpy(m_fragBuf1.data() + headerLen, payload, fragSize);
    if (ip4)
        reinterpret_cast<WINDIVERT_IPHDR*>(m_fragBuf1.data())->Length = htons(static_cast<uint16_t>(f1Len));
    else
        reinterpret_cast<WINDIVERT_IPV6HDR*>(m_fragBuf1.data())->Length = htons(static_cast<uint16_t>(f1Len - 40));

    memcpy(m_fragBuf2.data(), packet, headerLen);
    memcpy(m_fragBuf2.data() + headerLen, payload + fragSize, remaining);
    reinterpret_cast<WINDIVERT_TCPHDR*>(m_fragBuf2.data() + ipHdrLen)->SeqNum =
        htonl(ntohl(tcp->SeqNum) + static_cast<uint32_t>(fragSize));

    if (ip4)
        reinterpret_cast<WINDIVERT_IPHDR*>(m_fragBuf2.data())->Length = htons(static_cast<uint16_t>(f2Len));
    else
        reinterpret_cast<WINDIVERT_IPV6HDR*>(m_fragBuf2.data())->Length = htons(static_cast<uint16_t>(f2Len - 40));

    WinDivertHelperCalcChecksums(m_fragBuf1.data(), f1Len, addr, 0);
    WinDivertHelperCalcChecksums(m_fragBuf2.data(), f2Len, addr, 0);

    if (reverse) {
        WinDivertSend(h, m_fragBuf2.data(), f2Len, nullptr, addr);
        WinDivertSend(h, m_fragBuf1.data(), f1Len, nullptr, addr);
    } else {
        WinDivertSend(h, m_fragBuf1.data(), f1Len, nullptr, addr);
        WinDivertSend(h, m_fragBuf2.data(), f2Len, nullptr, addr);
    }
    return true;
}

void WinDivertWorker::sendFakePacket(
    HANDLE h, uint8_t* packet, uint32_t packetLen, WINDIVERT_ADDRESS* addr,
    WINDIVERT_IPHDR* ip4, WINDIVERT_IPV6HDR* ip6, WINDIVERT_TCPHDR* tcp)
{
    if (!packet || packetLen == 0 || !tcp) return;

    uint32_t ipHdrLen  = ip4 ? (ip4->HdrLength * 4u) : 40u;
    uint32_t tcpHdrLen = tcp->HdrLength * 4u;
    uint32_t headerLen = ipHdrLen + tcpHdrLen;
    constexpr uint32_t FAKE_SZ = 16;
    uint32_t fakeLen = headerLen + FAKE_SZ;

    if (fakeLen > MAX_PACKET_SIZE) return;

    std::vector<uint8_t> localBuf(fakeLen);
    memcpy(localBuf.data(), packet, headerLen);

    for (uint32_t i = 0; i < FAKE_SZ; ++i)
        localBuf[headerLen + i] = static_cast<uint8_t>(rand() % 256);

    if (ip4) {
        auto* h4   = reinterpret_cast<WINDIVERT_IPHDR*>(localBuf.data());
        h4->Length = htons(static_cast<uint16_t>(fakeLen));
        h4->TTL    = static_cast<uint8_t>(m_config.fakePacketTTL);
    } else if (ip6) {
        auto* h6     = reinterpret_cast<WINDIVERT_IPV6HDR*>(localBuf.data());
        h6->Length   = htons(static_cast<uint16_t>(fakeLen - 40));
        h6->HopLimit = static_cast<uint8_t>(m_config.fakePacketTTL);
    }

    WinDivertHelperCalcChecksums(localBuf.data(), fakeLen, addr, 0);
    WinDivertSend(h, localBuf.data(), fakeLen, nullptr, addr);
}

void WinDivertWorker::sendWrongChecksumPacket(
    HANDLE h, uint8_t* packet, uint32_t packetLen, WINDIVERT_ADDRESS* addr,
    WINDIVERT_IPHDR* ip4, WINDIVERT_IPV6HDR* ip6, WINDIVERT_TCPHDR* tcp)
{
    if (!packet || packetLen == 0 || !tcp || packetLen > MAX_PACKET_SIZE) return;

    uint32_t ipHdrLen = ip4 ? (ip4->HdrLength * 4u) : 40u;

    std::vector<uint8_t> localBuf(packetLen);
    memcpy(localBuf.data(), packet, packetLen);

    reinterpret_cast<WINDIVERT_TCPHDR*>(localBuf.data() + ipHdrLen)->Checksum = 0xDEAD;

    if (ip4)
        reinterpret_cast<WINDIVERT_IPHDR*>(localBuf.data())->TTL = static_cast<uint8_t>(m_config.fakePacketTTL);
    else if (ip6)
        reinterpret_cast<WINDIVERT_IPV6HDR*>(localBuf.data())->HopLimit = static_cast<uint8_t>(m_config.fakePacketTTL);

    WinDivertSend(h, localBuf.data(), packetLen, nullptr, addr);
}

void WinDivertWorker::sendWrongSeqPacket(
    HANDLE h, uint8_t* packet, uint32_t packetLen, WINDIVERT_ADDRESS* addr,
    WINDIVERT_IPHDR* ip4, WINDIVERT_IPV6HDR* ip6, WINDIVERT_TCPHDR* tcp)
{
    if (!packet || packetLen == 0 || !tcp || packetLen > MAX_PACKET_SIZE) return;

    uint32_t ipHdrLen = ip4 ? (ip4->HdrLength * 4u) : 40u;

    std::vector<uint8_t> localBuf(packetLen);
    memcpy(localBuf.data(), packet, packetLen);

    auto* fakeTcp   = reinterpret_cast<WINDIVERT_TCPHDR*>(localBuf.data() + ipHdrLen);
    fakeTcp->SeqNum = htonl(ntohl(tcp->SeqNum) ^ 0x0000C001);

    if (ip4)
        reinterpret_cast<WINDIVERT_IPHDR*>(localBuf.data())->TTL = static_cast<uint8_t>(m_config.fakePacketTTL);
    else if (ip6)
        reinterpret_cast<WINDIVERT_IPV6HDR*>(localBuf.data())->HopLimit = static_cast<uint8_t>(m_config.fakePacketTTL);

    WinDivertHelperCalcChecksums(localBuf.data(), packetLen, addr, 0);
    WinDivertSend(h, localBuf.data(), packetLen, nullptr, addr);
}

bool WinDivertWorker::redirectDnsOutbound(
    HANDLE h, uint8_t* packet, uint32_t packetLen, WINDIVERT_ADDRESS* addr,
    WINDIVERT_IPHDR* ip4, WINDIVERT_UDPHDR* udp)
{
    if (!packet || packetLen == 0 || !ip4) {
        return false;
    }

    std::vector<uint8_t> pkt(packetLen);
    memcpy(pkt.data(), packet, packetLen);

    uint32_t ipHdrLen = reinterpret_cast<WINDIVERT_IPHDR*>(pkt.data())->HdrLength * 4u;
    auto* newIp4 = reinterpret_cast<WINDIVERT_IPHDR*>(pkt.data());
    auto* newUdp = reinterpret_cast<WINDIVERT_UDPHDR*>(pkt.data() + ipHdrLen);

    QHostAddress dnsAddr(m_config.dnsServer);
    if (dnsAddr.isNull()) {
        return false;
    }

    newIp4->DstAddr = htonl(dnsAddr.toIPv4Address());
    newUdp->DstPort = htons(static_cast<uint16_t>(m_config.dnsPort));

    WinDivertHelperCalcChecksums(pkt.data(), packetLen, addr, 0);
    WinDivertSend(h, pkt.data(), packetLen, nullptr, addr);
    return true;
}

bool WinDivertWorker::rewriteDnsInbound(
    HANDLE h, uint8_t* packet, uint32_t packetLen, WINDIVERT_ADDRESS* addr,
    WINDIVERT_IPHDR* ip4, WINDIVERT_UDPHDR* udp,
    quint32 origSrcIp, quint16 origSrcPort)
{
    if (!packet || packetLen == 0 || !ip4) {
        return false;
    }

    std::vector<uint8_t> pkt(packetLen);
    memcpy(pkt.data(), packet, packetLen);

    uint32_t ipHdrLen = reinterpret_cast<WINDIVERT_IPHDR*>(pkt.data())->HdrLength * 4u;
    auto* newIp4 = reinterpret_cast<WINDIVERT_IPHDR*>(pkt.data());
    auto* newUdp = reinterpret_cast<WINDIVERT_UDPHDR*>(pkt.data() + ipHdrLen);

    newIp4->SrcAddr = htonl(origSrcIp);
    newUdp->SrcPort = htons(origSrcPort);

    WinDivertHelperCalcChecksums(pkt.data(), packetLen, addr, 0);
    WinDivertSend(h, pkt.data(), packetLen, nullptr, addr);
    return true;
}

#endif // PLATFORM_WINDOWS