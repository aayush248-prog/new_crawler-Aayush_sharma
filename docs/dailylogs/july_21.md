# Work Log — July 21, 2026 (Compiled)

## Goals for the Day
- Understand MongoDB well enough to use it as the crawler's persistence layer, and
  pin down exactly what the project needs from it.
- Build a URL normalizer so relative links extracted from a page can be resolved to
  absolute URLs before they're deduped/queued.

---

## Session 1 — Getting to Know MongoDB & Finding Project Requirements

### Why MongoDB
- Crawler needs to persist crawled state (`url`, `depth`) so a crawl can be
  interrupted and resumed instead of restarting from the seed URL every time.
- Data is simple and document-shaped (`{url, depth}` per page) with no need for
  joins or a fixed schema — a good fit for a document store over a relational DB.
- Wanted something crawlable data can be dumped into quickly during development,
  without migrations, and that's easy to inspect (`mongosh` / Compass) while
  iterating on the crawler.

### Getting to Know MongoDB
- Reviewed core concepts: databases → collections → documents (BSON), and how
  that maps onto the crawler's needs (one collection per crawled site).
- Looked at the official C++ driver: `mongocxx` (high-level API) sitting on top of
  `bsoncxx` (BSON document building/parsing) and the underlying `mongoc` C driver.
- Read up on `mongocxx::instance` being a required, process-wide singleton that
  must be constructed exactly once before any `mongocxx::client`.
- Looked at `bsoncxx::builder::basic::make_document` / `kvp` for building BSON
  documents to insert, and at cursor iteration (`collection.find({})`) for reading
  them back.
- Checked query options relevant to resuming a crawl: sorting by `_id` descending
  and `limit(1)` to fetch the most recently inserted document.

### Requirements Identified for the Project
- **Connection**: local MongoDB instance (`mongodb://localhost:27017`) is enough
  for development.
- **Schema (informal)**: each document needs at minimum `url` (string) and
  `depth` (int).
- **Per-site isolation**: separate collection per crawled site (named from the
  seed URL) so multiple crawls don't mix their visited-URL data.
- **Operations needed**: insert per crawled URL; bulk-read all documents (rebuild
  visited-set on startup); fetch the most recent document (resume the frontier);
  delete a single document by URL (clear a resumed/duplicate entry); dump all
  documents for debugging.
- **Dependencies**: `mongocxx` + `bsoncxx` (MongoDB C++ driver) and the underlying
  `mongoc`/`libbson` C driver, wired into the CMake build.

---

## Session 2 — URL Normalizer (Absolute vs. Relative)

### Why it's needed
- Links pulled from a page's HTML can be absolute (`https://...`) or relative
  (`/about`, `contact.html`, `../pricing`).
- The frontier/seekstore only work correctly if every stored URL is in one
  consistent, absolute form — otherwise the same page could be queued twice, or a
  relative link could be crawled directly and fail.

### `isrelative()` — detecting the link type

```cpp
bool crawler::isrelative(string url) {
    return !(url.find("https://") == 0 || url.find("http://") == 0);
}
```

### `makeabs()` — resolving a relative link against the current page

```cpp
string crawler::makeabs(string currentUrl, string link) {
    if (!link.empty() && link[0] == '/') {
        // root-relative: keep scheme + host, drop the rest of the path
        size_t pos = currentUrl.find("://");
        pos = currentUrl.find('/', pos + 3);

        if (pos == string::npos)
            return currentUrl + link;

        return currentUrl.substr(0, pos) + link;
    }

    // current URL is already a directory (ends in "/")
    if (!currentUrl.empty() && currentUrl.back() == '/')
        return currentUrl + link;

    // otherwise strip the last path segment before appending
    size_t pos = currentUrl.rfind('/');

    if (pos != string::npos)
        currentUrl = currentUrl.substr(0, pos);

    return currentUrl + "/" + link;
}
```

Handles root-relative links (resolved against scheme+host) and document-relative
links (resolved against the current URL's directory).

### Wiring into the crawl loop

```cpp
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
```

Every URL is normalized before it's checked against `seekstore` or pushed onto the
`frontier`, so de-duplication and persistence stay consistent.

---

## Errors / Issues Encountered
- Had to get the `mongocxx::instance` singleton rule right early — constructing it
  more than once throws, so it needed to live in `main()` and nowhere else.
- Root-relative vs. document-relative resolution in `makeabs()` needed two separate
  code paths; an early version handled only one and mis-resolved links on
  non-directory current URLs (had to add the "strip last path segment" branch).
- No handling yet for `../` segment collapsing or query-string/fragment stripping
  in `makeabs()` — normalizer currently does simple string concatenation only.

## Achievements
- MongoDB connection requirements and the driver API surface needed for the
  project are now understood and scoped.
- A working `isrelative()` / `makeabs()` pair that resolves both root-relative and
  document-relative links against the current page, wired directly into the crawl
  loop.
- Groundwork laid for the frontier/seekstore + MongoDB integration built the
  following day.

### Notes
-