#include "MqttPublisher.hpp"

#include <cstring>
#include <chrono>
#include <iostream>
#include <vector>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socket_t = SOCKET;
  static const socket_t BAD_SOCK = INVALID_SOCKET;
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  using socket_t = int;
  static const socket_t BAD_SOCK = -1;
#endif

namespace {

#ifdef _WIN32
// Inicializa o Winsock uma unica vez por processo.
struct WsaInit {
    WsaInit()  { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); }
    ~WsaInit() { WSACleanup(); }
};
static WsaInit g_wsa;
#endif

// Codifica o "Remaining Length" do MQTT (esquema de 7 bits + bit de continuacao).
void encodeRemLen(std::vector<uint8_t>& buf, size_t len) {
    do {
        uint8_t b = static_cast<uint8_t>(len % 128);
        len /= 128;
        if (len > 0) b |= 0x80;
        buf.push_back(b);
    } while (len > 0);
}

// Escreve uma string MQTT (2 bytes de tamanho big-endian + bytes).
void putStr(std::vector<uint8_t>& buf, const std::string& s) {
    buf.push_back(static_cast<uint8_t>((s.size() >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>(s.size() & 0xFF));
    buf.insert(buf.end(), s.begin(), s.end());
}

bool sendAll(socket_t s, const uint8_t* data, size_t n) {
    size_t off = 0;
    while (off < n) {
        int sent = ::send(s, reinterpret_cast<const char*>(data + off),
                          static_cast<int>(n - off), 0);
        if (sent <= 0) return false;
        off += static_cast<size_t>(sent);
    }
    return true;
}

bool recvAll(socket_t s, uint8_t* data, size_t n) {
    size_t off = 0;
    while (off < n) {
        int got = ::recv(s, reinterpret_cast<char*>(data + off),
                         static_cast<int>(n - off), 0);
        if (got <= 0) return false;
        off += static_cast<size_t>(got);
    }
    return true;
}

} // namespace

MqttPublisher::MqttPublisher(const std::string& brokerUri,
                             const std::string& clientId, int netDelayMs)
    : clientId_(clientId), netDelayMs_(netDelayMs) {
    // Parse de "tcp://host:port"
    std::string uri = brokerUri;
    auto pos = uri.find("://");
    if (pos != std::string::npos) uri = uri.substr(pos + 3);
    auto colon = uri.find(':');
    if (colon != std::string::npos) {
        host_ = uri.substr(0, colon);
        port_ = std::atoi(uri.substr(colon + 1).c_str());
    } else {
        host_ = uri;
    }
    if (host_.empty()) host_ = "localhost";
    if (port_ == 0)    port_ = 1883;

    connected_.store(doConnect());
    if (!connected_.load()) {
        std::cerr << "[MQTT] aviso: nao foi possivel conectar em " << host_ << ":"
                  << port_ << " — seguindo sem publicar (atraso de rede mantido).\n";
    }
    worker_ = std::thread(&MqttPublisher::senderLoop, this);
}

MqttPublisher::~MqttPublisher() {
    flush();
    {
        std::lock_guard<std::mutex> lk(mtx_);
        stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    closeSock();
}

bool MqttPublisher::doConnect() {
    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    std::string portStr = std::to_string(port_);
    if (getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &res) != 0 || !res)
        return false;

    socket_t s = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == BAD_SOCK) { freeaddrinfo(res); return false; }
    if (::connect(s, res->ai_addr, static_cast<int>(res->ai_addrlen)) != 0) {
        freeaddrinfo(res);
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return false;
    }
    freeaddrinfo(res);

    // Timeout de recepcao p/ nao travar esperando CONNACK/PUBACK.
#ifdef _WIN32
    DWORD tv = 2000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&tv), sizeof(tv));
#else
    timeval tv; tv.tv_sec = 2; tv.tv_usec = 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    sock_ = static_cast<long long>(s);

    // CONNECT
    std::vector<uint8_t> vh, payload, pkt;
    putStr(vh, "MQTT");
    vh.push_back(0x04);                 // protocol level 4 (MQTT 3.1.1)
    vh.push_back(0x02);                 // flags: clean session
    vh.push_back(0x00); vh.push_back(0x3C); // keep alive = 60s
    putStr(payload, clientId_);

    pkt.push_back(0x10);               // CONNECT
    std::vector<uint8_t> rl;
    encodeRemLen(rl, vh.size() + payload.size());
    pkt.insert(pkt.end(), rl.begin(), rl.end());
    pkt.insert(pkt.end(), vh.begin(), vh.end());
    pkt.insert(pkt.end(), payload.begin(), payload.end());

    if (!sendAll(s, pkt.data(), pkt.size())) { closeSock(); return false; }

    uint8_t resp[4];
    if (!recvAll(s, resp, 4))          { closeSock(); return false; }
    if (resp[0] != 0x20 || resp[3] != 0x00) { closeSock(); return false; }
    return true;
}

void MqttPublisher::doSend(const std::string& topic, const std::string& payload,
                           bool qos1) {
    if (sock_ < 0) return;
    socket_t s = static_cast<socket_t>(sock_);

    std::vector<uint8_t> vh;
    putStr(vh, topic);
    uint16_t id = 0;
    if (qos1) {
        id = pid_++;
        if (pid_ == 0) pid_ = 1;
        vh.push_back(static_cast<uint8_t>(id >> 8));
        vh.push_back(static_cast<uint8_t>(id & 0xFF));
    }

    std::vector<uint8_t> pkt;
    pkt.push_back(qos1 ? 0x32 : 0x30);   // PUBLISH (QoS1 -> 0x32, QoS0 -> 0x30)
    std::vector<uint8_t> rl;
    encodeRemLen(rl, vh.size() + payload.size());
    pkt.insert(pkt.end(), rl.begin(), rl.end());
    pkt.insert(pkt.end(), vh.begin(), vh.end());
    pkt.insert(pkt.end(), payload.begin(), payload.end());

    if (!sendAll(s, pkt.data(), pkt.size())) {
        connected_.store(false);
        closeSock();
        return;
    }
    sent_.fetch_add(1);

    if (qos1) {
        uint8_t ack[4];
        recvAll(s, ack, 4); // aguarda PUBACK (bloqueante) — ignora o conteudo
    }
}

void MqttPublisher::publishSync(const std::string& topic, const std::string& payload) {
    if (netDelayMs_ > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(netDelayMs_));
    if (connected_.load()) doSend(topic, payload, /*qos1=*/true);
}

void MqttPublisher::publishAsync(const std::string& topic, const std::string& payload) {
    pending_.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(mtx_);
        q_.push(Msg{topic, payload});
    }
    cv_.notify_one();
}

void MqttPublisher::senderLoop() {
    for (;;) {
        Msg m;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            cv_.wait(lk, [&] { return stop_ || !q_.empty(); });
            if (q_.empty()) {
                if (stop_) return;
                continue;
            }
            m = std::move(q_.front());
            q_.pop();
        }
        if (netDelayMs_ > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(netDelayMs_));
        if (connected_.load()) doSend(m.topic, m.payload, /*qos1=*/false);
        pending_.fetch_sub(1);
    }
}

void MqttPublisher::flush() {
    while (pending_.load() > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void MqttPublisher::closeSock() {
    if (sock_ < 0) return;
    socket_t s = static_cast<socket_t>(sock_);
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    sock_ = -1;
}
