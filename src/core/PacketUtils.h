#pragma once
#include <cstdint>
#include <cstring>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QMap>
#include "platform/IPlatformBackend.h"


static constexpr uint8_t  TLS_HANDSHAKE      = 0x16;
static constexpr uint8_t  TLS_CLIENT_HELLO   = 0x01;
static constexpr uint16_t TLS_EXTENSION_SNI  = 0x0000;
static constexpr uint16_t TLS_EXTENSION_ECH  = 0xFE0D; // RFC 9001 ECH
static constexpr uint8_t  SNI_TYPE_HOST_NAME = 0x00;


struct TlsFlowBuffer {
    QByteArray data;
    QList<QByteArray> rawPackets;
    uint32_t   expectedLen = 0;
    quint64    lastUpdate  = 0;
};

inline uint32_t tlsRecordTotalLen(const uint8_t* buf, uint32_t bufLen)
{
    if (bufLen < 5) return 0;
    if (buf[0] != TLS_HANDSHAKE) return 0;
    uint32_t recordLen = (static_cast<uint32_t>(buf[3]) << 8) | buf[4];
    return recordLen + 5u; // 5 byte TLS record header
}

inline bool accumulateTlsFragment(TlsFlowBuffer& flow,
                                  const uint8_t* payload, uint32_t pLen,
                                  QByteArray& assembled)
{
    flow.data.append(reinterpret_cast<const char*>(payload), static_cast<int>(pLen));

    if (flow.expectedLen == 0) {
        uint32_t total = tlsRecordTotalLen(
            reinterpret_cast<const uint8_t*>(flow.data.constData()),
            static_cast<uint32_t>(flow.data.size()));
        if (total == 0) return false;
        flow.expectedLen = total;
    }

    if (static_cast<uint32_t>(flow.data.size()) >= flow.expectedLen) {
        assembled = flow.data.left(static_cast<int>(flow.expectedLen));
        flow.data.clear();
        flow.expectedLen = 0;
        return true;
    }
    return false;
}

inline QByteArray buildSniExtension(const QString& hostname)
{
    QByteArray name = hostname.toLatin1();
    uint16_t nameLen     = static_cast<uint16_t>(name.size());
    uint16_t sniListLen  = nameLen + 3;  // type(1) + len(2) + name
    uint16_t extDataLen  = sniListLen + 2; // list_length field

    QByteArray ext;
    // Extension type: SNI (0x0000)
    ext.append('\x00'); ext.append('\x00');
    // Extension length
    ext.append(static_cast<char>((extDataLen >> 8) & 0xFF));
    ext.append(static_cast<char>(extDataLen & 0xFF));
    // SNI list length
    ext.append(static_cast<char>((sniListLen >> 8) & 0xFF));
    ext.append(static_cast<char>(sniListLen & 0xFF));
    // Name type: host_name (0x00)
    ext.append('\x00');
    // Name length
    ext.append(static_cast<char>((nameLen >> 8) & 0xFF));
    ext.append(static_cast<char>(nameLen & 0xFF));
    ext.append(name);
    return ext;
}

inline QByteArray buildGreaseEchExtension()
{
    static const uint8_t greasePayload[] = {
        0x00, 0x00, 0x01, 0x00, 0x01, 0x68, 0x00, 0x20,
        0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
        0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
        0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
        0x00, 0x00
    };
    uint16_t payloadLen = sizeof(greasePayload);

    QByteArray ext;
    ext.append(static_cast<char>((TLS_EXTENSION_ECH >> 8) & 0xFF));
    ext.append(static_cast<char>(TLS_EXTENSION_ECH & 0xFF));
    ext.append(static_cast<char>((payloadLen >> 8) & 0xFF));
    ext.append(static_cast<char>(payloadLen & 0xFF));
    ext.append(reinterpret_cast<const char*>(greasePayload), payloadLen);
    return ext;
}

inline QByteArray injectECH(const uint8_t* payload, uint32_t payloadLen,
                            const QString& outerSni)
{
    if (payloadLen < 5) return {};
    if (payload[0] != TLS_HANDSHAKE) return {};

    QByteArray record(reinterpret_cast<const char*>(payload),
                      static_cast<int>(payloadLen));

    uint32_t offset = 5;
    if (offset + 4 > payloadLen) return {};
    if (payload[offset] != TLS_CLIENT_HELLO) return {};
    // Handshake header: type(1)+length(3)
    uint32_t hsLenOffset = offset + 1;
    offset += 4;

    // client_version(2) + random(32) + session_id_len(1)
    if (offset + 34 + 1 > payloadLen) return {};
    offset += 34;
    uint8_t sessionIdLen = payload[offset++];
    if (offset + sessionIdLen > payloadLen) return {};
    offset += sessionIdLen;

    // cipher_suites
    if (offset + 2 > payloadLen) return {};
    uint16_t cipherLen = (payload[offset] << 8) | payload[offset + 1];
    offset += 2 + cipherLen;
    if (offset > payloadLen) return {};

    // compression_methods
    if (offset + 1 > payloadLen) return {};
    uint8_t compLen = payload[offset++];
    offset += compLen;
    if (offset > payloadLen) return {};

    // save extensions_length offset
    if (offset + 2 > payloadLen) return {};
    uint32_t extLenOffset = offset;
    uint16_t extTotalLen  = (payload[offset] << 8) | payload[offset + 1];
    offset += 2;

    uint32_t extStart = offset;
    uint32_t extEnd   = offset + extTotalLen;
    if (extEnd > payloadLen) return {};

    QByteArray newExts;
    uint32_t pos = extStart;

    bool sniReplaced = false;
    while (pos + 4 <= extEnd) {
        uint16_t eType = (payload[pos] << 8)     | payload[pos + 1];
        uint16_t eLen  = (payload[pos + 2] << 8) | payload[pos + 3];
        pos += 4;

        if (eType == TLS_EXTENSION_SNI && !outerSni.isEmpty()) {
            newExts.append(buildSniExtension(outerSni));
            sniReplaced = true;
        } else if (eType == TLS_EXTENSION_ECH) {
            // skip
        } else {
            // copy
            newExts.append(static_cast<char>((eType >> 8) & 0xFF));
            newExts.append(static_cast<char>(eType & 0xFF));
            newExts.append(static_cast<char>((eLen >> 8) & 0xFF));
            newExts.append(static_cast<char>(eLen & 0xFF));
            if (pos + eLen <= extEnd)
                newExts.append(reinterpret_cast<const char*>(payload + pos), eLen);
        }
        pos += eLen;
    }

    if (!sniReplaced && !outerSni.isEmpty())
        newExts.append(buildSniExtension(outerSni));

    newExts.append(buildGreaseEchExtension());

    uint32_t headerUpToExts = extLenOffset; // record header + hs header + ... + comp
    QByteArray result;
    result.append(record.left(static_cast<int>(headerUpToExts)));

    uint16_t newExtLen = static_cast<uint16_t>(newExts.size());
    result.append(static_cast<char>((newExtLen >> 8) & 0xFF));
    result.append(static_cast<char>(newExtLen & 0xFF));
    result.append(newExts);

    // update TLS record length  (byte 3-4)
    uint32_t newRecordBodyLen = static_cast<uint32_t>(result.size()) - 5;
    result[3] = static_cast<char>((newRecordBodyLen >> 8) & 0xFF);
    result[4] = static_cast<char>(newRecordBodyLen & 0xFF);

    // update handshake length  (byte hsLenOffset+1 .. +3, 3 byte big-endian)
    uint32_t newHsBodyLen = newRecordBodyLen - 4;
    result[static_cast<int>(hsLenOffset)]     = static_cast<char>((newHsBodyLen >> 16) & 0xFF);
    result[static_cast<int>(hsLenOffset) + 1] = static_cast<char>((newHsBodyLen >> 8)  & 0xFF);
    result[static_cast<int>(hsLenOffset) + 2] = static_cast<char>(newHsBodyLen & 0xFF);

    return result;
}


inline QString extractSNI(const uint8_t* payload, uint32_t payloadLen)
{
    if (payloadLen < 5) return {};
    if (payload[0] != TLS_HANDSHAKE) return {};

    uint32_t offset = 5;

    if (offset + 4 > payloadLen) return {};
    if (payload[offset] != TLS_CLIENT_HELLO) return {};
    offset += 4; // Handshake header (type+length)

    // client_version(2) + random(32)
    if (offset + 34 > payloadLen) return {};
    offset += 34;

    // session_id
    if (offset + 1 > payloadLen) return {};
    uint8_t sessionIdLen = payload[offset++];
    if (offset + sessionIdLen > payloadLen) return {};
    offset += sessionIdLen;

    // cipher_suites
    if (offset + 2 > payloadLen) return {};
    uint16_t cipherLen = (payload[offset] << 8) | payload[offset + 1];
    offset += 2;
    if (offset + cipherLen > payloadLen) return {};
    offset += cipherLen;

    // compression_methods
    if (offset + 1 > payloadLen) return {};
    uint8_t compLen = payload[offset++];
    if (offset + compLen > payloadLen) return {};
    offset += compLen;

    // extensions_length
    if (offset + 2 > payloadLen) return {};
    uint16_t extTotalLen = (payload[offset] << 8) | payload[offset + 1];
    offset += 2;
    if (offset + extTotalLen > payloadLen) return {};

    uint32_t extEnd = offset + extTotalLen;

    while (offset + 4 <= extEnd) {
        uint16_t extType = (payload[offset] << 8) | payload[offset + 1];
        uint16_t extLen  = (payload[offset + 2] << 8) | payload[offset + 3];
        offset += 4;

        if (extType == TLS_EXTENSION_SNI) {
            // SNI list_length(2) + name_type(1) + name_length(2) + name
            if (offset + 5 > extEnd) return {};
            offset += 2; // list_length
            if (payload[offset++] != SNI_TYPE_HOST_NAME) return {};
            uint16_t nameLen = (payload[offset] << 8) | payload[offset + 1];
            offset += 2;
            if (offset + nameLen > extEnd) return {};
            return QString::fromLatin1(
                reinterpret_cast<const char*>(payload + offset), nameLen);
        }
        offset += extLen;
    }
    return {};
}


inline QString extractHttpHost(const uint8_t* payload, uint32_t payloadLen)
{
    if (payloadLen < 10) return {};

    QByteArray data(reinterpret_cast<const char*>(payload),
                    static_cast<int>(payloadLen));

    int hostIdx = data.indexOf("\r\nHost: ");
    int start;
    if (hostIdx >= 0) {
        start = hostIdx + 8;
    } else {
        hostIdx = data.indexOf("Host: ");
        if (hostIdx < 0) return {};
        start = hostIdx + 6;
    }

    int end = data.indexOf("\r\n", start);
    if (end < 0) end = static_cast<int>(payloadLen);

    QString host = QString::fromLatin1(data.mid(start, end - start));

    int colonIdx = host.lastIndexOf(':');
    if (colonIdx > 0) host = host.left(colonIdx);
    return host.trimmed();
}

inline QByteArray injectZeroRatingHost(const uint8_t* payload,
                                       uint32_t        payloadLen,
                                       const QString&  zeroHost)
{
    if (payloadLen < 10 || zeroHost.isEmpty()) return {};

    QByteArray data(reinterpret_cast<const char*>(payload),
                    static_cast<int>(payloadLen));

    int hostIdx = data.indexOf("\r\nHost: ");
    int prefixEnd;
    if (hostIdx >= 0) {
        prefixEnd = hostIdx + 8;
    } else {
        hostIdx = data.indexOf("Host: ");
        if (hostIdx < 0) return {};
        prefixEnd = hostIdx + 6;
    }

    int lineEnd = data.indexOf("\r\n", prefixEnd);
    if (lineEnd < 0) lineEnd = static_cast<int>(payloadLen);

    QByteArray newHost = zeroHost.toLatin1();
    QByteArray result;
    result.reserve(data.size() - (lineEnd - prefixEnd) + newHost.size());
    result  = data.left(prefixEnd);
    result += newHost;
    result += data.mid(lineEnd);
    return result;
}

inline bool matchesHostList(const QString& host, const QStringList& list)
{
    if (host.isEmpty()) return false;
    for (QString entry : list) {
        entry = entry.trimmed();

        if (entry.startsWith("*.")) {
            entry = entry.mid(2);
        } else if (entry.startsWith("*")) {
            entry = entry.mid(1);
        }

        if (host.compare(entry, Qt::CaseInsensitive) == 0) return true;
        if (host.endsWith(QLatin1Char('.') + entry, Qt::CaseInsensitive)) return true;
    }
    return false;
}


inline QString extractDnsQuery(const uint8_t* payload, uint32_t payloadLen)
{
    if (payloadLen < 12) return {};

    uint16_t qdcount = (payload[4] << 8) | payload[5];
    if (qdcount == 0) return {};

    uint32_t offset = 12;
    QString domain;

    while (offset < payloadLen) {
        uint8_t len = payload[offset++];
        if (len == 0) break;

        if ((len & 0xC0) == 0xC0) {
            offset++;
            break;
        }

        if (offset + len > payloadLen) return {};
        if (!domain.isEmpty()) domain += ".";

        domain += QString::fromLatin1(reinterpret_cast<const char*>(payload + offset), len);
        offset += len;
    }
    return domain;
}

inline bool shouldBypass(const QString& host, const BypassConfig& config)
{
    switch (config.mode) {
    case BypassMode::Disabled:
        return false;
    case BypassMode::GlobalMode:
        return true;
    case BypassMode::BlacklistMode:
        if (config.blacklist.isEmpty()) return false;
        return matchesHostList(host, config.blacklist);
    case BypassMode::WhitelistMode:
        if (config.whitelist.isEmpty()) return false;
        return matchesHostList(host, config.whitelist);
    }
    return false;
}