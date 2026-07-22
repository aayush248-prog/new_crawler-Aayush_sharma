#pragma once

#include <string>

// Named 'node' (lowercase) deliberately — the project's own List.h
// declares a template Node<T> (capital N) used internally by
// Linkedlist<T>. Keeping this lowercase avoids the two colliding in
// any translation unit that includes both.
class node
{
public:
    int depth;
    std::string url;

    node()
    {
        depth = -1;
        url = "";
    }
};