#pragma once

#include <string>
#include <vector>

#include "Browser.h"

// =========================
// Page load / save / link-extract wrapper
// =========================

class pageload
{

public:

    Browser browser;

    int pagecount;

    std::string lastHtml;

    pageload();

    void getpage(std::string path1);

    bool loadpage(std::string url);

    std::string savepage(std::string name);

    int pages();

    // Extracts <a href> values, filters out non-page links, dedupes.
    // Note: as declared, this returns raw href strings — relative-URL
    // resolution (resolveUrl/lastUrl) is not yet wired into this
    // header split; see the crawler's own isrelative()/makeabs()
    // helpers, which currently do that job instead.
    std::vector<std::string> linktag();

private:

    static std::string trim(const std::string& s);

    static bool isIgnoredHref(const std::string& href);

};

#include "PageLoad.tpp"