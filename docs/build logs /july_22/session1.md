# Work Log — July 22, 2026 (10:00–11:00)

## Task: MongoDB Connection + Retrieval in C++

### 1. Connection setup (`Database` class)

Set up the MongoDB connection using the `mongocxx` driver — one `mongocxx::client` per
`Database` instance, pointed at a local MongoDB, with the database fixed and the
collection named per crawled site:

```cpp
class Database {
public:
    mongocxx::client client;
    mongocxx::database db;
    mongocxx::collection collection;

    Database(string& url)
        : client(mongocxx::uri{"mongodb://localhost:27017"}),
          db(client["new_crawler_base"]),
          collection(db[url])
    {
    }

    void insert(const std::string& url, int depth)
    {
        using bsoncxx::builder::basic::kvp;
        using bsoncxx::builder::basic::make_document;

        auto value = make_document(
            kvp("url", url),
            kvp("depth", depth)
        );

        collection.insert_one(value.view());
    }
};
```

- `mongocxx::instance` is a process-wide singleton and must be constructed exactly
  once, before any `mongocxx::client` — done once in `main()`.
- Collection name is derived per-site from the seed URL (protocol/path stripped,
  `.` replaced with `_`) via `crawler::getCollectionName()`.

### 2. Retrieval

Two read paths off the collection:

- **`Database::get(seekstore &hm)`** — pulls every stored `{url, depth}` document and
  rebuilds the `seekstore` (visited-set) from it, so a restart doesn't re-crawl
  already-seen pages.

```cpp
void Database::get(seekstore & hm)
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
```

- **`Database::getlast(frontier &fr)`** — sorts by `_id` descending with `limit(1)` to
  fetch the most recently inserted document, and pushes it back onto the `frontier`
  so the crawl can resume from where it left off.

```cpp
bool Database::getlast(frontier & fr)
{
    mongocxx::options::find opts{};
    opts.sort(make_document(kvp("_id", -1)));
    opts.limit(1);

    auto cursor = collection.find({}, opts);

    for (auto&& doc : cursor)
    {
        node n;
        n.url   = string(doc["url"].get_string().value);
        n.depth = doc["depth"].get_int32().value;
        fr.push(n);
        return true;
    }

    return false;
}
```

### 3. Also added
- `Database::print()` — dumps every stored document as JSON for quick inspection.
- `Database::deleteOne(url)` — removes a single document by URL (used to drop the
  resumed "last" entry once it's been re-queued into the frontier).

### Notes
-