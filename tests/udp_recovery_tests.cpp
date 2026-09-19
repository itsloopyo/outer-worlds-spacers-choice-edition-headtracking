// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// What happens when the player launches this game with the previous one still
// running, and then closes it.
//
// The port is held, the mod's bind fails, and the only thing that ever gets
// tracking working again is the receiver's own retry - so this suite runs real
// sockets and real threads and MEASURES the recovery rather than reading the
// code. It holds the port with a second socket, starts the receiver against it,
// releases the port at a phase the retry timer has not been told about, and
// times the gap to a bound socket and to the first pose reaching the game
// thread. A live tracker sends throughout, as one would be while the user is
// alt-tabbing between two games.
//
// The bind-failure line is checked for the OS's own words, because a line that
// names a cause the OS never gave sends the user hunting for an app that is not
// running.

#include "cameraunlock/protocol/udp_receiver.h"
#include "test_harness.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace {

using Clock = std::chrono::steady_clock;

// Away from 4242 so a real OpenTrack or a running game on this machine cannot
// take part in the measurement, and derived from the process id so two runs -
// a second checkout, or a sibling mod's suite on the same machine - cannot bind
// the same port and end up feeding each other's receiver.
// Shifted before the modulus: Windows process ids are always multiples of 4, so
// taking the id itself would use one value in four and give two concurrent runs
// a 1-in-500 collision rather than 1-in-2000.
const uint16_t kTestPort =
    static_cast<uint16_t>(45000 + ((::GetCurrentProcessId() >> 2) % 2000));

double MsSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

/// The previous game's mod: a plain UDP socket sitting on the tracker port.
class PortHog {
public:
    bool Hold(uint16_t port) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
        m_wsa = true;
        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket == INVALID_SOCKET) return false;
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
            return false;
        }
        return true;
    }

    void Release() {
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
        if (m_wsa) {
            WSACleanup();
            m_wsa = false;
        }
    }

    ~PortHog() { Release(); }

private:
    SOCKET m_socket{INVALID_SOCKET};
    bool m_wsa{false};
};

/// A tracker app sending OpenTrack pose packets for the whole run. The yaw walks
/// so no packet is a bit-identical repeat of the one before it, which is what
/// the receiver's freeze gate looks for.
class TrackerSender {
public:
    void Start(uint16_t port) {
        m_stop.store(false);
        m_thread = std::thread([this, port] {
            WSADATA wsa;
            WSAStartup(MAKEWORD(2, 2), &wsa);
            SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sockaddr_in dest;
            std::memset(&dest, 0, sizeof(dest));
            dest.sin_family = AF_INET;
            dest.sin_port = htons(port);
            inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);

            double yaw = 0.0;
            while (!m_stop.load(std::memory_order_acquire)) {
                yaw += 0.05;
                if (yaw > 2.0) yaw = -2.0;
                const double packet[6] = {0.0, 0.0, 0.0, yaw, 0.25, -0.25};
                sendto(s, reinterpret_cast<const char*>(packet), sizeof(packet), 0,
                       reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
                m_sent.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }
            closesocket(s);
            WSACleanup();
        });
    }

    void Stop() {
        m_stop.store(true, std::memory_order_release);
        if (m_thread.joinable()) m_thread.join();
    }

    unsigned long long Sent() const {
        return static_cast<unsigned long long>(m_sent.load(std::memory_order_relaxed));
    }

private:
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<unsigned long long> m_sent{0};
};

/// Timestamped copy of everything the receiver logged.
class LogCapture {
public:
    void Attach(cameraunlock::UdpReceiver& receiver) {
        m_start = Clock::now();
        receiver.SetLog([this](const std::string& line) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lines.push_back(std::make_pair(MsSince(m_start), line));
        });
    }

    std::vector<std::pair<double, std::string> > Lines() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lines;
    }

    bool Any(const char* needle) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < m_lines.size(); ++i) {
            if (m_lines[i].second.find(needle) != std::string::npos) return true;
        }
        return false;
    }

    void Print(const char* label) const {
        const std::vector<std::pair<double, std::string> > lines = Lines();
        for (size_t i = 0; i < lines.size(); ++i) {
            std::printf("    [%s +%7.1fms] %s\n", label, lines[i].first,
                        lines[i].second.c_str());
        }
    }

private:
    mutable std::mutex m_mutex;
    Clock::time_point m_start;
    std::vector<std::pair<double, std::string> > m_lines;
};

/// Spins rather than sleeps: the Windows sleep granularity is coarser than the
/// numbers being measured here.
template <typename Pred>
double WaitFor(Pred pred, double timeoutMs) {
    const Clock::time_point start = Clock::now();
    for (;;) {
        if (pred()) return MsSince(start);
        if (MsSince(start) > timeoutMs) return -1.0;
        std::this_thread::yield();
    }
}

// The bind fails while the port is held, and the line the user reads says what
// the OS said about it.
void TestBindFailureNamesTheRealCause() {
    PortHog hog;
    // Returns rather than carrying on: with nothing holding the port the bind
    // below SUCCEEDS, and every assertion about the failure line would then be
    // checking a message that was never written.
    if (!hog.Hold(kTestPort)) {
        CHECK_MSG(false, "test harness can hold the tracker port");
        return;
    }

    cameraunlock::UdpReceiver receiver;
    LogCapture log;
    log.Attach(receiver);

    const bool started = receiver.Start(kTestPort);
    CHECK_MSG(!started, "Start reports failure while another socket holds the port");
    CHECK(receiver.IsFailed());
    CHECK(receiver.IsRetrying());
    CHECK(!receiver.IsRunning());

    log.Print("bind");
    const std::string wantPort = "Failed to bind UDP port " + std::to_string(kTestPort);
    CHECK_MSG(log.Any(wantPort.c_str()), "the failure names the port");
    // WSAEADDRINUSE. The number and the text both come from the OS; neither is a
    // guess this code makes about which program is holding the port.
    CHECK_MSG(log.Any("error 10048"), "the failure carries the OS error code");
    CHECK_MSG(log.Any("Only one usage of each socket address"),
              "the failure carries the OS message text");
    CHECK_MSG(!log.Any("another program"), "the failure does not invent a cause");
    CHECK_MSG(!log.Any("another game"), "the failure does not invent a cause");
    CHECK_MSG(log.Any("retrying every 500ms"), "the failure tells the user it will retry");

    receiver.Stop();
    hog.Release();
}

// The measurement that matters: with the port held and a tracker sending, how
// long after the other game exits does the view start moving.
void TestRecoveryLatencyAcrossRetryPhases() {
    TrackerSender sender;
    sender.Start(kTestPort);

    // Dwells chosen to land the release at spread-out phases of the 500ms retry
    // timer, so no trial is quietly measuring the same moment twice.
    const int dwellsMs[] = {120, 337, 555, 781, 1013};
    double worstBind = 0.0;
    double worstPose = 0.0;

    for (int trial = 0; trial < 5; ++trial) {
        PortHog hog;
        // Aborts the trial rather than recording a failure and carrying on.
        // CHECK does not stop, so a failed hold used to leave every timing
        // assertion below measuring a run in which the port was never held -
        // they all pass, having exercised no retry at all, and the printed
        // latency table records numbers that mean nothing.
        if (!hog.Hold(kTestPort)) {
            CHECK_MSG(false, "the port is held for this trial");
            continue;
        }

        cameraunlock::UdpReceiver receiver;
        LogCapture log;
        log.Attach(receiver);
        CHECK_MSG(!receiver.Start(kTestPort), "bind fails while the port is held");

        std::this_thread::sleep_for(std::chrono::milliseconds(dwellsMs[trial]));
        CHECK_MSG(!receiver.IsRunning(), "still not listening while the port is held");

        // The other game exits. Both latencies below are measured from THIS
        // instant, because it is the only one the player experiences - a pose
        // latency timed from the bind instead reports single-digit
        // milliseconds and hides the retry wait that dominates the gap they
        // actually sit through.
        const Clock::time_point released = Clock::now();
        hog.Release();
        CHECK_MSG(WaitFor([&] { return receiver.IsRunning(); }, 5000.0) >= 0.0,
                  "the socket binds once the port frees");
        const double bindMs = MsSince(released);
        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
        CHECK_MSG(
            WaitFor([&] { return receiver.GetRotation(yaw, pitch, roll); }, 5000.0) >= 0.0,
            "a tracker pose reaches the game thread after recovery");
        const double poseMs = MsSince(released);

        std::printf("  trial %d: held %4dms, bound %6.1fms after release, "
                    "first pose %6.1fms after release\n",
                    trial + 1, dwellsMs[trial], bindMs, poseMs);
        log.Print("trial");

        // One retry interval plus one supervisor tick is the worst the timer can
        // impose; the rest is slack for a loaded machine.
        CHECK_MSG(bindMs < 900.0, "recovery lands within one retry interval plus a tick");
        CHECK_MSG(poseMs < 1000.0, "tracking is live within a second of the port freeing");
        CHECK_MSG(receiver.IsReceiving(), "the receiver reports live data after recovery");
        CHECK_MSG(!receiver.IsFailed(), "the failure flag clears on a successful retry");
        CHECK_MSG(!receiver.IsRetrying(), "the retry state clears on a successful retry");
        CHECK_MSG(log.Any("tracking is live"), "the recovery line tells the user it worked");

        if (bindMs > worstBind) worstBind = bindMs;
        if (poseMs > worstPose) worstPose = poseMs;

        receiver.Stop();
    }

    sender.Stop();
    std::printf("  worst bind latency %.1fms, worst pose latency %.1fms, %llu packets sent\n",
                worstBind, worstPose, sender.Sent());
}

// The reverse order: the mod is listening first and the OTHER app takes the port
// later. Nothing can steal a bound UDP socket while SO_REUSEADDR stays off, so
// this holds that property - it is what keeps the retry loop meaningful.
void TestBoundReceiverIsNotDisturbedByALaterBinder() {
    TrackerSender sender;
    sender.Start(kTestPort);

    cameraunlock::UdpReceiver receiver;
    LogCapture log;
    log.Attach(receiver);
    CHECK_MSG(receiver.Start(kTestPort), "binds when the port is free");

    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    CHECK_MSG(WaitFor([&] { return receiver.GetRotation(yaw, pitch, roll); }, 2000.0) >= 0.0,
              "poses arrive on the freshly bound socket");

    PortHog latecomer;
    const bool stole = latecomer.Hold(kTestPort);
    CHECK_MSG(!stole, "a second binder cannot take the port from us");
    latecomer.Release();

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK_MSG(receiver.IsRunning(), "the receiver is still listening");
    CHECK_MSG(receiver.IsReceiving(), "poses are still arriving");
    log.Print("bound");

    receiver.Stop();
    sender.Stop();
}

}  // namespace

int main() {
    std::printf("udp recovery: bind failure reporting\n");
    TestBindFailureNamesTheRealCause();
    std::printf("udp recovery: latency from the port freeing to live tracking\n");
    TestRecoveryLatencyAcrossRetryPhases();
    std::printf("udp recovery: a bound receiver is undisturbed\n");
    TestBoundReceiverIsNotDisturbedByALaterBinder();
    return tow_test::Report();
}
