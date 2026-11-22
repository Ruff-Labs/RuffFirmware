#pragma once
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QUdpSocket>

// GUI -> firmware commands (UDP to protocol::kRemotePort):
//   press():     the Remote screen's buttons, standing in for the RF remote until the receiver
//                is wired up, plus "resetRace" (head table). One protocol::Command byte per press.
//   setupRace(): the Setup screen's "Start Race": a protocol::SetupPacket with the lane names
//                and heats. The firmware treats it as a new race and echoes it in RaceStatus.
//
// Firmware address: 127.0.0.1 (GUI and firmware on the same board). To drive the board
// from a laptop, start the GUI with RUFF_BOARD_HOST=<board-ip>.
class VirtualRemote : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString host READ host CONSTANT)
    Q_PROPERTY(QString lastPress READ lastPress NOTIFY lastPressChanged)

public:
    explicit VirtualRemote(QObject* parent = nullptr);

    // button: "ready", "start", "stop", "reset", "abort" or "resetRace"
    Q_INVOKABLE void press(const QString& button);
    // names longer than protocol::kLaneNameLen - 1 UTF-8 bytes are cut; heats is clamped to 1..255
    Q_INVOKABLE void setupRace(const QString& lane1, const QString& lane2, int heats);

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
