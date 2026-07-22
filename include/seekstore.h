#pragma once

#include <string>

#include <Hashmap.h>
#include "Node.h"

class seekstore
{
public:

    HashMap<std::string, node> hs;

    seekstore();

    void push(std::string val, int depth);

    void pop(std::string val);

    bool exists(std::string val);

    node* get(std::string val);

};

#include "SeekStore.tpp"