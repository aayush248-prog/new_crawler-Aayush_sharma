# Work Log — July 21, 2026 (Session 1)

## Task: Getting to Know MongoDB & Finding Project Requirements

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
  must be constructed exactly once before any `mongocxx::client` — important to
  get right before writing any connection code.
- Looked at `bsoncxx::builder::basic::make_document` / `kvp` for building BSON
  documents to insert, and at cursor iteration (`collection.find({})`) for reading
  them back.
- Checked query options relevant to resuming a crawl: sorting by `_id` descending
  and `limit(1)` to fetch the most recently inserted document.

### Requirements Identified for the Project
- **Connection**: local MongoDB instance (`mongodb://localhost:27017`) is enough
  for development — no auth/replica-set setup needed yet.
- **Schema (informal)**: each document needs at minimum `url` (string) and
  `depth` (int).
- **Per-site isolation**: use a separate collection per crawled site (named from
  the seed URL) so multiple crawls don't mix their visited-URL data.
- **Operations needed**:
  - insert a document per crawled URL
  - bulk-read all documents (to rebuild the in-memory visited-set on startup)
  - fetch the single most recent document (to resume the frontier)
  - delete a single document by URL (to clear a resumed/duplicate entry)
  - dump all documents for debugging/inspection
- **Dependencies to install**: `mongocxx` and `bsoncxx` (via the MongoDB C++
  driver), plus the underlying `mongoc`/`libbson` C driver, and wiring these into
  the CMake build.

### Notes
-