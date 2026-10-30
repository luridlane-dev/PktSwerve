#pragma once
#ifdef PLATFORM_MACOS

#include "platform/IPlatformBackend.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

// What the menu bar app and its root helper say to each other: one line per
// message over a local socket.
//
//   app    -> helper:  START <config json>  |  STOP
//   helper -> app:     STARTED  |  STOPPED  |  STATS <connections> <bytes>  |  ERROR <message>
namespace PfIpc {

// Created by the helper, then handed to the user so no other account can connect.
inline QString socketPath(qulonglong uid)
{
    return QString("/tmp/pktswerve-%1.sock").arg(uid);
}

inline QByteArray configToJson(const BypassConfig& c)
{
    QJsonObject o;
    o["mode"]                 = static_cast<int>(c.mode);
    o["fragmentHttpRequests"] = c.fragmentHttpRequests;
    o["fragmentHttpsPersist"] = c.fragmentHttpsPersist;
    o["nativeFrag"]           = c.nativeFrag;
    o["reverseFrag"]          = c.reverseFrag;
    o["tlsRecordSplit"]       = c.tlsRecordSplit;
    o["wrongChecksum"]        = c.wrongChecksum;
    o["wrongSeq"]             = c.wrongSeq;
    o["fakePackets"]          = c.fakePackets;
    o["fakeVoiceUdp"]         = c.fakeVoiceUdp;
    o["httpFragmentSize"]     = c.httpFragmentSize;
    o["httpsFragmentSize"]    = c.httpsFragmentSize;
    o["fakePacketTTL"]        = c.fakePacketTTL;
    o["dnsRedirect"]          = c.dnsRedirect;
    o["dnsServer"]            = c.dnsServer;
    o["dnsPort"]              = c.dnsPort;
    o["blacklist"]            = QJsonArray::fromStringList(c.blacklist);
    o["whitelist"]            = QJsonArray::fromStringList(c.whitelist);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// Fields missing from the message keep the values already in `c`.
inline bool configFromJson(const QByteArray& data, BypassConfig& c)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject o = doc.object();

    c.mode                 = static_cast<BypassMode>(o["mode"].toInt(static_cast<int>(c.mode)));
    c.fragmentHttpRequests = o["fragmentHttpRequests"].toBool(c.fragmentHttpRequests);
    c.fragmentHttpsPersist = o["fragmentHttpsPersist"].toBool(c.fragmentHttpsPersist);
    c.nativeFrag           = o["nativeFrag"].toBool(c.nativeFrag);
    c.reverseFrag          = o["reverseFrag"].toBool(c.reverseFrag);
    c.tlsRecordSplit       = o["tlsRecordSplit"].toBool(c.tlsRecordSplit);
    c.wrongChecksum        = o["wrongChecksum"].toBool(c.wrongChecksum);
    c.wrongSeq             = o["wrongSeq"].toBool(c.wrongSeq);
    c.fakePackets          = o["fakePackets"].toBool(c.fakePackets);
    c.fakeVoiceUdp         = o["fakeVoiceUdp"].toBool(c.fakeVoiceUdp);
    c.httpFragmentSize     = o["httpFragmentSize"].toInt(c.httpFragmentSize);
    c.httpsFragmentSize    = o["httpsFragmentSize"].toInt(c.httpsFragmentSize);
    c.fakePacketTTL        = o["fakePacketTTL"].toInt(c.fakePacketTTL);
    c.dnsRedirect          = o["dnsRedirect"].toBool(c.dnsRedirect);
    c.dnsServer            = o["dnsServer"].toString(c.dnsServer);
    c.dnsPort              = o["dnsPort"].toInt(c.dnsPort);

    c.blacklist.clear();
    c.whitelist.clear();
    for (const QJsonValue& v : o["blacklist"].toArray()) c.blacklist << v.toString();
    for (const QJsonValue& v : o["whitelist"].toArray()) c.whitelist << v.toString();
    return true;
}

} // namespace PfIpc

#endif // PLATFORM_MACOS
