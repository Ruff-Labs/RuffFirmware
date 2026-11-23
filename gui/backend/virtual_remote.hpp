#pragma once
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QUdpSocket>

// GUI -> firmware commands, UDP to protocol::kRemotePort. Registered in main.cpp as the context
// property "virtualRemote". UDP has no reply: the firmware's answer shows up in `backend`.
//
// Firmware address: 127.0.0.1 (same board). To drive the board from a laptop, start the GUI
// with RUFF_BOARD_HOST=<board-ip>.
class VirtualRemote : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString host      READ host      CONSTANT)
    Q_PROPERTY(QString lastPress READ lastPress NOTIFY lastPressChanged)   // what was SENT last, and when

public:
    explicit VirtualRemote(QObject* parent = nullptr);

    // The judge remote's buttons ("ready", "start", "stop", "reset", "abort"), standing in for the
    // RF remote until the receiver is wired, plus "resetRace" (head table). One byte per press.
    Q_INVOKABLE void press(const QString& button);

    // Setup screen "Start Race": the firmware starts a new race with these and echoes them back
    // (backend.laneNames / heats). Names are cut to fit (31 UTF-8 bytes); heats clamped to 1..255.
    Q_INVOKABLE void setupRace(const QString& lane1, const QString& lane2, int heats);

    // Settings screen "Connect": the firmware joins this Wi-Fi with nmcli (progress in
    // backend.wifiState / wifiMessage). Blank address = DHCP; address may be "a.b.c.d" or
    // "a.b.c.d/24". Returns "" if sent, else what's wrong with the input (nothing is sent).
    Q_INVOKABLE QString setWifi(const QString& ssid, const QString& password,
                                const QString& address, const QString& gateway, const QString& dns);

    // Settings screen "Restart": restart the firmware + GUI services (the screen goes blank briefly).
    Q_INVOKABLE void restartSoftware();

    QString host() const { return host_.toString(); }
    QString lastPress() const { return lastPress_; }

signals:
    void lastPressChanged();

private:
    void send(const char* data, qint64 size, const QString& what);

    QUdpSocket   socket_;
    QHostAddress host_;
    QString      lastPress_;
};
