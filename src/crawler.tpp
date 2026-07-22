#include <iostream>
#include <vector>

inline crawler::crawler(std::string url)
{
    seedurl = url;
}

inline bool crawler::isrelative(std::string url)
{
    return !(url.find("https://") == 0 || url.find("http://") == 0);
}

inline std::string crawler::makeabs(std::string currentUrl, std::string link)
{
    if (!link.empty() && link[0] == '/')
    {
        size_t pos = currentUrl.find("://");
        pos = currentUrl.find('/', pos + 3);

        if (pos == std::string::npos)
            return currentUrl + link;

        return currentUrl.substr(0, pos) + link;
    }

    // Current URL is a directory
    if (!currentUrl.empty() && currentUrl.back() == '/')
        return currentUrl + link;

    // Remove last path component
    size_t pos = currentUrl.rfind('/');

    if (pos != std::string::npos)
        currentUrl = currentUrl.substr(0, pos);

    return currentUrl + "/" + link;
}

inline void crawler::start()
{
    pageload pg;
    frontier fr;
    seekstore ss;
    int i = 0;
    node ns;
    ns.depth = 0;
    ns.url = seedurl;
    fr.push(ns);

    while (!fr.isempty())
    {
        node st = fr.peek();
        int height = st.depth;
        std::string url = st.url;
        fr.pop();
        i++;

        if (height > 2) continue;
        if (ss.exists(url)) continue;

        ss.push(url, height);

        if (!pg.loadpage(url)) continue;

        std::string name = "index_" + std::to_string(height) + std::to_string(i);
        pg.savepage(name);
        std::cout << height << " ";

        std::vector<std::string> v = pg.linktag();

        for (std::string s : v)
        {
            if (isrelative(s))
            {
                s = makeabs(url, s);
            }

            if (!ss.exists(s))
            {
                node n;
                n.url = s;
                n.depth = height + 1;
                fr.push(n);
            }
        }
    }
}
