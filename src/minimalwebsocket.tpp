#include <iostream>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "Base64.h"

inline bool MinimalWebSocket::readAll(int fd, void* buf, size_t len)
{
    uint8_t* p = (uint8_t*)buf;
    size_t got = 0;

    while (got < len)
    {
        ssize_t n = ::read(fd, p + got, len - got);

        if (n <= 0)
            return false;

        got += (size_t)n;
    }

    return true;
}

inline bool MinimalWebSocket::parseWsUrl(
    const std::string& url,
    std::string& host,
    int& port,
    std::string& path
)
{
    std::string prefix = "ws://";

    if (url.compare(0, prefix.size(), prefix) != 0)
        return false;

    std::string rest = url.substr(prefix.size());

    size_t slashPos = rest.find('/');

    std::string hostPort =
        (slashPos == std::string::npos) ? rest : rest.substr(0, slashPos);

    path =
        (slashPos == std::string::npos) ? "/" : rest.substr(slashPos);

    size_t colonPos = hostPort.find(':');

    if (colonPos == std::string::npos)
    {
        host = hostPort;
        port = 80;
    }
    else
    {
        host = hostPort.substr(0, colonPos);
        port = std::stoi(hostPort.substr(colonPos + 1));
    }

    return true;
}

inline bool MinimalWebSocket::doHandshake(
    const std::string& host,
    int port,
    const std::string& path
)
{
    uint8_t keyBytes[16];

    for (int i = 0; i < 16; i++)
        keyBytes[i] = (uint8_t)(rand() % 256);

    std::string secKey = base64_encode(keyBytes, 16);

    std::ostringstream req;

    req << "GET " << path << " HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << secKey << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n"
        << "\r\n";

    std::string reqStr = req.str();

    if (::write(sockfd, reqStr.data(), reqStr.size()) < 0)
        return false;

    // Read response headers until the blank line
    std::string resp;
    char c;

    while (true)
    {
        ssize_t n = ::read(sockfd, &c, 1);

        if (n <= 0)
            return false;

        resp.push_back(c);

        if (resp.size() >= 4 &&
            resp.compare(resp.size() - 4, 4, "\r\n\r\n") == 0)
            break;
    }

    // Expect HTTP/1.1 101 Switching Protocols
    if (resp.find(" 101 ") == std::string::npos)
    {
        std::cout << "WebSocket handshake failed:\n" << resp << std::endl;
        return false;
    }

    return true;
}

inline bool MinimalWebSocket::readFrame(uint8_t& opcode, bool& fin, std::string& payload)
{
    uint8_t hdr[2];

    if (!readAll(sockfd, hdr, 2))
        return false;

    fin = (hdr[0] & 0x80) != 0;
    opcode = hdr[0] & 0x0F;

    bool masked = (hdr[1] & 0x80) != 0;
    uint64_t len = hdr[1] & 0x7F;

    if (len == 126)
    {
        uint8_t ext[2];

        if (!readAll(sockfd, ext, 2))
            return false;

        len = ((uint64_t)ext[0] << 8) | ext[1];
    }
    else if (len == 127)
    {
        uint8_t ext[8];

        if (!readAll(sockfd, ext, 8))
            return false;

        len = 0;

        for (int i = 0; i < 8; i++)
            len = (len << 8) | ext[i];
    }

    uint8_t maskKey[4] = {0, 0, 0, 0};

    if (masked)
    {
        if (!readAll(sockfd, maskKey, 4))
            return false;
    }

    payload.resize(len);

    if (len > 0)
    {
        if (!readAll(sockfd, &payload[0], len))
            return false;
    }

    if (masked)
    {
        for (uint64_t i = 0; i < len; i++)
            payload[i] = (char)((uint8_t)payload[i] ^ maskKey[i % 4]);
    }

    return true;
}

inline void MinimalWebSocket::writeFrame(uint8_t opcode, const std::string& payload)
{
    std::string frame;

    frame.push_back((char)(0x80 | opcode)); // FIN=1

    uint64_t len = payload.size();
    uint8_t maskBit = 0x80; // client->server frames must be masked

    if (len <= 125)
    {
        frame.push_back((char)(maskBit | len));
    }
    else if (len <= 0xFFFF)
    {
        frame.push_back((char)(maskBit | 126));
        frame.push_back((char)((len >> 8) & 0xFF));
        frame.push_back((char)(len & 0xFF));
    }
    else
    {
        frame.push_back((char)(maskBit | 127));

        for (int i = 7; i >= 0; i--)
            frame.push_back((char)((len >> (8 * i)) & 0xFF));
    }

    uint8_t maskKey[4];

    for (int i = 0; i < 4; i++)
        maskKey[i] = (uint8_t)(rand() % 256);

    frame.append((char*)maskKey, 4);

    std::string maskedPayload = payload;

    for (uint64_t i = 0; i < len; i++)
        maskedPayload[i] =
            (char)((uint8_t)maskedPayload[i] ^ maskKey[i % 4]);

    frame += maskedPayload;

    ::write(sockfd, frame.data(), frame.size());
}

inline bool MinimalWebSocket::connect(const std::string& url)
{
    std::string host, path;
    int port = 80;

    if (!parseWsUrl(url, host, port, path))
    {
        std::cout << "Invalid WebSocket URL: " << url << std::endl;
        return false;
    }

    sockfd = ::socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        std::cout << "Socket creation failed\n";
        return false;
    }

    struct hostent* server = gethostbyname(host.c_str());

    if (!server)
    {
        std::cout << "DNS resolution failed for " << host << std::endl;
        return false;
    }

    struct sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    memcpy(
        &serv_addr.sin_addr.s_addr,
        server->h_addr,
        server->h_length
    );
    serv_addr.sin_port = htons(port);

    if (::connect(
            sockfd,
            (struct sockaddr*)&serv_addr,
            sizeof(serv_addr)
        ) < 0)
    {
        std::cout << "TCP connect failed\n";
        return false;
    }

    if (!doHandshake(host, port, path))
        return false;

    running = true;

    if (onOpen)
        onOpen();

    readerThread = std::thread(&MinimalWebSocket::readLoop, this);

    return true;
}

inline void MinimalWebSocket::readLoop()
{
    std::string messageBuffer;
    bool assembling = false;

    while (running)
    {
        uint8_t opcode;
        bool fin;
        std::string payload;

        if (!readFrame(opcode, fin, payload))
        {
            running = false;
            break;
        }

        if (opcode == 0x8) // close
        {
            running = false;
            break;
        }

        if (opcode == 0x9) // ping -> respond pong
        {
            writeFrame(0xA, payload);
            continue;
        }

        if (opcode == 0xA) // pong
        {
            continue;
        }

        if (opcode == 0x1 || opcode == 0x2) // text/binary start
        {
            messageBuffer = payload;
            assembling = !fin;
        }
        else if (opcode == 0x0) // continuation
        {
            messageBuffer += payload;
            assembling = !fin;
        }

        if (fin && onMessage)
            onMessage(messageBuffer);
    }
}

inline void MinimalWebSocket::send(const std::string& data)
{
    if (running)
        writeFrame(0x1, data);
}

inline void MinimalWebSocket::close()
{
    if (running)
    {
        running = false;

        // best-effort close frame; ignore errors, socket is going away
        writeFrame(0x8, "");
    }

    if (sockfd >= 0)
    {
        ::shutdown(sockfd, SHUT_RDWR);
        ::close(sockfd);
        sockfd = -1;
    }

    if (readerThread.joinable())
        readerThread.join();
}

inline MinimalWebSocket::~MinimalWebSocket()
{
    close();
}
