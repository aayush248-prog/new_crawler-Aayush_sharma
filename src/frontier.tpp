inline frontier::frontier() {}

inline void frontier::push(node val)
{
    ls.push(val);
}

inline void frontier::pop()
{
    ls.pop_front();
}

inline int frontier::size()
{
    return ls.Size();
}

inline node frontier::peek()
{
    if (ls.isempty())
        throw std::out_of_range("peek() called on empty frontier");

    return ls.begin()->value;
}

inline bool frontier::isempty()
{
    return ls.isempty();
}
