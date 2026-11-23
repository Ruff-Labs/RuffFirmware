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

// UTF-8 into a fixed char[N], cut to fit with room for the 0, never mid-character
template <size_t N>
static void copy_utf8(char (&dst)[N], const QString& in) {
    QString s = in;
    QByteArray utf8 = s.toUtf8();
    while (utf8.size() > qsizetype(N - 1)) {
        s.chop(1);
        if (!s.isEmpty() && s.back().isHighSurrogate()) s.chop(1);   // don't split an emoji
        utf8 = s.toUtf8();
    }
    std::memset(dst, 0, N);
    std::memcpy(dst, utf8.constData(), size_t(utf8.size()));
}

void VirtualRemote::setupRace(const QString& lane1, const QString& lane2, int heats) {
    protocol::SetupPacket p;
    p.setup.heats = uint8_t(std::clamp(heats, 1, 255));
    copy_utf8(p.setup.lane_names[0], lane1.trimmed());
    copy_utf8(p.setup.lane_names[1], lane2.trimmed());
    send(reinterpret_cast<const char*>(&p), sizeof p, QStringLiteral("SETUP"));
}

static bool is_ipv4(const QString& s) {
    QHostAddress a;
    return a.setAddress(s) && a.protocol() == QAbstractSocket::IPv4Protocol;
}

QString VirtualRemote::setWifi(const QString& ssid, const QString& password,
                               const QString& address, const QString& gateway, const QString& dns) {
    const QString name = ssid.trimmed();
    const QString addr = address.trimmed(), gw = gateway.trimmed(), ns = dns.trimmed();

    if (name.isEmpty())                     return QStringLiteral("Enter the network name");
    if (name.toUtf8().size() > 32)          return QStringLiteral("Network name is too long (32 bytes max)");
    if (!password.isEmpty() && (password.size() < 8 || password.toUtf8().size() > 63))
        return QStringLiteral("Wi-Fi passwords are 8 to 63 characters (leave it blank for an open network)");
    if (!addr.isEmpty()) {
        const QStringList parts = addr.split('/');
        bool ok = true;
        const int prefix = parts.size() == 2 ? parts[1].toInt(&ok) : 24;
        if (parts.size() > 2 || !is_ipv4(parts[0]) || !ok || prefix < 1 || prefix > 32)
            return QStringLiteral("IP address isn't valid (e.g. 192.168.1.50 or 192.168.1.50/24)");
    }
    if (!gw.isEmpty() && !is_ipv4(gw))      return QStringLiteral("Gateway isn't a valid IP address");
    if (!ns.isEmpty() && !is_ipv4(ns))      return QStringLiteral("DNS server isn't a valid IP address");
    if (addr.isEmpty() && !gw.isEmpty())    return QStringLiteral("A gateway needs an IP address too (or leave both blank for automatic)");

    protocol::WifiPacket p;
    copy_utf8(p.wifi.ssid, name);
    copy_utf8(p.wifi.password, password);
    copy_utf8(p.wifi.address, addr);
    copy_utf8(p.wifi.gateway, gw);
    copy_utf8(p.wifi.dns, ns);
    send(reinterpret_cast<const char*>(&p), sizeof p, QStringLiteral("WI-FI"));
    return {};
}

void VirtualRemote::restartSoftware() {
    const char byte = static_cast<char>(protocol::Command::Restart);
    send(&byte, 1, QStringLiteral("RESTART"));
}

void VirtualRemote::send(const char* data, qint64 size, const QString& what) {
    if (socket_.writeDatagram(data, size, host_, protocol::kRemotePort) != size)
        qWarning() << "virtual remote: send failed:" << socket_.errorString();
    lastPress_ = what + "  " + QTime::currentTime().toString("hh:mm:ss");
    emit lastPressChanged();
}
