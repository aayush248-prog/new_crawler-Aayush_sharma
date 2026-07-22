# Work Log — July 22, 2026 (11:00–13:00)

## Task: MongoDB Integration

- Integrated MongoDB into the crawler (via `mongocxx`/`bsoncxx`) for persisting crawled URLs and depth.
- Wired up `Database` class methods (insert, get, getlast, deleteOne, print).

## Concept: Frontier

BFS work queue of URLs still to crawl, backed by `Linkedlist<node>`:

```cpp
class frontier {
public:
    Linkedlist<node> ls;

    void push(node val) { ls.push(val); }
    void pop()           { ls.pop_front(); }
    int  size()          { return ls.Size(); }
    node peek() {
        if (ls.isempty())
            throw std::out_of_range("peek() called on empty frontier");
        return ls.begin()->value;
    }
    bool isempty() { return ls.isempty(); }
};
```

Each `node` carries `{url, depth}`. `crawler::start()` pops the next node, checks its
depth cap, and — if not already visited — crawls it and pushes its outgoing links
back onto the frontier at `depth + 1`.

## Concept: Seekstore

Visited-set for de-duplication, backed by `HashMap<string, node>` keyed on URL:

```cpp
class seekstore {
public:
    HashMap<string, node> hs;

    void push(string val, int depth) {
        if (hs.find(val) != NULL) return;   // already seen — skip
        node ns;
        ns.depth = depth;
        ns.url = val;
        hs.push(val, ns);
    }
    void pop(string val)   { hs.pop(val); }
    bool exists(string val){ return hs.find(val) != NULL; }
};
```

Before a URL is pushed onto the frontier, `seekstore::exists()` is checked so the
same page isn't queued (or crawled) twice.

## Wiring Frontier + Seekstore to MongoDB

MongoDB is the persistence layer behind both, so a crawl can be interrupted and
resumed:

- **On startup**, `Database::get(seekstore &hm)` replays every stored `{url, depth}`
  document into `seekstore`, restoring the full visited-set before crawling resumes.
- **On startup**, `Database::getlast(frontier &fr)` fetches the single most-recently
  inserted document (sorted by `_id` descending, `limit(1)`) and pushes it onto the
  `frontier`, so the crawl restarts from the last URL it was working on instead of
  the seed.
- **During the crawl**, every URL popped from the `frontier` is immediately
  `db.insert(url, depth)`'d before it's fetched, so MongoDB always reflects the
  crawl's progress even if the process dies mid-page.
- After a resumed run re-queues the last node, `db.deleteOne(url)` removes that
  duplicate entry so it isn't inserted twice.

## Errors Encountered

- **Incomplete-type ordering**: `Database::get(seekstore&)` and
  `Database::getlast(frontier&)` take `seekstore`/`frontier` by reference, but those
  classes aren't defined yet at the point `Database` is declared (and `Database` is
  needed before `frontier`/`seekstore` are fully usable). Fixed by forward-declaring
  `class seekstore;` / `class frontier;` above `Database`, declaring the two methods
  there, and only *defining* them after both classes are complete.
- **`mongocxx::instance` double-construction**: it's a process-wide singleton and
  throws if constructed more than once. Had to make sure it's created exactly once
  in `main()` — not inside `crawler::start()`, which runs per-crawl and would
  otherwise try to reconstruct it.
- **`peek()` on an empty frontier**: originally had no guard, which meant popping
  past the end of an empty `Linkedlist` and hitting undefined behavior. Added an
  explicit `std::out_of_range` throw in `frontier::peek()`.
- **`HashMap::find()` returning `NULL` vs. a real pointer**: `seekstore::exists()`
  and `push()` both rely on comparing `hs.find(val)` against `NULL` to detect
  "already seen" — got this backwards once early on and ended up re-crawling
  duplicate URLs before catching it.
- **`db.get(ss)` popping the last-seen URL back out**: after replaying all stored
  docs into `seekstore`, the loop immediately calls `hm.pop(url1)` on the last URL
  read from the cursor — deliberately so that URL is treated as "not yet visited"
  and gets picked up again by `getlast()`/re-queued, rather than being skipped as
  already-seen.

### Notes
-