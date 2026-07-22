inline seekstore::seekstore() {}

inline void seekstore::push(std::string val, int depth)
{
    if (hs.find(val) != NULL)
        return;

    node ns;
    ns.depth = depth;
    ns.url = val;
    hs.push(val, ns);
}

inline void seekstore::pop(std::string val)
{
    hs.pop(val);
}

inline bool seekstore::exists(std::string val)
{
    return hs.find(val) != NULL;
}

inline node* seekstore::get(std::string val)
{
    HashNode<std::string, node>* n = hs.find(val);
    if (n == NULL) return NULL;
    return &n->Value;
}
