#include <iostream>
#include <fstream>
#include <stdexcept>
#include <set>
#include <regex>
#include <algorithm>
#include <cctype>
#include <sys/stat.h>   // for mkdir()

inline pageload::pageload()
{
    pagecount = 0;
}

inline void pageload::getpage(std::string path1)
{
    std::string path = "./include/pages/" + path1 + ".txt";
    std::ifstream file(path);

    if (!file.is_open())
    {
        std::cout << "File not found: " << path << std::endl;
        return;
    }

    std::string text;

    while (std::getline(file, text))
    {
        std::cout << text << std::endl;
    }

    file.close();
}

inline bool pageload::loadpage(std::string url)
{
    try
    {
        lastHtml = browser.fetch(url);

        if (lastHtml.empty())
        {
            std::cout << "Failed to fetch: " << url << std::endl;
            return false;
        }

        return true;
    }
    catch (std::exception& err)
    {
        std::cout << err.what() << std::endl;
        return false;
    }
}

inline std::string pageload::savepage(std::string name)
{
    try
    {
        if (lastHtml.empty())
        {
            throw std::runtime_error(
                "No page loaded — call loadpage() successfully first"
            );
        }

        mkdir("./include", 0755);
        mkdir("./include/pages", 0755);

        std::string path = "./include/pages/" + name + ".txt";

        std::ofstream file(path);

        if (!file.is_open())
        {
            throw std::runtime_error("File cannot open: " + path);
        }

        pagecount++;

        file << lastHtml;

        file.close();

        return path;
    }
    catch (std::exception& err)
    {
        std::cout << err.what() << std::endl;
        return "";
    }
}

inline int pageload::pages()
{
    return pagecount;
}

inline std::vector<std::string> pageload::linktag()
{
    std::vector<std::string> links;
    std::set<std::string> seen;

    std::regex pattern(R"re(<a\s[^>]*href\s*=\s*"([^"]*)")re", std::regex::icase);

    std::sregex_iterator begin(lastHtml.begin(), lastHtml.end(), pattern);
    std::sregex_iterator end;

    for (auto it = begin; it != end; ++it)
    {
        std::string href = (*it)[1].str();

        if (isIgnoredHref(href))
            continue;

        std::string trimmed = trim(href);

        if (seen.count(trimmed))
            continue;

        seen.insert(trimmed);
        links.push_back(trimmed);
    }

    return links;
}

inline std::string pageload::trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t stop = s.find_last_not_of(" \t\r\n");
    return s.substr(start, stop - start + 1);
}

inline bool pageload::isIgnoredHref(const std::string& href)
{
    std::string trimmed = trim(href);

    if (trimmed.empty())
        return true;

    if (trimmed[0] == '#')
        return true;

    auto startsWithCI = [&](const std::string& prefix) {
        if (trimmed.size() < prefix.size())
            return false;
        return std::equal(prefix.begin(), prefix.end(), trimmed.begin(),
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
