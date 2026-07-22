#pragma once

#include <string>

#include "PageLoad.h"
#include "Frontier.h"
#include "SeekStore.h"
#include "Node.h"

class crawler
{
public:

    std::string seedurl;

    crawler(std::string url);

    bool isrelative(std::string url);

    std::string makeabs(std::string currentUrl, std::string link);

    void start();

};

#include "Crawler.tpp"