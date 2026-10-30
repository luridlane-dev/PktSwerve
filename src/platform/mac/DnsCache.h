#pragma once

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>

// Keeps DNS answers until their TTL runs out, so a name that was just looked up
// costs nothing the second time. Used by the macOS DNS proxy, where every
// lookup would otherwise travel to the (possibly far away) redirect server.
//
// A cached answer is replayed with the asking client's transaction id and
// spelling of the name, and with its TTLs counted down by the time it spent in
// the cache. Queries that set the DNSSEC OK bit are cached apart from the
// others, because their answers carry signatures.
class DnsCache {
public:
    // The cached reply for `query`, or an empty array when there is nothing fresh.
    QByteArray lookup(const QByteArray& query)
    {
        int qEnd = 0;
        const QByteArray key = keyFor(query, qEnd);
        if (key.isEmpty()) return {};

        QMutexLocker lock(&m_mutex);
        const auto it = m_entries.find(key);
        if (it == m_entries.end()) return {};
        const qint64 now = nowMs();
        if (now >= it->expiresMs) {
            m_entries.erase(it);
            return {};
        }

        QByteArray reply = it->reply;
        reply[0] = query[0];   // transaction id
        reply[1] = query[1];
        // Resolvers may randomise the case of the name; answer in the case that was asked.
        reply.replace(12, qEnd - 12, query.mid(12, qEnd - 12));
        ageTtls(reply, qEnd, static_cast<uint32_t>((now - it->storedMs) / 1000));
        return reply;
    }

    void store(const QByteArray& query, const QByteArray& reply)
    {
        int qEnd = 0;
        const QByteArray key = keyFor(query, qEnd);
        if (key.isEmpty()) return;

        const int ttl = minTtl(reply, qEnd);
        if (ttl <= 0) return;

        QMutexLocker lock(&m_mutex);
        if (m_entries.size() >= kMaxEntries)
            dropExpired();
        if (m_entries.size() >= kMaxEntries)
            m_entries.clear();
        const qint64 now = nowMs();
        m_entries.insert(key, {reply, now, now + std::min(ttl, kMaxTtlSeconds) * 1000});
    }

private:
    struct Entry {
        QByteArray reply;
        qint64     storedMs;
        qint64     expiresMs;
    };

    static constexpr int kMaxEntries    = 2048;
    static constexpr int kMaxTtlSeconds = 120;   // keep answers fresh even for long TTLs

    QMutex                   m_mutex;
    QHash<QByteArray, Entry> m_entries;

    static qint64 nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void dropExpired()
    {
        const qint64 now = nowMs();
        for (auto it = m_entries.begin(); it != m_entries.end();)
            it = (now >= it->expiresMs) ? m_entries.erase(it) : std::next(it);
    }

    static uint16_t be16(const QByteArray& m, int off)
    {
        return static_cast<uint16_t>((static_cast<uint8_t>(m[off]) << 8) | static_cast<uint8_t>(m[off + 1]));
    }

    // Offset just past the name starting at `off` (plain labels or a pointer), or -1.
    static int skipName(const QByteArray& m, int off)
    {
        for (int labels = 0; off < m.size() && labels < 128; ++labels) {
            const uint8_t len = static_cast<uint8_t>(m[off]);
            if (len == 0) return off + 1;
            if ((len & 0xC0) == 0xC0) return off + 2 <= m.size() ? off + 2 : -1;
            if ((len & 0xC0) != 0 || off + 1 + len > m.size()) return -1;
            off += 1 + len;
        }
        return -1;
    }

    // Calls fn(type, ttlOffset) for each of the `records` resource records that
    // follow the question. False if the message is malformed.
    template <typename Fn>
    static bool forEachRecord(const QByteArray& m, int qEnd, int records, Fn fn)
    {
        int off = qEnd;
        for (int i = 0; i < records; ++i) {
            off = skipName(m, off);
            if (off < 0 || off + 10 > m.size()) return false;
            fn(be16(m, off), off + 4);
            off += 10 + be16(m, off + 8);
            if (off > m.size()) return false;
        }
        return true;
    }

    static int recordCount(const QByteArray& m)
    {
        return be16(m, 6) + be16(m, 8) + be16(m, 10);
    }

    // True when the query asks for DNSSEC records (the DO bit of its EDNS option).
    static bool dnssecOk(const QByteArray& query, int qEnd)
    {
        bool dnssec = false;
        forEachRecord(query, qEnd, recordCount(query), [&](int type, int ttlOff) {
            if (type == 41 && (static_cast<uint8_t>(query[ttlOff + 2]) & 0x80))
                dnssec = true;
        });
        return dnssec;
    }

    // Lower-cased question (name, type, class) of a plain one-question query, plus
    // a marker for the DNSSEC OK bit; `qEnd` is where the question ends. Empty
    // when the message is not something worth caching.
    static QByteArray keyFor(const QByteArray& query, int& qEnd)
    {
        if (query.size() < 17 || be16(query, 4) != 1) return {};
        const int nameEnd = skipName(query, 12);
        if (nameEnd < 0 || nameEnd + 4 > query.size()) return {};
        qEnd = nameEnd + 4;
        QByteArray key = query.mid(12, qEnd - 12).toLower();
        key.append(dnssecOk(query, qEnd) ? '\x01' : '\x00');
        return key;
    }

    // Smallest TTL of the records in a reply that is safe to cache, else 0.
    static int minTtl(const QByteArray& r, int qEnd)
    {
        if (r.size() < 17) return 0;
        const uint8_t flags = static_cast<uint8_t>(r[2]);
        const bool isReply = (flags & 0x80) != 0;
        const bool truncated = (flags & 0x02) != 0;
        const int rcode = static_cast<uint8_t>(r[3]) & 0x0F;
        if (!isReply || truncated || rcode != 0 || be16(r, 4) != 1 || be16(r, 6) == 0) return 0;

        int ttl = INT32_MAX;
        const bool ok = forEachRecord(r, qEnd, recordCount(r), [&](int type, int ttlOff) {
            if (type == 41) return;   // OPT's ttl field holds flags, not a lifetime
            const uint32_t t = (static_cast<uint32_t>(be16(r, ttlOff)) << 16) | be16(r, ttlOff + 2);
            ttl = std::min<int>(ttl, static_cast<int>(std::min<uint32_t>(t, 86400)));
        });
        return ok && ttl != INT32_MAX ? ttl : 0;
    }

    // Counts every record's TTL down by `seconds`, never below 1.
    static void ageTtls(QByteArray& reply, int qEnd, uint32_t seconds)
    {
        if (seconds == 0) return;
        forEachRecord(reply, qEnd, recordCount(reply), [&](int type, int ttlOff) {
            if (type == 41) return;
            const uint32_t t = (static_cast<uint32_t>(be16(reply, ttlOff)) << 16) | be16(reply, ttlOff + 2);
            const uint32_t aged = t > seconds ? t - seconds : 1;
            reply[ttlOff]     = static_cast<char>(aged >> 24);
            reply[ttlOff + 1] = static_cast<char>(aged >> 16);
            reply[ttlOff + 2] = static_cast<char>(aged >> 8);
            reply[ttlOff + 3] = static_cast<char>(aged);
        });
    }
};
