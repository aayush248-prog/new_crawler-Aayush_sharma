#ifndef WEBCRAWLER_H
#define WEBCRAWLER_H

#include <iostream>
#include <fstream>
#include <string>
#include "List.h"
#include <stdexcept>
#include <unistd.h>
#include <thread>
#include <sstream>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <map>
#include <chrono>
#include <vector>
#include <set>
#include <regex>
#include <algorithm>
#include <cctype>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/stat.h>   // for mkdir()
#include <cerrno>       // for errno / strerror() diagnostics
#include <Hashmap.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>

using namespace std;
using json = nlohmann::json;

// =========================
// Utility
// =========================

string base64_encode(const uint8_t* data, size_t len);


// =========================
// Minimal WebSocket client
// (raw sockets, RFC6455 framing — no Boost, no websocketpp)
// =========================

class MinimalWebSocket
{

private:

    int sockfd = -1;

    atomic<bool> running{false};

    static bool readAll(int fd, void *buf, size_t len);

    static bool parseWsUrl(
        const string &url,
        string &host,
        int &port,
        string &path
    );

    bool doHandshake(
        const string &host,
        int port,
        const string &path
    );

    bool readFrame(uint8_t &opcode, bool &fin, string &payload);

    void writeFrame(uint8_t opcode, const string &payload);

public:

    function<void()> onOpen;
    function<void(const string &)> onMessage;

    thread readerThread;

    bool connect(const string &url);

    void readLoop();

    void send(const string &data);

    void close();

    ~MinimalWebSocket();

};


// =========================
// WebSocket Client (CDP-facing wrapper)
// =========================

class WebSocketClient
{

private:

    MinimalWebSocket ws;

    atomic<bool> connected{false};

    atomic<int> idCounter{1};

    mutex responseMutex;
    condition_variable responseCV;
    map<int, json> responses;

    mutex eventMutex;
    map<string, function<void(json)>> eventHandlers;

    void handleMessage(const string &payload);

public:

    bool connect(string url);

    void disconnect();

    void onEvent(const string &method, function<void(json)> handler);

    int send(
        string method,
        json params = {}
    );

    json sendAndWait(
        string method,
        json params = {},
        int timeoutMs = 10000
    );

    ~WebSocketClient();

};


// =========================
// Browser Controller
// =========================

class Browser
{

private:

    string websocketURL;

    WebSocketClient socket;

    bool chromeLaunched = false;

    bool socketOpen = false;

    static size_t WriteCallback(
        void *contents,
        size_t size,
        size_t nmemb,
        string *output
    );

    bool hasFrameworkRoot(const string &html);

public:

    Browser();

    bool getSocketURL();

    bool openPage(string url);

    string getHtml();

    string fetch(string url);

};


// =========================
// Page load / save / link-extract wrapper
// =========================

class pageload
{

public:

    Browser browser;

    int pagecount;

    string lastHtml;

    pageload();

    void getpage(string path1);

    bool loadpage(string url);

    string savepage(string name);

    int pages();

    // Filters out href="" (dropdown toggles), href="#" (placeholders),
    // tel:/mailto:/javascript: links, and dedupes — none of the
    // filtered ones are real crawlable pages.
    vector<string> linktag();

private:

    static string trim(const string &s);

    // Returns true for href values that aren't real, fetchable page
    // links: empty, fragment-only (#...), tel:, mailto:, javascript:.
    static bool isIgnoredHref(const string &href);

};


// Rename the crawler's own list node so it can't collide with
// include/List.h's template Node<T>.
class node {
public:
    int depth;
    string url;
    node() {
        depth = -1;
        url = "";
    }
};


// =========================
// Database — declared before seekstore/frontier are complete,
// so Database::get()/getlast() are only *declared* here and
// defined in the .tpp once seekstore/frontier are complete types.
// =========================

class seekstore; // forward declaration, needed for Database::get()
class frontier;  // forward declaration, needed for Database::getlast()

class Database {
public:

    mongocxx::client client;
    mongocxx::database db;
    mongocxx::collection collection;

    Database(string& url);

    void insert(const std::string& url, int depth);

    // defined in the .tpp, after seekstore/frontier are complete
    void get(seekstore & hm);
    bool getlast(frontier & fr);

    void print();

    void deleteOne(const std::string& url);
};


class frontier {
public:
    Linkedlist<node> ls;

    frontier() {}

    void push(node val);

    void pop();

    int size();

    node peek();

    bool isempty();
};


class seekstore {
public:
    HashMap<string, node> hs;

    seekstore() {}

    void push(string val, int depth);

    void pop(string val);

    bool exists(string val);

    node* get(string val);
};


// =========================
// Crawler
// =========================

class crawler {
public:

    string seedurl;

    crawler(string url);

    bool isrelative(string url);

    string makeabs(string currentUrl, string link);

    string getCollectionName(string url);

    void start();
};

#include "crawler.tpp"

#endif // WEBCRAWLER_H