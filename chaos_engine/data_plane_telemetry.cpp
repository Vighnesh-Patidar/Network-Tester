#include "data_plane_telemetry.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef HAVE_LIBPCAP
#include <pcap/pcap.h>
#endif

namespace nch {

namespace {

double now_ms() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch())
        .count();
}

}  // namespace

DataPlaneTelemetry::DataPlaneTelemetry(Config config) : config_(std::move(config)) {}

DataPlaneTelemetry::~DataPlaneTelemetry() { stop(); }

bool DataPlaneTelemetry::using_pcap() const { return pcap_active_.load(); }

void DataPlaneTelemetry::record_arrival(double ts_ms, std::size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    arrivals_ms_.push_back(ts_ms);
    bytes_received_ += bytes;
}

void DataPlaneTelemetry::mark_fault() {
    std::lock_guard<std::mutex> lock(mutex_);
    fault_ms_ = now_ms() - start_ms_;
}

bool DataPlaneTelemetry::start() {
    if (running_.exchange(true)) {
        return false;  // already running
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        arrivals_ms_.clear();
        probes_sent_ = 0;
        bytes_received_ = 0;
        fault_ms_ = -1.0;
        start_ms_ = now_ms();
    }

#ifdef HAVE_LIBPCAP
    if (!config_.capture_iface.empty()) {
        capture_thread_ = std::thread(&DataPlaneTelemetry::capture_loop_pcap, this);
    } else {
        capture_thread_ = std::thread(&DataPlaneTelemetry::capture_loop_socket, this);
    }
#else
    capture_thread_ = std::thread(&DataPlaneTelemetry::capture_loop_socket, this);
#endif

    sender_thread_ = std::thread(&DataPlaneTelemetry::sender_loop, this);
    return true;
}

void DataPlaneTelemetry::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (sender_thread_.joinable()) {
        sender_thread_.join();
    }
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
}

void DataPlaneTelemetry::sender_loop() {
    const int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return;
    }

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(config_.port);
    if (::inet_pton(AF_INET, config_.target_ip.c_str(), &dest.sin_addr) != 1) {
        ::close(sock);
        return;
    }

    std::vector<char> payload(config_.payload_bytes, 0);
    std::uint64_t seq = 0;
    while (running_.load()) {
        std::memcpy(payload.data(), &seq, sizeof(seq));
        const ssize_t n = ::sendto(sock, payload.data(), payload.size(), 0,
                                   reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
        if (n > 0) {
            std::lock_guard<std::mutex> lock(mutex_);
            ++probes_sent_;
        }
        ++seq;
        std::this_thread::sleep_for(
            std::chrono::milliseconds(config_.probe_interval_ms));
    }
    ::close(sock);
}

void DataPlaneTelemetry::capture_loop_socket() {
    const int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return;
    }

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(config_.port);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(sock, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) != 0) {
        ::close(sock);
        return;
    }

    // A short receive timeout lets the loop observe running_ going false rather
    // than blocking forever in recvfrom after stop().
    timeval tv{};
    tv.tv_sec = 0;
    tv.tv_usec = 200 * 1000;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::vector<char> buffer(std::max<std::uint32_t>(config_.payload_bytes, 2048));
    while (running_.load()) {
        const ssize_t n = ::recv(sock, buffer.data(), buffer.size(), 0);
        if (n > 0) {
            record_arrival(now_ms() - start_ms_, static_cast<std::size_t>(n));
        }
    }
    ::close(sock);
}

void DataPlaneTelemetry::capture_loop_pcap() {
#ifdef HAVE_LIBPCAP
    char errbuf[PCAP_ERRBUF_SIZE] = {0};
    pcap_t* handle =
        pcap_open_live(config_.capture_iface.c_str(), 65535, 1, 200, errbuf);
    if (handle == nullptr) {
        // Fall back to the socket receiver so telemetry still produces numbers.
        capture_loop_socket();
        return;
    }

    std::string filter = "udp";
    if (config_.port != 0) {
        filter += " and dst port " + std::to_string(config_.port);
    }
    bpf_program program{};
    if (pcap_compile(handle, &program, filter.c_str(), 1, PCAP_NETMASK_UNKNOWN) == 0) {
        pcap_setfilter(handle, &program);
        pcap_freecode(&program);
    }

    pcap_active_.store(true);

    while (running_.load()) {
        pcap_pkthdr* header = nullptr;
        const u_char* data = nullptr;
        const int rc = pcap_next_ex(handle, &header, &data);
        if (rc == 1 && header != nullptr) {
            record_arrival(now_ms() - start_ms_, header->caplen);
        } else if (rc == PCAP_ERROR || rc == PCAP_ERROR_BREAK) {
            break;
        }
        // rc == 0 is a timeout: loop and re-check running_.
    }

    pcap_close(handle);
    pcap_active_.store(false);
#endif
}

DataPlaneStats DataPlaneTelemetry::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    DataPlaneStats result;
    result.probes_sent = probes_sent_;
    result.probes_received = arrivals_ms_.size();
    result.used_pcap = pcap_active_.load();

    std::vector<double> arrivals = arrivals_ms_;
    std::sort(arrivals.begin(), arrivals.end());

    double max_gap = 0.0;
    double fault_gap = -1.0;
    for (std::size_t i = 1; i < arrivals.size(); ++i) {
        const double gap = arrivals[i] - arrivals[i - 1];
        max_gap = std::max(max_gap, gap);
        // The reconvergence number is the gap that straddles the fault instant.
        if (fault_ms_ >= 0.0 && arrivals[i - 1] <= fault_ms_ && arrivals[i] >= fault_ms_) {
            fault_gap = gap;
        }
    }
    result.max_gap_ms = max_gap;
    result.reconvergence_ms = fault_gap >= 0.0 ? fault_gap : (fault_ms_ >= 0.0 ? max_gap : -1.0);

    if (!arrivals.empty()) {
        const double span_ms = arrivals.back() - arrivals.front();
        if (span_ms > 0.0) {
            const double bits = static_cast<double>(bytes_received_) * 8.0;
            result.achieved_throughput_mbps = bits / (span_ms / 1000.0) / 1e6;
        }
    }
    return result;
}

}  // namespace nch
