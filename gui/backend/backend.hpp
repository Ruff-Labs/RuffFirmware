#pragma once
#include <common/protocol.h>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUdpSocket>
#include <QVariantList>

// GUI side of the firmware -> GUI link. The firmware sends a protocol::RaceStatus packet
// (UDP, protocol::kGuiPort) every 50 ms and on every state change (StatusSender in the firmware);
// this keeps the latest one and exposes it to QML as properties, so the screens bind to
// `backend.clockMs` etc. Packets with the wrong size, magic or version are ignored.
// Registered in main.cpp as the context property "backend" (same as virtualRemote).
//
// Every property uses the one statusChanged() signal: a packet updates everything at once.
// Lane properties are 2-item lists: [0] = lane 1 (LEFT), [1] = lane 2 (RIGHT).
class Backend : public QObject {
    Q_OBJECT

    // link
    Q_PROPERTY(bool         connected    READ connected    NOTIFY statusChanged)  // packet in the last 1 s

    // timing / race
    Q_PROPERTY(QString      raceState    READ raceState    NOTIFY statusChanged)  // protocol::race_state_name(): "Idle", "Running", ...
    Q_PROPERTY(bool         raceActive   READ raceActive   NOTIFY statusChanged)  // a heat is under way (Sequence/Running/FalseStart)
    Q_PROPERTY(bool         judgeReady   READ judgeReady   NOTIFY statusChanged)
    Q_PROPERTY(double       clockMs      READ clockMs      NOTIFY statusChanged)  // negative in countdown
    Q_PROPERTY(int          winner       READ winner       NOTIFY statusChanged)  // -1 none, 0 lane 1, 1 lane 2

    // sensors: alignment + triggers, per lane
    Q_PROPERTY(QVariantList aligned      READ aligned      NOTIFY statusChanged)  // bool per lane
    Q_PROPERTY(QVariantList beamBroken   READ beamBroken   NOTIFY statusChanged)  // bool per lane

    // results, per lane
    Q_PROPERTY(QVariantList splits       READ splits       NOTIFY statusChanged)  // int per lane
    Q_PROPERTY(QVariantList raceTimes    READ raceTimes    NOTIFY statusChanged)  // string per lane: "12.345" or "DNF"
    Q_PROPERTY(QVariantList startMs      READ startMs      NOTIFY statusChanged)  // double per lane (negative = false start), undefined = none yet
    Q_PROPERTY(QVariantList falseStarts  READ falseStarts  NOTIFY statusChanged)  // int per lane

    // race setup (what the Setup screen last sent, echoed back by the firmware)
    Q_PROPERTY(QVariantList laneNames    READ laneNames    NOTIFY statusChanged)  // string per lane, "Lane 1" if not set
    Q_PROPERTY(int          heats        READ heats        NOTIFY statusChanged)  // 0 = not set up

    // network
    Q_PROPERTY(QString      ipAddress    READ ipAddress    NOTIFY networkChanged) // "" = no network

public:
    explicit Backend(QObject* parent = nullptr);

    bool         connected()   const { return connected_; }
    QString      raceState()   const;
    bool         raceActive()  const;
    bool         judgeReady()  const;
    double       clockMs()     const;
    int          winner()      const;
    QVariantList aligned()     const;
    QVariantList beamBroken()  const;
    QVariantList splits()      const;
    QVariantList raceTimes()   const;
    QVariantList startMs()     const;
    QVariantList falseStarts() const;
    QVariantList laneNames()   const;
    int          heats()       const;
    QString      ipAddress()   const { return ip_; }

signals:
    void statusChanged();
    void networkChanged();

private:
    void read_packets();       // socket readyRead
    void on_timeout();         // no packet for 1 s
    void check_network();      // every few seconds

    QUdpSocket          socket_;
    QTimer              timeout_;          // single-shot, restarted by every packet
    QTimer              network_timer_;
    protocol::RaceStatus last_{};          // latest packet (all zeros until the first one)
    bool                connected_ = false;
    bool                warned_version_ = false;   // say "protocol mismatch" once, not 20x a second
    QString             ip_;
};
