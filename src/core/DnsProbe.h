#pragma once

#include <QHostAddress>
#include <QString>
#include <QVector>

struct DnsProbeTarget {
    QString label;
    QString host;
};

struct DnsProbeTargetResult {
    DnsProbeTarget target;
    bool            succeeded = false;
    qint64          latencyMs = -1;
    QString         error;
};

struct DnsProbeReport {
    QString                        serverError;
    QVector<DnsProbeTargetResult>  targets;
};

namespace DnsProbe {

QVector<DnsProbeTarget> defaultTargets();

// Sends standard UDP DNS queries directly to the configured server and port.
// It deliberately does not use the operating-system resolver, so results are
// independent of the user's DNS cache and the bypass engine's routing rules.
DnsProbeReport probe(const QHostAddress& server, quint16 port,
                     const QVector<DnsProbeTarget>& targets,
                     int timeoutMs = 3000);

} // namespace DnsProbe
