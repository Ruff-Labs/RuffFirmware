#pragma once
#include <common/protocol.h>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUdpSocket>
#include <QVariantList>

// GUI side of the firmware -> GUI link. The firmware sends a protocol::RaceStatus packet
// (UDP, protocol::kGuiPort) ~20x a second; this keeps the latest one and exposes it to QML
// as properties, so the screens bind to `backend.clockMs` etc. instead of fake data.
// Registered in main.cpp as the context property "backend" (same as virtualRemote).
//
// Every property uses the one statusChanged() signal: a packet updates everything at once.
// Lane properties are 2-item lists: [0] = lane 1 (LEFT), [1] = lane 2 (RIGHT).
class Backend : public QObject {
    Q_OBJECT

    // link
    Q_PROPERTY(bool         connected    READ connected    NOTIFY statusChanged)  // packet in the last 1 s

    // timing / race
    Q_PROPERTY(QString      raceState    READ raceState    NOTIFY statusChanged)  // "Idle", "Ready", "Running", ...
    Q_PROPERTY(bool         judgeReady   READ judgeReady   NOTIFY statusChanged)
    Q_PROPERTY(double       clockMs      READ clockMs      NOTIFY statusChanged)  // negative in countdown
    Q_PROPERTY(int          winner       READ winner       NOTIFY statusChanged)  // -1 none, 0 lane 1, 1 lane 2

    // sensors: alignment + triggers, per lane
    Q_PROPERTY(QVariantList aligned      READ aligned      NOTIFY statusChanged)  // bool per lane
    Q_PROPERTY(QVariantList beamBroken   READ beamBroken   NOTIFY statusChanged)  // bool per lane

    // results, per lane
    Q_PROPERTY(QVariantList splits       READ splits       NOTIFY statusChanged)  // int per lane
    Q_PROPERTY(QVariantList raceTimes    READ raceTimes    NOTIFY statusChanged)  // string per lane: "12.345" or "DNF"
    Q_PROPERTY(QVariantList startMs      READ startMs      NOTIFY statusChanged)  // double per lane (negative = false start)
    Q_PROPERTY(QVariantList falseStarts  READ falseStarts  NOTIFY statusChanged)  // int per lane

    // network
    Q_PROPERTY(QString      ipAddress    READ ipAddress    NOTIFY networkChanged) // "" = no network

public:
    explicit Backend(QObject* parent = nullptr);

    bool         connected()   const { return connected_; }
    QString      raceState()   const;
    bool         judgeReady()  const;
    double       clockMs()     const;
    int          winner()      const;
    QVariantList aligned()     const;
    QVariantList beamBroken()  const;
    QVariantList splits()      const;
    QVariantList raceTimes()   const;
    QVariantList startMs()     const;
    QVariantList falseStarts() const;
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
    QString             ip_;
};
