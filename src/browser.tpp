#include <iostream>
#include <unistd.h>
#include <chrono>
#include <mutex>
#include <condition_variable>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

inline size_t Browser::WriteCallback(
    void* contents,
    size_t size,
    size_t nmemb,
    std::string* output
)
{
    size_t total = size * nmemb;

    output->append(
        (char*)contents,
        total
    );

    return total;
}

inline bool Browser::hasFrameworkRoot(const std::string& html)
{
    std::vector<std::string> patterns = {
        "id=\"root\"",
        "id='root'",
        "id=\"app\"",
        "<app-root>"
    };

    for (auto& p : patterns)
    {
        if (html.find(p) != std::string::npos)
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
    std::string command =
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

    int result = system(command.c_str());

    if (result == 0)
    {
        std::cout << "Chrome launch command issued\n";
        chromeLaunched = true;
    }
    else
    {
        std::cout << "Chrome failed to launch\n";
        chromeLaunched = false;
    }

    sleep(2);
}

inline bool Browser::getSocketURL()
{
    if (!chromeLaunched)
    {
        std::cout << "Chrome was never launched; cannot fetch debug targets\n";
        return false;
    }

    CURL* curl = curl_easy_init();

    if (!curl)
    {
        std::cout << "curl_easy_init failed\n";
        return false;
    }

    std::string response;

    curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:9222/json");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);

    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
    {
        std::cout << "Curl error " << curl_easy_strerror(res) << std::endl;
        return false;
    }

    try
    {
        json data = json::parse(response);

        if (data.empty())
        {
            std::cout << "No debug targets returned by Chrome\n";
            return false;
        }

        std::cout << "\nAvailable debug targets:\n";

        for (auto& target : data)
        {
            std::cout << "  type=" << target.value("type", "?")
                       << "  url=" << target.value("url", "?")
                       << std::endl;
        }

        bool found = false;

        for (auto& target : data)
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
            std::cout << "No target with type=='page' found; "
                         "falling back to first entry\n";

            websocketURL = data[0]["webSocketDebuggerUrl"];
        }

        std::cout << "\nUsing debugger URL:\n" << websocketURL << std::endl;
    }
    catch (std::exception& e)
    {
        std::cout << "JSON error " << e.what() << std::endl;
        return false;
    }

    return true;
}

inline bool Browser::openPage(std::string url)
{
    if (socketOpen)
    {
        socket.disconnect();
        socketOpen = false;
    }

    if (!socket.connect(websocketURL))
    {
        std::cout << "Failed to connect to debugger websocket\n";
        return false;
    }

    socketOpen = true;

    socket.sendAndWait("Page.enable");

    socket.sendAndWait(
        "Page.setLifecycleEventsEnabled",
        { { "enabled", true } }
    );

    std::mutex loadMutex;
    std::condition_variable loadCV;
    bool settled = false;

    socket.onEvent("Page.lifecycleEvent", [&](json params)
    {
        std::string name = params.value("name", "");

        if (name == "networkIdle")
        {
            {
                std::lock_guard<std::mutex> lock(loadMutex);
                settled = true;
            }
            loadCV.notify_all();
        }
    });

    socket.sendAndWait("Page.navigate", { { "url", url } });

    bool fired;

    {
        std::unique_lock<std::mutex> lock(loadMutex);

        fired = loadCV.wait_for(
            lock,
            std::chrono::seconds(20),
            [&]() { return settled; }
        );

        if (!fired)
            std::cout << "Warning: networkIdle not observed within "
                         "timeout, reading DOM anyway\n";
    }

    socket.onEvent("Page.lifecycleEvent", nullptr);

    usleep(500000);

    return true;
}

inline std::string Browser::getHtml()
{
    if (!socketOpen)
    {
        std::cout << "getHtml() called with no open debugger connection\n";
        return "";
    }

    socket.sendAndWait("Runtime.enable");

    json result = socket.sendAndWait(
        "Runtime.evaluate",
        {
            { "expression", "document.documentElement.outerHTML" },
            { "returnByValue", true }
        }
    );

    try
    {
        return result["result"]["value"].get<std::string>();
    }
    catch (std::exception& e)
    {
        std::cout << "Failed to extract HTML: " << e.what() << std::endl;
        return "";
    }
}

inline std::string Browser::fetch(std::string url)
{
    std::string html;

    CURL* curl = curl_easy_init();

    if (!curl)
    {
        std::cout << "curl_easy_init failed\n";
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
        std::cout << "Curl error " << curl_easy_strerror(res) << std::endl;
        curl_easy_cleanup(curl);
        return "";
    }

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    std::cout << "HTTP status: " << httpCode
               << ", body length: " << html.size() << std::endl;

    if (httpCode < 200 || httpCode >= 300)
    {
        std::cout << "Non-success HTTP status (" << httpCode
                   << "), treating as fetch failure\n";
        return "";
    }

    bool flag = hasFrameworkRoot(html);

    if (flag == false)
    {
        std::cout << "static file\n";
        return html;
    }

    std::cout << "dynamic site\n";

    if (!getSocketURL())
    {
        std::cout << "Failed to get debugger socket URL\n";
        return "";
    }

    if (!openPage(url))
    {
        std::cout << "Failed to open page via debugger\n";
        return "";
    }

    return getHtml();
}
