#include <iostream>
#include <chrono>
#include <unistd.h>

inline void WebSocketClient::handleMessage(const std::string& payload)
{
    json msg;

    try
    {
        msg = json::parse(payload);
    }
    catch (std::exception& e)
    {
        std::cout << "Failed to parse CDP message: " << e.what() << std::endl;
        return;
    }

    if (msg.contains("id"))
    {
        int id = msg["id"].get<int>();

        {
            std::lock_guard<std::mutex> lock(responseMutex);
            responses[id] = msg;
        }

        responseCV.notify_all();
    }
    else if (msg.contains("method"))
    {
        std::string method = msg["method"].get<std::string>();

        std::function<void(json)> handler;

        {
            std::lock_guard<std::mutex> lock(eventMutex);
            auto it = eventHandlers.find(method);

            if (it != eventHandlers.end())
                handler = it->second;
        }

        if (handler)
            handler(msg.value("params", json::object()));
    }
}

inline bool WebSocketClient::connect(std::string url)
{
    ws.onOpen = [&]()
    {
        std::cout << "WebSocket connected\n";
        connected = true;
    };

    ws.onMessage = [&](const std::string& payload)
    {
        handleMessage(payload);
    };

    if (!ws.connect(url))
    {
        std::cout << "Connection error\n";
        return false;
    }

    while (!connected)
    {
        usleep(10000);
    }

    return true;
}

inline void WebSocketClient::disconnect()
{
    ws.close();
    connected = false;

    std::lock_guard<std::mutex> lock(responseMutex);
    responses.clear();
}

inline void WebSocketClient::onEvent(const std::string& method, std::function<void(json)> handler)
{
    std::lock_guard<std::mutex> lock(eventMutex);
    eventHandlers[method] = handler;
}

inline int WebSocketClient::send(std::string method, json params)
{
    if (!connected)
    {
        std::cout << "Socket not connected\n";
        return -1;
    }

    int id = idCounter++;

    json request;

    request["id"] = id;
    request["method"] = method;

    if (!params.empty())
        request["params"] = params;

    ws.send(request.dump());

    return id;
}

inline json WebSocketClient::sendAndWait(
    std::string method,
    json params,
    int timeoutMs
)
{
    int id = send(method, params);

    if (id < 0)
        return json::object();

    std::unique_lock<std::mutex> lock(responseMutex);

    bool got = responseCV.wait_for(
        lock,
        std::chrono::milliseconds(timeoutMs),
        [&]() { return responses.find(id) != responses.end(); }
    );

    if (!got)
    {
        std::cout << "Timed out waiting for response to " << method << std::endl;
        return json::object();
    }

    json response = responses[id];
    responses.erase(id);

    if (response.contains("error"))
    {
        std::cout << "CDP error for " << method << ": "
                   << response["error"].dump() << std::endl;
        return json::object();
    }

    return response.value("result", json::object());
}

inline WebSocketClient::~WebSocketClient()
{
    ws.close();
}
