#ifndef LLST_ARGS_H_INCLUDED
#define LLST_ARGS_H_INCLUDED

#include <cstddef>
#include <string>

struct args
{
    std::size_t heapSize;
    std::size_t maxHeapSize;
    std::string imagePath;
    int         showHelp;
    args() :
        heapSize(0), maxHeapSize(0), showHelp(false)
    {
    }
    void parse(int argc, char **argv);
};

#endif