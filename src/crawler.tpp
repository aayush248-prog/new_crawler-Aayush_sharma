#ifndef WEBCRAWLER_TPP
#define WEBCRAWLER_TPP

// This file is included at the bottom of WebCrawler.h — it is not
// meant to be compiled or #included on its own.

// =========================
// Utility
// =========================

inline string base64_encode(const uint8_t* data, size_t len)
{
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    string out;
    out.reserve(((len + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= len)
    {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back(table[(n >> 6) & 0x3F]);
        out.push_back(table[n & 0x3F]);
        i += 3;
    }

    size_t rem = len - i;
    if (rem == 1)
    {
        uint32_t n = data[i] << 16;
        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    }
    else if (rem == 2)
    {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back(table[(n >> 6) & 0x3F]);
        out.push_back('=');
    }

    return out;
}


// =========================
// MinimalWebSocket
// =========================

inline bool MinimalWebSocket::readAll(int fd, void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
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
    const string &url,
    string &host,
    int &port,
    string &path
)
{
    string prefix = "ws://";

    if (url.compare(0, prefix.size(), prefix) != 0)
        return false;

    string rest = url.substr(prefix.size());

    size_t slashPos = rest.find('/');

    string hostPort =
        (slashPos == string::npos) ? rest : rest.substr(0, slashPos);

    path =
        (slashPos == string::npos) ? "/" : rest.substr(slashPos);

    size_t colonPos = hostPort.find(':');

    if (colonPos == string::npos)
    {
        host = hostPort;
        port = 80;
    }
    else
    {
        host = hostPort.substr(0, colonPos);
        port = stoi(hostPort.substr(colonPos + 1));
    }

    return true;
}


inline bool MinimalWebSocket::doHandshake(
    const string &host,
    int port,
    const string &path
)
{
    uint8_t keyBytes[16];

    for (int i = 0; i < 16; i++)
        keyBytes[i] = (uint8_t)(rand() % 256);

    string secKey = base64_encode(keyBytes, 16);

    ostringstream req;

    req << "GET " << path << " HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "Upgrade: websocket\r\n"
        << "Connection: Upgrade\r\n"
        << "Sec-WebSocket-Key: " << secKey << "\r\n"
        << "Sec-WebSocket-Version: 13\r\n"
        << "\r\n";

    string reqStr = req.str();

    if (::write(sockfd, reqStr.data(), reqStr.size()) < 0)
        return false;

    // Read response headers until the blank line
    string resp;
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
    if (resp.find(" 101 ") == string::npos)
    {
        cout << "WebSocket handshake failed:\n" << resp << endl;
        return false;
    }

    return true;
}


inline bool MinimalWebSocket::readFrame(uint8_t &opcode, bool &fin, string &payload)
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


inline void MinimalWebSocket::writeFrame(uint8_t opcode, const string &payload)
{
    string frame;

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

    frame.append((char *)maskKey, 4);

    string maskedPayload = payload;

    for (uint64_t i = 0; i < len; i++)
        maskedPayload[i] =
            (char)((uint8_t)maskedPayload[i] ^ maskKey[i % 4]);

    frame += maskedPayload;

    ::write(sockfd, frame.data(), frame.size());
}


inline bool MinimalWebSocket::connect(const string &url)
{
    string host, path;
    int port = 80;

    if (!parseWsUrl(url, host, port, path))
    {
        cout << "Invalid WebSocket URL: " << url << endl;
        return false;
    }

    sockfd = ::socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        cout << "Socket creation failed\n";
        return false;
    }

    struct hostent *server = gethostbyname(host.c_str());

    if (!server)
    {
        cout << "DNS resolution failed for " << host << endl;
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
            (struct sockaddr *)&serv_addr,
            sizeof(serv_addr)
        ) < 0)
    {
        cout << "TCP connect failed\n";
        return false;
    }

    if (!doHandshake(host, port, path))
        return false;

    running = true;

    if (onOpen)
        onOpen();

    readerThread = thread(&MinimalWebSocket::readLoop, this);

    return true;
}


inline void MinimalWebSocket::readLoop()
{
    string messageBuffer;
    bool assembling = false;

    while (running)
    {
        uint8_t opcode;
        bool fin;
        string payload;

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


inline void MinimalWebSocket::send(const string &data)
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


// =========================
// WebSocketClient
// =========================

inline void WebSocketClient::handleMessage(const string &payload)
{
    json msg;

    try
    {
        msg = json::parse(payload);
    }
    catch (exception &e)
    {
        cout << "Failed to parse CDP message: " << e.what() << endl;
        return;
    }

    if (msg.contains("id"))
    {
        int id = msg["id"].get<int>();

        {
            lock_guard<mutex> lock(responseMutex);
            responses[id] = msg;
        }

        responseCV.notify_all();
    }
    else if (msg.contains("method"))
    {
        string method = msg["method"].get<string>();

        function<void(json)> handler;

        {
            lock_guard<mutex> lock(eventMutex);
            auto it = eventHandlers.find(method);

            if (it != eventHandlers.end())
                handler = it->second;
        }

        if (handler)
            handler(msg.value("params", json::object()));
    }
}


inline bool WebSocketClient::connect(string url)
{
    ws.onOpen = [&]()
    {
        cout << "WebSocket connected\n";
        connected = true;
    };

    ws.onMessage = [&](const string &payload)
    {
        handleMessage(payload);
    };

    if (!ws.connect(url))
    {
        cout << "Connection error\n";
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

    lock_guard<mutex> lock(responseMutex);
    responses.clear();
}


inline void WebSocketClient::onEvent(const string &method, function<void(json)> handler)
{
    lock_guard<mutex> lock(eventMutex);
    eventHandlers[method] = handler;
}


inline int WebSocketClient::send(
    string method,
    json params
)
{
    if (!connected)
    {
        cout << "Socket not connected\n";
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
    string method,
    json params,
    int timeoutMs
)
{
    int id = send(method, params);

    if (id < 0)
        return json::object();

    unique_lock<mutex> lock(responseMutex);

    bool got = responseCV.wait_for(
        lock,
        chrono::milliseconds(timeoutMs),
        [&]() { return responses.find(id) != responses.end(); }
    );

    if (!got)
    {
        cout << "Timed out waiting for response to " << method << endl;
        return json::object();
    }

    json response = responses[id];
    responses.erase(id);

    if (response.contains("error"))
    {
        cout << "CDP error for " << method << ": "
             << response["error"].dump() << endl;
        return json::object();
    }

    return response.value("result", json::object());
}


inline WebSocketClient::~WebSocketClient()
{
    ws.close();
}


// =========================
// Browser
// =========================

inline size_t Browser::WriteCallback(
    void *contents,
    size_t size,
    size_t nmemb,
    string *output
)
{
    size_t total = size * nmemb;

    output->append(
        (char *)contents,
        total
    );

    return total;
}


inline bool Browser::hasFrameworkRoot(const string &html)
{
    vector<string> patterns = {
        "id=\"root\"",
        "id='root'",
        "id=\"app\"",
        "<app-root>"
    };

    for (auto &p : patterns)
    {
        if (html.find(p) != string::npos)
            return true;
    }

    return false;
}


inline Browser::Browser()
{
    system("pkill -f 'remote-debugging-port=9222' 2>/dev/null");
    sleep(1);

    // NOTE: this path is macOS-only. On Linux, use something like
    // "google-chrome" or "chromium-browser"; on Windows, the full
    // path to chrome.exe. Consider making this configurable
    // (env var or constructor argument) rather than hardcoded.
    string command =
"/Applications/Google\\ Chrome.app/Contents/MacOS/Google\\ Chrome "
"--headless=new "
"--remote-debugging-port=9222 "
"--disable-gpu "
"--disable-extensions "
"--disable-background-networking "
"--disable-background-mode "
"--disable-component-update "
"--disable-domain-reliability "
"--disable-crash-reporter "
"--no-first-run "
"--no-default-browser-check "
"--noerrdialogs "
"about:blank >/dev/null 2>&1 &";

    int result =
        system(command.c_str());

    if (result == 0)
    {
        cout << "Chrome launch command issued\n";
        chromeLaunched = true;
    }
    else
    {
        cout << "Chrome failed to launch\n";
        chromeLaunched = false;
    }

    sleep(2);
}


inline bool Browser::getSocketURL()
{
    if (!chromeLaunched)
    {
        cout << "Chrome was never launched; cannot fetch debug targets\n";
        return false;
    }

    CURL *curl =
        curl_easy_init();

    if (!curl)
    {
        cout << "curl_easy_init failed\n";
        return false;
    }

    string response;

    curl_easy_setopt(
        curl,
        CURLOPT_URL,
        "http://localhost:9222/json"
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEFUNCTION,
        WriteCallback
    );

    curl_easy_setopt(
        curl,
        CURLOPT_WRITEDATA,
        &response
    );

    CURLcode res =
        curl_easy_perform(curl);

    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
    {
        cout << "Curl error "
             << curl_easy_strerror(res)
             << endl;

        return false;
    }


    try
    {
        json data =
            json::parse(response);

        if (data.empty())
        {
            cout << "No debug targets returned by Chrome\n";
            return false;
        }

        cout << "\nAvailable debug targets:\n";

        for (auto &target : data)
        {
            cout << "  type=" << target.value("type", "?")
                 << "  url=" << target.value("url", "?")
                 << endl;
        }

        bool found = false;

        for (auto &target : data)
        {
            if (target.value("type", "") == "page")
            {
                websocketURL = target["webSocketDebuggerUrl"];
                found = true;
                break;
            }
        }

        if (!found)
        {
            cout << "No target with type=='page' found; "
                    "falling back to first entry\n";

            websocketURL =
                data[0]["webSocketDebuggerUrl"];
        }

        cout << "\nUsing debugger URL:\n"
             << websocketURL
             << endl;
    }

    catch (exception &e)
    {
        cout << "JSON error "
             << e.what()
             << endl;

        return false;
    }

    return true;
}


inline bool Browser::openPage(string url)
{
    if (socketOpen)
    {
        socket.disconnect();
        socketOpen = false;
    }

    if (!socket.connect(websocketURL))
    {
        cout << "Failed to connect to debugger websocket\n";
        return false;
    }

    socketOpen = true;

    socket.sendAndWait("Page.enable");

    socket.sendAndWait(
        "Page.setLifecycleEventsEnabled",
        {
            {
                "enabled",
                true
            }
        }
    );

    mutex loadMutex;
    condition_variable loadCV;
    bool settled = false;

    socket.onEvent("Page.lifecycleEvent", [&](json params)
    {
        string name = params.value("name", "");

        if (name == "networkIdle")
        {
            {
                lock_guard<mutex> lock(loadMutex);
                settled = true;
            }
            loadCV.notify_all();
        }
    });

    socket.sendAndWait(
        "Page.navigate",
        {
            {
                "url",
                url
            }
        }
    );

    bool fired;

    {
        unique_lock<mutex> lock(loadMutex);

        fired = loadCV.wait_for(
            lock,
            chrono::seconds(20),
            [&]() { return settled; }
        );

        if (!fired)
            cout << "Warning: networkIdle not observed within "
                    "timeout, reading DOM anyway\n";
    }

    socket.onEvent("Page.lifecycleEvent", nullptr);

    usleep(500000);

    return true;
}


inline string Browser::getHtml()
{
    if (!socketOpen)
    {
        cout << "getHtml() called with no open debugger connection\n";
        return "";
    }

    socket.sendAndWait("Runtime.enable");

    json result = socket.sendAndWait(
        "Runtime.evaluate",
        {
            {
                "expression",
                "document.documentElement.outerHTML"
            },

            {
                "returnByValue",
                true
            }
        }
    );

    try
    {
        return result["result"]["value"].get<string>();
    }
    catch (exception &e)
    {
        cout << "Failed to extract HTML: " << e.what() << endl;
        return "";
    }
}


inline string Browser::fetch(string url)
{
    string html;

    CURL *curl = curl_easy_init();

    if (!curl)
    {
        cout << "curl_easy_init failed\n";
        return "";
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    // Force HTTP/1.1 — avoids HTTP/2/ALPN negotiation issues that can
    // occur if libcurl's http2 support isn't cleanly built against
    // the local SSL library.
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

    // Temporary — prints curl's full connection trace so you can see
    // exactly where a failure occurs (TLS handshake, HTTP version
    // negotiation, etc). Remove once diagnosed; very noisy otherwise.
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK)
    {
        cout << "Curl error " << curl_easy_strerror(res) << endl;
        curl_easy_cleanup(curl);
        return "";
    }

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    cout << "HTTP status: " << httpCode
         << ", body length: " << html.size() << endl;

    if (httpCode < 200 || httpCode >= 300)
    {
        cout << "Non-success HTTP status (" << httpCode
             << "), treating as fetch failure\n";
        return "";
    }

    bool flag = hasFrameworkRoot(html);

    if (flag == false)
    {
        cout << "static file\n";
        return html;
    }

    cout << "dynamic site\n";

    if (!getSocketURL())
    {
        cout << "Failed to get debugger socket URL\n";
        return "";
    }

    if (!openPage(url))
    {
        cout << "Failed to open page via debugger\n";
        return "";
    }

    return getHtml();
}


// =========================
// pageload
// =========================

inline pageload::pageload()
{
    pagecount = 0;
}


inline void pageload::getpage(string path1)
{
    string path = "./include/pages/" + path1 + ".txt";
    ifstream file(path);

    if (!file.is_open())
    {
        cout << "File not found: " << path << endl;
        return;
    }

    string text;

    while (getline(file, text))
    {
        cout << text << endl;
    }

    file.close();
}


inline bool pageload::loadpage(string url)
{
    try
    {
        lastHtml = browser.fetch(url);

        if (lastHtml.empty())
        {
            cout << "Failed to fetch: " << url << endl;
            return false;
        }

        return true;
    }
    catch (exception &err)
    {
        cout << err.what() << endl;
        return false;
    }
}


inline string pageload::savepage(string name)
{
    try
    {
        if (lastHtml.empty())
        {
            throw runtime_error(
                "No page loaded — call loadpage() successfully first"
            );
        }

        // mkdir() returns -1 on failure. EEXIST just means the directory
        // is already there, which is fine; anything else (permissions,
        // a file blocking the path, etc.) is a real problem and needs
        // to surface instead of being silently ignored.
        if (mkdir("./include", 0755) != 0 && errno != EEXIST)
        {
            throw runtime_error(
                "mkdir(./include) failed: " + string(strerror(errno))
            );
        }

        if (mkdir("./include/pages", 0755) != 0 && errno != EEXIST)
        {
            throw runtime_error(
                "mkdir(./include/pages) failed: " + string(strerror(errno))
            );
        }

        string path = "./include/pages/" + name + ".txt";

        ofstream file(path);

        if (!file.is_open())
        {
            throw runtime_error(
                "File cannot open: " + path + " (" + strerror(errno) + ")"
            );
        }

        file << lastHtml;

        if (file.fail())
        {
            throw runtime_error("Write to " + path + " failed");
        }

        file.close();

        pagecount++;

        // Print the resolved absolute path so it's obvious where the
        // file actually landed — "./include/pages/..." is relative to
        // whatever directory the binary was launched from, which is
        // easy to lose track of.
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd)))
        {
            cout << "Saved page to: " << cwd << "/" << path.substr(2) << endl;
        }
        else
        {
            cout << "Saved page to: " << path << endl;
        }

        return path;
    }
    catch (exception &err)
    {
        cout << "savepage() failed: " << err.what() << endl;
        return "";
    }
}


inline int pageload::pages()
{
    return pagecount;
}


inline vector<string> pageload::linktag()
{
    vector<string> links;
    set<string> seen;

    regex pattern(R"re(<a\s[^>]*href\s*=\s*"([^"]*)")re", regex::icase);

    sregex_iterator begin(lastHtml.begin(), lastHtml.end(), pattern);
    sregex_iterator end;

    for (auto it = begin; it != end; ++it)
    {
        string href = (*it)[1].str();

        if (isIgnoredHref(href))
            continue;

        string trimmed = trim(href);

        if (seen.count(trimmed))
            continue;

        seen.insert(trimmed);
        links.push_back(trimmed);
    }

    return links;
}


inline string pageload::trim(const string &s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos)
        return "";
    size_t stop = s.find_last_not_of(" \t\r\n");
    return s.substr(start, stop - start + 1);
}


inline bool pageload::isIgnoredHref(const string &href)
{
    string trimmed = trim(href);

    if (trimmed.empty())
        return true;

    if (trimmed[0] == '#')
        return true;

    auto startsWithCI = [&](const string &prefix) {
        if (trimmed.size() < prefix.size())
            return false;
        return equal(prefix.begin(), prefix.end(), trimmed.begin(),
                     [](char a, char b) { return tolower(a) == tolower(b); });
    };

    if (startsWithCI("tel:"))
        return true;
    if (startsWithCI("mailto:"))
        return true;
    if (startsWithCI("javascript:"))
        return true;

    return false;
}


// =========================
// Database
// =========================

inline Database::Database(string& url)
    : client(mongocxx::uri{"mongodb://localhost:27017"}),
      db(client["new_crawler_base"]),
      collection(db[url])
{
}


inline void Database::insert(const std::string& url, int depth)
{
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto value = make_document(
        kvp("url", url),
        kvp("depth", depth)
    );

    collection.insert_one(value.view());
}


inline void Database::print()
{
    auto cursor = collection.find({});

    for (auto&& doc : cursor)
    {
        std::cout << bsoncxx::to_json(doc) << '\n';
    }
}


inline void Database::deleteOne(const std::string& url)
{
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    auto filter = make_document(kvp("url", url));

    auto result = collection.delete_one(filter.view());

    if (result) {
        std::cout << "Deleted " << result->deleted_count() << " document(s)\n";
    }
}


// =========================
// frontier
// =========================

inline void frontier::push(node val) {
    ls.push(val);
}

inline void frontier::pop() {
    ls.pop_front();
}

inline int frontier::size() {
    return ls.Size();
}

inline node frontier::peek() {
    if (ls.isempty())
        throw std::out_of_range("peek() called on empty frontier");

    return ls.begin()->value;
}

inline bool frontier::isempty() {
    return ls.isempty();
}


// =========================
// seekstore
// =========================

inline void seekstore::push(string val, int depth) {
    if (hs.find(val) != NULL) return;
    node ns;
    ns.depth = depth;
    ns.url = val;
    hs.push(val, ns);
}

inline void seekstore::pop(string val) { hs.pop(val); }

inline bool seekstore::exists(string val) { return hs.find(val) != NULL; }

inline node* seekstore::get(string val) {
    HashNode<string, node>* n = hs.find(val);
    if (n == NULL) return NULL;
    return &n->Value;
}


// Now that seekstore/frontier are complete types, define the two
// Database methods that depend on them:

inline void Database::get(seekstore & hm)
{
    auto cursor = collection.find({});
    string url1 = "";
    for (auto&& doc : cursor)
    {
        string url = string(doc["url"].get_string().value);
        int depth = doc["depth"].get_int32().value;
        url1 = url;
        hm.push(url, depth);
    }
    hm.pop(url1);
}

inline bool Database::getlast(frontier & fr)
{
    using bsoncxx::builder::basic::kvp;
    using bsoncxx::builder::basic::make_document;

    mongocxx::options::find opts{};
    opts.sort(make_document(kvp("_id", -1)));  // descending = newest first
    opts.limit(1);

    auto cursor = collection.find({}, opts);

    for (auto&& doc : cursor)
    {
        string url   = string(doc["url"].get_string().value);
        int depth = doc["depth"].get_int32().value;
        node n;
        n.url = url;
        n.depth = depth;
        fr.push(n);
        return true;   // only one doc will ever be yielded, due to limit(1)
    }

    return false;   // collection was empty
}


// =========================
// crawler
// =========================

inline crawler::crawler(string url) { seedurl = url; }

inline bool crawler::isrelative(string url) {
    return !(url.find("https://") == 0 || url.find("http://") == 0);
}

inline string crawler::makeabs(string currentUrl, string link) {
    if (!link.empty() && link[0] == '/') {
        size_t pos = currentUrl.find("://");
        pos = currentUrl.find('/', pos + 3);

        if (pos == string::npos)
            return currentUrl + link;

        return currentUrl.substr(0, pos) + link;
    }

    // Current URL is a directory
    if (!currentUrl.empty() && currentUrl.back() == '/')
        return currentUrl + link;

    // Remove last path component
    size_t pos = currentUrl.rfind('/');

    if (pos != string::npos)
        currentUrl = currentUrl.substr(0, pos);

    return currentUrl + "/" + link;
}

inline string crawler::getCollectionName(string url)
{
    // remove protocol
    size_t pos = url.find("://");

    if (pos != string::npos)
        url = url.substr(pos + 3);

    // remove path
    pos = url.find('/');

    if (pos != string::npos)
        url = url.substr(0, pos);

    // replace invalid characters
    for (char &c : url)
    {
        if (c == '.')
            c = '_';
    }

    return url;
}

inline void crawler::start() {
    pageload pg;
    frontier fr;
    seekstore ss;
    string sub = getCollectionName(seedurl);
    Database db(sub);
    // NOTE: mongocxx::instance is created exactly once, in main().
    // Do NOT create another one here — a second instance throws.
    db.get(ss);
    int i = 0;
    node ns;

    ns.depth = 0;
    ns.url = seedurl;
    string abs = "";
    if (!db.getlast(fr)) {
        fr.push(ns);
    }
    else {
        node fs = fr.peek();
        db.deleteOne(fs.url);
    }

    while (!fr.isempty()) {
        node st = fr.peek();
        int height = st.depth;
        string url = st.url;
        i++;
        fr.pop();
        if (height > 2) continue;
        if (ss.exists(url)) continue;
        ss.push(url, height);
        db.insert(url, height);
        if (!pg.loadpage(url)) continue;
        string name = "index_" + to_string(height) + to_string(i);
        pg.savepage(name);
        cout << height << " ";
        vector<string> v = pg.linktag();
        for (string s : v) {
            if (isrelative(s)) {
                s = makeabs(url, s);
            }
            if (!ss.exists(s)) {
                node n;
                n.url = s;
                n.depth = height + 1;
                fr.push(n);
            }
        }
    }
}

#endif // WEBCRAWLER_TPP
