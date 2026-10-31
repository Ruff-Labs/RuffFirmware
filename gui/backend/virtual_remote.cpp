#include "virtual_remote.hpp"
#include <common/protocol.h>
#include <QDebug>
#include <QTime>

VirtualRemote::VirtualRemote(QObject* parent) : QObject(parent) {
    const QByteArray env = qgetenv("RUFF_BOARD_HOST");
    host_ = QHostAddress(env.isEmpty() ? QStringLiteral("127.0.0.1") : QString::fromUtf8(env));
    if (host_.isNull()) {
        qWarning() << "virtual remote: bad RUFF_BOARD_HOST" << env << "- using 127.0.0.1";
        host_ = QHostAddress::LocalHost;
    }
}

void VirtualRemote::press(const QString& button) {
    using protocol::RemoteButton;
    RemoteButton b;
    if      (button == "ready") b = RemoteButton::Ready;
    else if (button == "start") b = RemoteButton::Start;
    else if (button == "stop")  b = RemoteButton::Stop;
    else if (button == "reset") b = RemoteButton::Reset;
    else if (button == "abort") b = RemoteButton::Abort;
    else {
        qWarning() << "virtual remote: unknown button" << button;
        return;
    }

    const char byte = static_cast<char>(b);
    const qint64 sent = socket_.writeDatagram(&byte, 1, host_, protocol::kRemotePort);
    if (sent != 1)
        qWarning() << "virtual remote: send failed:" << socket_.errorString();

    // UDP has no reply, so this only says the GUI SENT it, not that the firmware got it
    lastPress_ = button.toUpper() + "  " + QTime::currentTime().toString("hh:mm:ss");
    emit lastPressChanged();
}
