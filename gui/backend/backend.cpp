// backend.cpp  —  firmware status -> QML properties (see backend.hpp)
//
// The firmware's StatusSender sends protocol::RaceStatus; read_packets() keeps the newest one,
// the getters turn it into Qt types, and on_timeout() marks the link down after 1 s of silence.
#include "backend.hpp"
#include <QDebug>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <cstring>

constexpr int TIMEOUT_MS = 1000;   // no packet this long = firmware not running
constexpr int NETWORK_MS = 5000;   // how often to re-check the IP address

// ---------- 1. constructor ----------
Backend::Backend(QObject* parent) : QObject(parent) {
    // listen for the firmware's packets (any address, so a laptop GUI works against the board too)
    if (!socket_.bind(QHostAddress::Any, protocol::kGuiPort))
        qWarning() << "backend: bind to port" << protocol::kGuiPort << "failed:" << socket_.errorString();
    connect(&socket_, &QUdpSocket::readyRead, this, &Backend::read_packets);

    // timeout: single-shot, every packet restarts it
    timeout_.setSingleShot(true);
    timeout_.setInterval(TIMEOUT_MS);
    connect(&timeout_, &QTimer::timeout, this, &Backend::on_timeout);

    // network: check now so the IP shows at startup, then every few seconds
    network_timer_.setInterval(NETWORK_MS);
    connect(&network_timer_, &QTimer::timeout, this, &Backend::check_network);
    network_timer_.start();
    check_network();
}

// ---------- 2. read packets ----------
void Backend::read_packets() {
    bool got = false;
    while (socket_.hasPendingDatagrams()) {   // read everything waiting; only the newest matters
        const QByteArray data = socket_.receiveDatagram().data();
        if (data.size() != qsizetype(sizeof(protocol::RaceStatus))) continue;   // wrong/old format, skip
        protocol::RaceStatus s;
        std::memcpy(&s, data.constData(), sizeof s);
        if (s.magic != protocol::kStatusMagic || s.version != protocol::kProtocolVersion) {
            if (!warned_version_) {
                qWarning() << "backend: firmware protocol version" << s.version << "but this GUI speaks"
                           << protocol::kProtocolVersion << "- rebuild both from the same commit";
                warned_version_ = true;
            }
            continue;
        }
        last_ = s;
        got = true;
    }
    if (!got) return;

    connected_ = true;
    timeout_.start();   // restart the 1 s countdown
    emit statusChanged();
}

// ---------- 3. timeout ----------
// Firmware stopped sending (crashed, restarting, not started). Screens show "Firmware not running"
// when backend.connected is false, instead of stale data.
void Backend::on_timeout() {
    connected_ = false;
    emit statusChanged();
}

// ---------- 4. getters ----------
// State names come from protocol.h, the same enum the firmware uses.
QString Backend::raceState() const {
    if (!connected_) return QStringLiteral("Not connected");
    return QString::fromLatin1(protocol::race_state_name(protocol::RaceState(last_.state)));
}

bool Backend::raceActive() const {
    using protocol::RaceState;
    const auto s = RaceState(last_.state);
    return connected_ && (s == RaceState::Sequence || s == RaceState::Running || s == RaceState::FalseStart);
}

bool Backend::judgeReady() const {
    return last_.judge_ready != 0;
}

// only updates ~20x a second; if the clock looks jumpy, RaceScreen can count up between packets
double Backend::clockMs() const {
    return last_.clock_ms;
}

int Backend::winner() const {
    return connected_ ? last_.winner : -1;   // all-zero last_ would read as "lane 1 won"
}

QVariantList Backend::aligned() const {
    return { last_.aligned[0] != 0, last_.aligned[1] != 0 };
}

QVariantList Backend::beamBroken() const {
    return { last_.beam_broken[0] != 0, last_.beam_broken[1] != 0 };
}

QVariantList Backend::splits() const {
    return { int(last_.n_splits[0]), int(last_.n_splits[1]) };
}

// seconds with 3 decimals once the judge stopped the clock; "DNF" for a lane under
// MIN_EXCHANGES passes; "--.---" while there's no result (not connected, waiting, running)
QVariantList Backend::raceTimes() const {
    using protocol::LaneStatus;
    QVariantList out;
    for (int l = 0; l < protocol::kNumLanes; ++l) {
        const auto status = LaneStatus(last_.lane_status[l]);
        if (connected_ && status == LaneStatus::Finished)
            out << QString::number(last_.race_ms[l] / 1000.0, 'f', 3);
        else if (connected_ && status == LaneStatus::DNF)
            out << QStringLiteral("DNF");
        else
            out << QStringLiteral("--.---");
    }
    return out;
}

QVariantList Backend::startMs() const {
    QVariantList out;
    for (int l = 0; l < protocol::kNumLanes; ++l)
        out << (connected_ && last_.has_start[l] ? QVariant(last_.start_ms[l]) : QVariant());
    return out;
}

QVariantList Backend::falseStarts() const {
    return { int(last_.false_starts[0]), int(last_.false_starts[1]) };
}

QVariantList Backend::laneNames() const {
    QVariantList out;
    for (int l = 0; l < protocol::kNumLanes; ++l) {
        char name[protocol::kLaneNameLen];
        std::memcpy(name, last_.setup.lane_names[l], sizeof name);
        name[sizeof name - 1] = '\0';
        const QString s = QString::fromUtf8(name);
        out << (connected_ && !s.isEmpty() ? s : QStringLiteral("Lane %1").arg(l + 1));
    }
    return out;
}

int Backend::heats() const {
    return connected_ ? last_.setup.heats : 0;
}

// ---------- 5. network ----------
// first non-loopback IPv4 address = this board's IP ("" if none)
void Backend::check_network() {
    QString ip;
    for (const QHostAddress& a : QNetworkInterface::allAddresses()) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback()) {
            ip = a.toString();
            break;
        }
    }
    if (ip != ip_) {
        ip_ = ip;
        emit networkChanged();
    }
}
