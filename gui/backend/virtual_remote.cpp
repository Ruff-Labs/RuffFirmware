#include "virtual_remote.hpp"
#include <common/protocol.h>
#include <QDebug>
#include <QTime>
#include <algorithm>
#include <cstring>

VirtualRemote::VirtualRemote(QObject* parent) : QObject(parent) {
    const QByteArray env = qgetenv("RUFF_BOARD_HOST");
    host_ = QHostAddress(env.isEmpty() ? QStringLiteral("127.0.0.1") : QString::fromUtf8(env));
    if (host_.isNull()) {
        qWarning() << "virtual remote: bad RUFF_BOARD_HOST" << env << "- using 127.0.0.1";
        host_ = QHostAddress::LocalHost;
    }
}

void VirtualRemote::press(const QString& button) {
    using protocol::Command;
    Command c;
    if      (button == "ready")     c = Command::Ready;
    else if (button == "start")     c = Command::Start;
    else if (button == "stop")      c = Command::Stop;
    else if (button == "reset")     c = Command::Reset;
    else if (button == "abort")     c = Command::Abort;
    else if (button == "resetRace") c = Command::ResetRace;
    else {
        qWarning() << "virtual remote: unknown button" << button;
        return;
    }
    const char byte = static_cast<char>(c);
    send(&byte, 1, button.toUpper());
}

// UTF-8, cut to fit with room for the 0, never in the middle of a character
static void copy_name(char (&dst)[protocol::kLaneNameLen], const QString& name) {
    QString s = name.trimmed();
    QByteArray utf8 = s.toUtf8();
    while (utf8.size() > protocol::kLaneNameLen - 1) {
        s.chop(1);
        if (!s.isEmpty() && s.back().isHighSurrogate()) s.chop(1);   // don't split an emoji
        utf8 = s.toUtf8();
    }
    std::memset(dst, 0, sizeof dst);
    std::memcpy(dst, utf8.constData(), size_t(utf8.size()));
}

void VirtualRemote::setupRace(const QString& lane1, const QString& lane2, int heats) {
    protocol::SetupPacket p;
    p.setup.heats = uint8_t(std::clamp(heats, 1, 255));
    copy_name(p.setup.lane_names[0], lane1);
    copy_name(p.setup.lane_names[1], lane2);
    send(reinterpret_cast<const char*>(&p), sizeof p, QStringLiteral("SETUP"));
}

void VirtualRemote::send(const char* data, qint64 size, const QString& what) {
    const qint64 sent = socket_.writeDatagram(data, size, host_, protocol::kRemotePort);
    if (sent != size)
        qWarning() << "virtual remote: send failed:" << socket_.errorString();

    // UDP has no reply, so this only says the GUI SENT it, not that the firmware got it
    lastPress_ = what + "  " + QTime::currentTime().toString("hh:mm:ss");
    emit lastPressChanged();
}
