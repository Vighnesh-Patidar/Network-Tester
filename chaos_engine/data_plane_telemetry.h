#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nch {

// What the data plane actually did during a test, kept strictly separate from any
// control-plane number (§4.4). reconvergence_ms is the outage observed around the
// injected fault; achieved_throughput_mbps lets the capacity-constrained failover
// case (§6) report real throughput against a link's declared capacity.
struct DataPlaneStats {
    std::uint64_t probes_sent = 0;
    std::uint64_t probes_received = 0;
    double reconvergence_ms = -1.0;
    double max_gap_ms = 0.0;
    double achieved_throughput_mbps = 0.0;
    bool used_pcap = false;
};

// Background telemetry: a sender thread injects UDP probes toward a target while
// a capture thread timestamps their arrival. The preferred capture path is
// libpcap on the wire (built when HAVE_LIBPCAP is defined); a bound-socket
// receiver is used as a portable fallback so the component builds and runs
// without libpcap present.
class DataPlaneTelemetry {
public:
    struct Config {
        std::string capture_iface;          // interface to sniff (pcap backend)
        std::string target_ip = "127.0.0.1";
        std::uint16_t port = 0;             // 0 lets the OS pick the receiver port
        std::uint32_t probe_interval_ms = 10;
        std::uint32_t payload_bytes = 1024;
    };

    explicit DataPlaneTelemetry(Config config);
    ~DataPlaneTelemetry();

    DataPlaneTelemetry(const DataPlaneTelemetry&) = delete;
    DataPlaneTelemetry& operator=(const DataPlaneTelemetry&) = delete;

    bool start();
    void mark_fault();   // call at the instant of fault injection
    void stop();

    DataPlaneStats stats() const;
    bool using_pcap() const;

private:
    Config config_;
    std::atomic<bool> running_{false};
    std::atomic<bool> pcap_active_{false};
    std::thread sender_thread_;
    std::thread capture_thread_;

    mutable std::mutex mutex_;
    std::vector<double> arrivals_ms_;       // steady-clock ms, capture-relative
    std::uint64_t probes_sent_ = 0;
    std::uint64_t bytes_received_ = 0;
    double start_ms_ = 0.0;
    double fault_ms_ = -1.0;

    void sender_loop();
    void capture_loop_pcap();
    void capture_loop_socket();
    void record_arrival(double ts_ms, std::size_t bytes);
};

}  // namespace nch
