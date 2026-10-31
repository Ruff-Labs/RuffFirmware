#pragma once
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QUdpSocket>

// Virtual judge remote: the Remote screen's buttons send presses to the firmware,
// standing in for the RF remote until the receiver is wired up.
// One UDP packet per press, one byte (protocol::RemoteButton), to protocol::kRemotePort.
//
// Firmware address: 127.0.0.1 (GUI and firmware on the same board). To drive the board
// from a laptop, start the GUI with RUFF_BOARD_HOST=<board-ip>.
class VirtualRemote : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString host READ host CONSTANT)
    Q_PROPERTY(QString lastPress READ lastPress NOTIFY lastPressChanged)

public:
    explicit VirtualRemote(QObject* parent = nullptr);

    // button: "ready", "start", "stop", "reset" or "abort"
    Q_INVOKABLE void press(const QString& button);

    QString host() const { return host_.toString(); }
    QString lastPress() const { return lastPress_; }

signals:
    void lastPressChanged();

private:
    QUdpSocket   socket_;
    QHostAddress host_;
    QString      lastPress_;
};
