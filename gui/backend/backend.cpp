// backend.cpp  —  firmware status -> QML properties (see backend.hpp)
//
// Steps:
//   1. constructor: bind the socket, hook up readyRead + the timers
//   2. read_packets(): keep the latest RaceStatus, mark connected
//   3. on_timeout(): no packet for 1 s -> not connected
//   4. getters: turn last_ into Qt types (state name, "DNF", lane lists)
//   5. check_network(): this board's IP address
//   6. wire-up: main.cpp, CMakeLists.txt + gui.pro, then the screens
//
// Needs the firmware side first: something in run_race() that fills a protocol::RaceStatus and
// sends it to protocol::kGuiPort. Until then connected stays false and the getters return placeholders.
#include "backend.hpp"
#include <QDebug>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <cstring>
#include <iterator>

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
        std::memcpy(&last_, data.constData(), sizeof(last_));
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
// TODO: RaceState names have to match the enum order in state_machine.cpp. Better: move the
//       enum (and RACE_DNF) into protocol.h so the firmware and GUI share one copy.
QString Backend::raceState() const {
    if (!connected_) return QStringLiteral("Not connected");
    static const char* NAMES[] = { "Idle", "Ready", "Sequence", "Running",
                                   "False start", "Finished", "Reset", "Reset race" };
    if (last_.state < std::size(NAMES)) return QString::fromLatin1(NAMES[last_.state]);
    return QStringLiteral("Unknown");
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

// seconds with 3 decimals; RACE_DNF (-1.0, Timer.hpp) -> "DNF"; nothing yet -> "--.---"
QVariantList Backend::raceTimes() const {
    QVariantList out;
    for (double ms : last_.race_ms) {
        if (!connected_)  out << QStringLiteral("--.---");
        else if (ms < 0)  out << QStringLiteral("DNF");
        else              out << QString::number(ms / 1000.0, 'f', 3);
    }
    return out;
}

QVariantList Backend::startMs() const {
    return { last_.start_ms[0], last_.start_ms[1] };
}

QVariantList Backend::falseStarts() const {
    return { int(last_.false_starts[0]), int(last_.false_starts[1]) };
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

// ---------- 6. wire-up ----------
// TODO(6):
//   main.cpp:        #include "backend/backend.hpp"
//                    Backend backend;   (before the engine, like virtualRemote)
//                    engine.rootContext()->setContextProperty("backend", &backend);
//   CMakeLists.txt:  add backend/backend.cpp + backend/backend.hpp to qt_add_executable
//   gui.pro:         same, under SOURCES / HEADERS
//   screens:         RaceScreen   -> backend.clockMs, backend.raceTimes (replace the fake stopwatch)
//                    SensorScreen -> backend.aligned, backend.beamBroken (replace the ListModel)
//                    MainScreen + SideBar footer -> backend.connected, backend.raceState, backend.ipAddress
