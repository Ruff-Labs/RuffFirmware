#include "backend.hpp"
#include <QDebug>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <cstring>

using protocol::LaneStatus;
using protocol::RaceState;
using protocol::SensorFault;

constexpr int TIMEOUT_MS = 1000;
constexpr int NETWORK_MS = 5000;

template <size_t N>
static QString text(const char (&s)[N]) {   // a fixed-size char[] from the packet
    return QString::fromUtf8(s, int(strnlen(s, N)));
}

static QVariantList lanes(const uint8_t (&v)[protocol::kNumLanes]) {
    return { v[0] != 0, v[1] != 0 };
}

Backend::Backend(QObject* parent) : QObject(parent) {
    // any address, so a GUI on a laptop can watch the board too (GUI_HOST in firmware.env)
    if (!socket_.bind(QHostAddress::Any, protocol::kGuiPort))
        qWarning() << "backend: bind to port" << protocol::kGuiPort << "failed:" << socket_.errorString();
    connect(&socket_, &QUdpSocket::readyRead, this, &Backend::read_packets);

    timeout_.setSingleShot(true);
    timeout_.setInterval(TIMEOUT_MS);
    connect(&timeout_, &QTimer::timeout, this, &Backend::on_timeout);

    network_timer_.setInterval(NETWORK_MS);
    connect(&network_timer_, &QTimer::timeout, this, &Backend::check_network);
    network_timer_.start();
    check_network();
}

void Backend::read_packets() {
    bool got = false;
    while (socket_.hasPendingDatagrams()) {
        const QByteArray data = socket_.receiveDatagram().data();
        if (data.size() != qsizetype(sizeof(protocol::RaceStatus))) continue;
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

        const auto state = RaceState(s.state);
        if (state == RaceState::Finished) {
            result_     = s;
            has_result_ = true;
        } else if (state == RaceState::Sequence || state == RaceState::ResetRace) {
            has_result_ = false;   // next heat's lights started, or a new race
        }
    }
    if (!got) return;

    connected_ = true;
    timeout_.start();
    emit statusChanged();
}

void Backend::on_timeout() {
    connected_ = false;
    emit statusChanged();
}

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

// ---------- race ----------
QString Backend::firmwareVersion() const {
    return connected_ ? text(last_.firmware_version) : QString();
}

QString Backend::raceState() const {
    if (!connected_) return QStringLiteral("Not connected");
    return QString::fromLatin1(protocol::race_state_name(RaceState(last_.state)));
}

bool Backend::raceActive() const {
    const auto s = RaceState(last_.state);
    return connected_ && (s == RaceState::Sequence || s == RaceState::Running || s == RaceState::FalseStart);
}

bool Backend::judgeReady() const { return last_.judge_ready != 0; }
double Backend::clockMs() const  { return results().clock_ms; }   // frozen at STOP while showingLastResult

// ---------- results ----------
bool Backend::showingLastResult() const {
    return connected_ && has_result_ && RaceState(last_.state) != RaceState::Finished;
}

const protocol::RaceStatus& Backend::results() const {
    return showingLastResult() ? result_ : last_;
}

int Backend::winner() const {
    return connected_ ? results().winner : -1;   // an all-zero packet would read as "lane 1 won"
}

QVariantList Backend::splits() const {
    return { int(results().n_splits[0]), int(results().n_splits[1]) };
}

QVariantList Backend::raceTimes() const {
    QVariantList out;
    const protocol::RaceStatus& r = results();
    for (int l = 0; l < protocol::kNumLanes; ++l) {
        const auto status = LaneStatus(r.lane_status[l]);
        if (connected_ && status == LaneStatus::Finished)
            out << QString::number(r.race_ms[l] / 1000.0, 'f', 3);
        else if (connected_ && status == LaneStatus::DNF)
            out << QStringLiteral("DNF");
        else
            out << QStringLiteral("--.---");
    }
    return out;
}

QVariantList Backend::startMs() const {
    QVariantList out;
    const protocol::RaceStatus& r = results();
    for (int l = 0; l < protocol::kNumLanes; ++l)
        out << (connected_ && r.has_start[l] ? QVariant(r.start_ms[l]) : QVariant());
    return out;
}

QVariantList Backend::falseStarts() const {
    return { int(last_.false_starts[0]), int(last_.false_starts[1]) };
}

// ---------- sensors ----------
QVariantList Backend::laneInUse() const { return connected_ ? lanes(last_.lane_in_use) : QVariantList{ true, true }; }
QVariantList Backend::aligned() const   { return lanes(last_.aligned); }
QVariantList Backend::forward() const   { return lanes(last_.forward); }
QVariantList Backend::back() const      { return lanes(last_.back); }

QVariantList Backend::beamBroken() const {
    return { last_.forward[0] || last_.back[0], last_.forward[1] || last_.back[1] };
}

QVariantList Backend::sensorFaults() const {
    QVariantList out;
    for (int l = 0; l < protocol::kNumLanes; ++l) {
        switch (connected_ ? SensorFault(last_.sensor_fault[l]) : SensorFault::None) {
            case SensorFault::Blocked: out << QStringLiteral("Beam blocked for 10 s+: something in the way, or a stuck sensor"); break;
            case SensorFault::Wiring:  out << QStringLiteral("All three signals on at once: check the sensor wiring"); break;
            default:                   out << QString(); break;
        }
    }
    return out;
}

bool Backend::anySensorFault() const {
    if (!connected_) return false;
    for (uint8_t f : last_.sensor_fault)
        if (SensorFault(f) != SensorFault::None) return true;
    return false;
}

// ---------- setup ----------
QVariantList Backend::laneNames() const {
    QVariantList out;
    for (int l = 0; l < protocol::kNumLanes; ++l) {
        const QString s = text(last_.setup.lane_names[l]);
        out << (connected_ && !s.isEmpty() ? s : QStringLiteral("Lane %1").arg(l + 1));
    }
    return out;
}

int Backend::heats() const { return connected_ ? last_.setup.heats : 0; }

// ---------- Wi-Fi (from the firmware's SystemControl) ----------
QString Backend::wifiSsid() const { return connected_ ? text(last_.wifi_ssid) : QString(); }

QString Backend::wifiState() const {
    if (!connected_) return QStringLiteral("idle");
    switch (protocol::NetOp(last_.net_op)) {
        case protocol::NetOp::Working: return QStringLiteral("working");
        case protocol::NetOp::Done:    return QStringLiteral("done");
        case protocol::NetOp::Failed:  return QStringLiteral("failed");
        default:                       return QStringLiteral("idle");
    }
}

QString Backend::wifiMessage() const { return connected_ ? text(last_.net_message) : QString(); }
