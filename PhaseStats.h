#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QString>

// PhaseStats – thread-safe phase timer for tile-loading instrumentation.
//
// Two ways to record:
//   PhaseStats::Scope scope("parse_float4");   // RAII; records elapsed ns on dtor
//   PhaseStats::instance().record("gpu_upload", elapsedNs);
//
// Plain counters (no timing):
//   PhaseStats::instance().incr("mem_hit");
//
// Call dump() to log the accumulated table.  Call resetAll() to start fresh.

class PhaseStats
{
public:
    static PhaseStats &instance();

    void record(const char *name, qint64 ns);
    void incr  (const char *name);
    void dump  () const;
    void resetAll();

    class Scope {
    public:
        explicit Scope(const char *name) : m_name(name) { m_timer.start(); }
        ~Scope() { PhaseStats::instance().record(m_name, m_timer.nsecsElapsed()); }
    private:
        const char    *m_name;
        QElapsedTimer  m_timer;
    };

private:
    PhaseStats() = default;

    struct Entry {
        qint64 count   = 0;
        qint64 totalNs = 0;
        qint64 maxNs   = 0;
    };

    mutable QMutex          m_mutex;
    QHash<QString, Entry>   m_entries;
};
