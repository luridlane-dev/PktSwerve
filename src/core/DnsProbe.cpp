#include "DnsProbe.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QUdpSocket>

namespace {

bool skipDnsName(const QByteArray& message, int& offset)
{
    int labels = 0;
    while (offset < message.size() && labels++ < 128) {
        const quint8 length = static_cast<quint8>(message.at(offset));
        if (length == 0) {
            ++offset;
            return true;
        }
        if ((length & 0xC0) == 0xC0) {
            if (offset + 1 >= message.size()) return false;
            offset += 2;
            return true;
        }
        if ((length & 0xC0) != 0 || length > 63 ||
            offset + 1 + length > message.size()) {
            return false;
        }
        offset += 1 + length;
    }
    return false;
}

QByteArray buildQuery(const QString& hostname, quint16 transactionId)
{
    QByteArray query;
    query.reserve(512);
    query.append(static_cast<char>(transactionId >> 8));
    query.append(static_cast<char>(transactionId & 0xFF));
    query.append("\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00", 10);
    for (const QString& label : hostname.split('.', Qt::SkipEmptyParts)) {
        const QByteArray bytes = label.toLatin1();
        query.append(static_cast<char>(bytes.size()));
        query.append(bytes);
    }
    query.append('\0');
    query.append("\x00\x01\x00\x01", 4); // A, IN
    return query;
}

bool containsAddressAnswer(const QByteArray& message, quint16 transactionId,
                           QString& error)
{
    if (message.size() < 12) {
        error = "Truncated DNS response";
        return false;
    }

    const auto byte = [&message](int index) {
        return static_cast<quint8>(message.at(index));
    };
    const quint16 responseId = static_cast<quint16>((byte(0) << 8) | byte(1));
    if (responseId != transactionId || (byte(2) & 0x80) == 0) {
        error = "Unexpected DNS response";
        return false;
    }
    const quint8 rcode = byte(3) & 0x0F;
    if (rcode != 0) {
        error = QString("DNS response code %1").arg(rcode);
        return false;
    }

    const quint16 questions = static_cast<quint16>((byte(4) << 8) | byte(5));
    const quint16 answers = static_cast<quint16>((byte(6) << 8) | byte(7));
    int offset = 12;
    for (quint16 i = 0; i < questions; ++i) {
        if (!skipDnsName(message, offset) || offset + 4 > message.size()) {
            error = "Malformed DNS question";
            return false;
        }
        offset += 4;
    }

    for (quint16 i = 0; i < answers; ++i) {
        if (!skipDnsName(message, offset) || offset + 10 > message.size()) {
            error = "Malformed DNS answer";
            return false;
        }
        const quint16 type = static_cast<quint16>((byte(offset) << 8) | byte(offset + 1));
        const quint16 dataLength = static_cast<quint16>((byte(offset + 8) << 8) | byte(offset + 9));
        offset += 10;
        if (offset + dataLength > message.size()) {
            error = "Truncated DNS answer data";
            return false;
        }
        if ((type == 1 && dataLength == 4) || (type == 28 && dataLength == 16))
            return true;
        offset += dataLength;
    }

    error = "No A/AAAA address answer";
    return false;
}

} // namespace

namespace DnsProbe {

QVector<DnsProbeTarget> defaultTargets()
{
    return {
        { "Discord", "discord.com" },
        { "Discord Gateway", "gateway.discord.gg" },
        { "Discord CDN", "cdn.discordapp.com" },
        { "Roblox", "roblox.com" },
        { "X", "x.com" },
        { "Reddit", "reddit.com" },
        { "YouTube", "youtube.com" },
        { "Instagram", "instagram.com" },
        { "Telegram", "telegram.org" },
        { "GitHub", "github.com" },
        { "Spotify", "spotify.com" },
        { "Steam", "steamcommunity.com" }
    };
}

static DnsProbeReport probeAttempt(const QHostAddress& server, quint16 port,
                                   const QVector<DnsProbeTarget>& targets,
                                   int timeoutMs)
{
    DnsProbeReport report;
    if (server.isNull() || port == 0) {
        report.serverError = "Enter a valid DNS server address and port.";
        return report;
    }

    for (const DnsProbeTarget& target : targets) {
        DnsProbeTargetResult result;
        result.target = target;
        const quint16 transactionId = static_cast<quint16>(QRandomGenerator::global()->generate());
        const QByteArray query = buildQuery(target.host, transactionId);

        QUdpSocket socket;
        if (!socket.bind(server.protocol() == QAbstractSocket::IPv6Protocol
                             ? QHostAddress::AnyIPv6 : QHostAddress::AnyIPv4, 0)) {
            result.error = socket.errorString();
            report.targets.append(result);
            continue;
        }
        if (socket.writeDatagram(query, server, port) != query.size()) {
            result.error = socket.errorString();
            report.targets.append(result);
            continue;
        }

        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
            if (!socket.waitForReadyRead(remaining)) break;
            while (socket.hasPendingDatagrams()) {
                QByteArray response;
                response.resize(static_cast<int>(socket.pendingDatagramSize()));
                socket.readDatagram(response.data(), response.size());
                QString responseError;
                if (containsAddressAnswer(response, transactionId, responseError)) {
                    result.succeeded = true;
                    result.latencyMs = timer.elapsed();
                    break;
                }
            }
            if (result.succeeded) break;
        }
        if (!result.succeeded && result.error.isEmpty())
            result.error = "Timeout or no usable address response";
        report.targets.append(result);
    }
    return report;
}

DnsProbeReport probe(const QHostAddress& server, quint16 port,
                     const QVector<DnsProbeTarget>& targets, int timeoutMs)
{
    DnsProbeReport report;
    if (server.isNull() || port == 0) {
        report.serverError = "Enter a valid DNS server address and port.";
        return report;
    }

    // Retry transient UDP loss without exceeding the target timeout.
    constexpr int kAttempts = 3;
    const int attemptTimeout = qMax(500, timeoutMs / kAttempts);
    for (const DnsProbeTarget& target : targets) {
        DnsProbeTargetResult finalResult;
        finalResult.target = target;
        for (int attempt = 0; attempt < kAttempts; ++attempt) {
            const DnsProbeReport attemptReport = probeAttempt(
                server, port, {target}, attemptTimeout);
            if (!attemptReport.serverError.isEmpty()) {
                report.serverError = attemptReport.serverError;
                return report;
            }
            if (!attemptReport.targets.isEmpty()) {
                finalResult = attemptReport.targets.constFirst();
                if (finalResult.succeeded)
                    break;
            }
        }
        report.targets.append(finalResult);
    }
    return report;
}

} // namespace DnsProbe
