#pragma once
#include <common/protocol.h>
#include <netinet/in.h>
#include <vector>

// Sends protocol::RaceStatus to the GUI (UDP, protocol::kGuiPort). The GUI's backend
// (gui/backend/backend.cpp) turns it into the properties the screens bind to.
//
// Always sends to 127.0.0.1 (GUI on the same board). To watch the board from a GUI on a
// laptop, also set GUI_HOST=<laptop-ip> in /etc/ruff/firmware.env.
// UDP, so sending never waits on the GUI, and a GUI that isn't running costs nothing.
class StatusSender {
public:
    StatusSender() = default;
    ~StatusSender();
    StatusSender(const StatusSender&) = delete;
    StatusSender& operator=(const StatusSender&) = delete;

    bool begin();                                   // open the socket; false = GUI gets no status
    void send(const protocol::RaceStatus& status);  // non-blocking

private:
    int fd_ = -1;
    std::vector<sockaddr_in> dest_;
    int last_errno_ = 0;                            // log each kind of send error once, not 20x a second
};
