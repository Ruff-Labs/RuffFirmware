#pragma once
#include <common/protocol.h>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUdpSocket>
#include <QVariantList>

// Firmware status -> QML. The firmware's StatusSender sends a protocol::RaceStatus (UDP,
// protocol::kGuiPort) every 50 ms and on every state change; Backend keeps the newest one and
// exposes it as properties, so screens bind to `backend.clockMs` etc. Registered in main.cpp as
// the context property "backend".
//
// - Packets with the wrong size, magic or protocol version are ignored (logged once): rebuild
//   the GUI and firmware from the same commit.
// - No packet for 1 s -> connected = false, and screens show "Firmware not running".
// - The last finished heat's result stays visible (showingLastResult) until the next heat's
//   lights start or the race is reset, instead of vanishing when the firmware moves on.
// - Every property shares statusChanged(): a packet updates everything at once.
// - Lane properties are 2-item lists: [0] = lane 1 (LEFT), [1] = lane 2 (RIGHT).
class Backend : public QObject {
    Q_OBJECT

    // link + versions
    Q_PROPERTY(bool         connected         READ connected         NOTIFY statusChanged)
    Q_PROPERTY(QString      firmwareVersion   READ firmwareVersion   NOTIFY statusChanged)  // "" = not connected

    // race
    Q_PROPERTY(QString      raceState         READ raceState         NOTIFY statusChanged)  // "Idle", "Running", ... or "Not connected"
    Q_PROPERTY(bool         raceActive        READ raceActive        NOTIFY statusChanged)  // Sequence / Running / FalseStart
    Q_PROPERTY(bool         judgeReady        READ judgeReady        NOTIFY statusChanged)

    // results: the last finished heat's while showingLastResult, else live
    Q_PROPERTY(double       clockMs           READ clockMs           NOTIFY statusChanged)  // negative while counting down to green
    Q_PROPERTY(bool         showingLastResult READ showingLastResult NOTIFY statusChanged)
    Q_PROPERTY(int          winner            READ winner            NOTIFY statusChanged)  // -1 none, else lane index
    Q_PROPERTY(QVariantList splits            READ splits            NOTIFY statusChanged)  // int per lane
    Q_PROPERTY(QVariantList raceTimes         READ raceTimes         NOTIFY statusChanged)  // "12.345", "DNF" or "--.---" per lane
    Q_PROPERTY(QVariantList startMs           READ startMs           NOTIFY statusChanged)  // reaction ms per lane, undefined = none
    Q_PROPERTY(QVariantList falseStarts       READ falseStarts       NOTIFY statusChanged)  // int per lane (always live)

    // sensors, per lane
    Q_PROPERTY(QVariantList laneInUse         READ laneInUse         NOTIFY statusChanged)  // false = not wired (one-lane mode)
    Q_PROPERTY(QVariantList aligned           READ aligned           NOTIFY statusChanged)
    Q_PROPERTY(QVariantList forward           READ forward           NOTIFY statusChanged)  // FORWARD beam broken
    Q_PROPERTY(QVariantList back              READ back              NOTIFY statusChanged)  // BACK beam broken
    Q_PROPERTY(QVariantList beamBroken        READ beamBroken        NOTIFY statusChanged)  // FORWARD or BACK
    Q_PROPERTY(QVariantList sensorFaults      READ sensorFaults      NOTIFY statusChanged)  // "" = fine, else a sentence
    Q_PROPERTY(bool         anySensorFault    READ anySensorFault    NOTIFY statusChanged)
    Q_PROPERTY(bool         sensorsOffline    READ sensorsOffline    NOTIFY statusChanged)   // radio link down

    // race setup (echoed back by the firmware)
    Q_PROPERTY(QVariantList laneNames         READ laneNames         NOTIFY statusChanged)  // "Lane 1" if not set
    Q_PROPERTY(int          heats             READ heats             NOTIFY statusChanged)  // 0 = not set up

    // network: this machine's IP, and the firmware's Wi-Fi state
    Q_PROPERTY(QString      ipAddress         READ ipAddress         NOTIFY networkChanged) // "" = no network
    Q_PROPERTY(QString      wifiSsid          READ wifiSsid          NOTIFY statusChanged)  // "" = not on Wi-Fi / unknown
    Q_PROPERTY(QString      wifiState         READ wifiState         NOTIFY statusChanged)  // "idle", "working", "done", "failed"
    Q_PROPERTY(QString      wifiMessage       READ wifiMessage       NOTIFY statusChanged)

public:
    explicit Backend(QObject* parent = nullptr);

    bool         connected()         const { return connected_; }
    QString      firmwareVersion()   const;
    QString      raceState()         const;
    bool         raceActive()        const;
    bool         judgeReady()        const;
    double       clockMs()           const;
    bool         showingLastResult() const;
    int          winner()            const;
    QVariantList splits()            const;
    QVariantList raceTimes()         const;
    QVariantList startMs()           const;
    QVariantList falseStarts()       const;
    QVariantList laneInUse()         const;
    QVariantList aligned()           const;
    QVariantList forward()           const;
    QVariantList back()              const;
    QVariantList beamBroken()        const;
    QVariantList sensorFaults()      const;
    bool         anySensorFault()    const;
    bool         sensorsOffline()    const;
    QVariantList laneNames()         const;
    int          heats()             const;
    QString      ipAddress()         const { return ip_; }
    QString      wifiSsid()          const;
    QString      wifiState()         const;
    QString      wifiMessage()       const;

signals:
    void statusChanged();
    void networkChanged();

private:
    void read_packets();     // socket readyRead: keep the newest valid packet, snapshot finished heats
    void on_timeout();       // no packet for TIMEOUT_MS
    void check_network();    // every NETWORK_MS: first non-loopback IPv4 address

    // the packet results come from: the last finished heat while showingLastResult, else the live one
    const protocol::RaceStatus& results() const;

    QUdpSocket           socket_;
    QTimer               timeout_;
    QTimer               network_timer_;
    protocol::RaceStatus last_{};            // newest packet (zeros until the first one)
    protocol::RaceStatus result_{};          // last packet seen in Finished
    bool                 has_result_     = false;
    bool                 connected_      = false;
    bool                 warned_version_ = false;
    QString              ip_;
};
