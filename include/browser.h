#pragma once

#include <string>
#include <vector>

#include "WebSocketClient.h"

// =========================
// Browser Controller
// =========================

class Browser
{

private:

    std::string websocketURL;

    WebSocketClient socket;

    bool chromeLaunched = false;

    bool socketOpen = false;

    static size_t WriteCallback(
        void* contents,
        size_t size,
        size_t nmemb,
        std::string* output
    );

    bool hasFrameworkRoot(const std::string& html);

public:

    Browser();

    bool getSocketURL();

    bool openPage(std::string url);

    std::string getHtml();

    std::string fetch(std::string url);

};

#include "Browser.tpp"