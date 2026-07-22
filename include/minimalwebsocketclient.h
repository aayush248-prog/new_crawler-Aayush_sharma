#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <cstdint>

// =========================
// Minimal WebSocket client
// (raw sockets, RFC6455 framing — no Boost, no websocketpp)
// =========================

class MinimalWebSocket
{

private:

    int sockfd = -1;

    std::atomic<bool> running{false};

    static bool readAll(int fd, void* buf, size_t len);

    static bool parseWsUrl(
        const std::string& url,
        std::string& host,
        int& port,
        std::string& path
    );

    bool doHandshake(
        const std::string& host,
        int port,
        const std::string& path
    );

    bool readFrame(uint8_t& opcode, bool& fin, std::string& payload);

    void writeFrame(uint8_t opcode, const std::string& payload);

public:

    std::function<void()> onOpen;
    std::function<void(const std::string&)> onMessage;

    std::thread readerThread;

    bool connect(const std::string& url);

    void readLoop();

    void send(const std::string& data);

    void close();

    ~MinimalWebSocket();

};

#include "MinimalWebSocket.tpp"