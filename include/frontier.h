#pragma once

#include <stdexcept>

#include "List.h"
#include "Node.h"

class frontier
{
public:

    Linkedlist<node> ls;

    frontier();

    void push(node val);

    void pop();

    int size();

    node peek();

    bool isempty();

};

#include "Frontier.tpp"