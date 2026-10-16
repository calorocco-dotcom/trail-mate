#include "hostlink/c6/c6_protocol.h"
#include "platform/esp/idf_common/wireless_companion/c6_companion.h"
#include "platform/memory/psram_ptr.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <mutex>
#if defined(_WIN32) && !defined(_GLIBCXX_HAS_GTHREADS)
namespace std
{
struct mutex
{
    void lock() {}
    void unlock() {}
};
} // namespace std
#endif
using namespace platform::esp::idf_common::wireless_companion;
constexpr size_t kWifiTcpRxBufferSize = 16384;
struct Tcp : WifiTcpTransport
{
    C6CompanionStatus status_{};
    WifiTcpStatus tcp_status_{};
    uint8_t next_tcp_connection_id_ = 1;
    std::mutex tcp_rx_mutex_{};
    using TcpRxBuffer = std::array<uint8_t, kWifiTcpRxBufferSize>;
    platform::memory::PsramPtr<TcpRxBuffer> tcp_rx_buffer_{};
    size_t tcp_rx_head_ = 0, tcp_rx_tail_ = 0, tcp_rx_size_ = 0;
    bool send_ok = true;
    unsigned sends = 0;
    bool begin() { return false; }
    void poll() {}
    void copy_text(char* out, const char* in) { std::strcpy(out, in); }
    bool send_wifi_tcp_command(const tm_c6_wifi_tcp_header_t&, const uint8_t*, size_t)
    {
        ++sends;
        return send_ok;
    }
    WifiTcpStatus tcpStatus() const override { return tcp_status_; }
#include "c6_tcp_api.inc"
#include "c6_tcp_buffer.inc"
    Tcp()
    {
        status_.started = status_.present = status_.wifi_connected = true;
        status_.supported_features = TM_C6_FEATURE_WIFI_TCP_PROXY;
    }
};
int main()
{
    Tcp tcp;
    assert(live_bytes == 0 && allocations == 0); // Idle Wi-Fi has no TCP payload storage.
    allocation_fails = true;
    assert(!tcp.openTcp("example.org", 443) && tcp.sends == 0 && live_bytes == 0);
    allocation_fails = false;
    tcp.send_ok = false;
    assert(!tcp.openTcp("example.org", 443) && live_bytes == 0);
    tcp.send_ok = true;
    std::array<uint8_t, 12000> data{};
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i);
    std::array<uint8_t, 12000> out{};
    for (unsigned cycle = 0; cycle < 100; ++cycle)
    {
        assert(tcp.openTcp("example.org", 443) && live_bytes == 16384);
        tcp.set_tcp_state(WifiTcpState::Connected, TM_C6_OK, "connected");
        assert(tcp.append_tcp_rx(data.data(), data.size()));
        assert(!tcp.append_tcp_rx(data.data(), data.size())); // Bounded, no growth on overflow.
        assert(tcp.readTcp(out.data(), 10000) == 10000);
        assert(std::equal(out.begin(), out.begin() + 10000, data.begin()));
        assert(tcp.append_tcp_rx(data.data(), data.size())); // Wrap around the ring.
        tcp.set_tcp_state(WifiTcpState::Disconnected, TM_C6_OK, "remote_closed");
        assert(live_bytes == 16384); // Preserve unread bytes after EOF.
        assert(tcp.readTcp(out.data(), 2000) == 2000);
        assert(std::equal(out.begin(), out.begin() + 2000, data.begin() + 10000));
        assert(tcp.readTcp(out.data(), out.size()) == out.size());
        assert(out == data && live_bytes == 0);
        assert(!tcp.append_tcp_rx(data.data(), 1)); // No storage resurrection on stale data.
        assert(tcp.openTcp("example.org", 443));
        assert(tcp.append_tcp_rx(data.data(), 20));
        tcp.set_tcp_state(WifiTcpState::Disconnected, TM_C6_OK, "remote_closed");
        tcp.closeTcp(); // Explicit close discards unread bytes and releases memory.
        assert(live_bytes == 0);
    }
    assert(tcp.openTcp("example.org", 443));
    tcp.reset_tcp_state(WifiTcpState::Disconnected, "wifi_disconnected");
    assert(live_bytes == 0 && tcp.tcp_status_.buffered_bytes == 0);
}
