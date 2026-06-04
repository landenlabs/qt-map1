#include "PhaseStats.h"

#include <QMutexLocker>
#include <QString>
#include <QtGlobal>
#include <algorithm>

PhaseStats &PhaseStats::instance() {
    static PhaseStats s;
    return s;
}

void PhaseStats::record(const char *name, qint64 ns) {
    QMutexLocker lock(&m_mutex);
    Entry &e = m_entries[QLatin1String(name)];
    e.count   += 1;
    e.totalNs += ns;
    e.maxNs    = std::max(e.maxNs, ns);
}

void PhaseStats::incr(const char *name) {
    QMutexLocker lock(&m_mutex);
    m_entries[QLatin1String(name)].count += 1;
}

void PhaseStats::resetAll() {
    QMutexLocker lock(&m_mutex);
    m_entries.clear();
}

void PhaseStats::dump() const {
    QMutexLocker lock(&m_mutex);
    if (m_entries.isEmpty())
        return;

    qInfo("PhaseStats: %-18s %8s %12s %10s %10s",
          "phase", "count", "total_ms", "avg_ms", "max_ms");

    QList<QString> names = m_entries.keys();
    std::sort(names.begin(), names.end());

    for (const QString &name : names) {
        const Entry &e = m_entries.value(name);
        const double totalMs = e.totalNs / 1.0e6;
        const double avgMs   = e.count ? totalMs / double(e.count) : 0.0;
        const double maxMs   = e.maxNs / 1.0e6;
        qInfo("PhaseStats: %-18s %8lld %12.2f %10.3f %10.3f",
              qPrintable(name),
              static_cast<long long>(e.count),
              totalMs, avgMs, maxMs);
    }
}
