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

// Sends UDP DNS queries directly to avoid the system cache and routing rules.
DnsProbeReport probe(const QHostAddress& server, quint16 port,
                     const QVector<DnsProbeTarget>& targets,
                     int timeoutMs = 3000);

} // namespace DnsProbe
