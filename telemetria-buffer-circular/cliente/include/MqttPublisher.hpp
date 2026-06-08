#pragma once
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <cstdint>

// ============================================================================
// MqttPublisher — cliente MQTT 3.1.1 minimo sobre TCP (autossuficiente).
// ----------------------------------------------------------------------------
// Adaptacao de ambiente: o plano sugere Eclipse Paho via vcpkg. Para manter o
// projeto compilavel/executavel SEM dependencias externas (nem vcpkg/CMake),
// implementamos um publicador MQTT minimo direto em sockets (Winsock no
// Windows, BSD sockets no POSIX). O contrato didatico e preservado:
//
//   publishSync  -> BLOQUEIA: aplica o atraso de rede e (QoS1) aguarda o PUBACK.
//                   Usado pela Vertente 1 (a amostragem para durante a rede).
//   publishAsync -> NAO bloqueia: enfileira; uma thread de fundo envia (QoS0).
//                   Usado pela Vertente 2 (o buffer absorve a latencia).
//
// Se o broker nao estiver acessivel, o objeto continua funcionando (connected()
// == false): o atraso de rede ainda e aplicado para a comparacao, mas o envio
// pelo socket vira no-op. Assim os benchmarks rodam mesmo sem Docker no ar.
// ============================================================================
class MqttPublisher {
public:
    MqttPublisher(const std::string& brokerUri, const std::string& clientId,
                  int netDelayMs = 0);
    ~MqttPublisher();

    bool connected() const { return connected_.load(); }

    void publishSync (const std::string& topic, const std::string& payload);
    void publishAsync(const std::string& topic, const std::string& payload);

    void     flush();                       // espera a fila async esvaziar
    uint64_t sent() const { return sent_.load(); }

private:
    struct Msg { std::string topic, payload; };

    void senderLoop();
    bool doConnect();
    void doSend(const std::string& topic, const std::string& payload, bool qos1);
    void closeSock();

    std::string host_;
    int         port_ = 1883;
    std::string clientId_;
    int         netDelayMs_ = 0;

    std::atomic<bool>     connected_{false};
    std::atomic<uint64_t> sent_{0};
    std::atomic<size_t>   pending_{0};
    long long             sock_ = -1;        // socket nativo (cast no .cpp)
    uint16_t              pid_  = 1;          // packet id p/ QoS1

    std::thread             worker_;
    std::mutex              mtx_;
    std::condition_variable cv_;
    std::queue<Msg>         q_;
    bool                    stop_ = false;
};
