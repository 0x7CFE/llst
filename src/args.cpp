#include <args.h>
#include <getopt.h>
#include <iostream>
#include <cstdlib>
#include <sstream>

void args::parse(int argc, char **argv)
{
    enum {
        help = 'S',
        image = 'i',
        heap_max = 'H',
        heap = 'h',
        
        getopt_set_arg = 0,
        getopt_err = '?',
        getopt_end = -1
    };
    
    const option long_options[] =
    {
        {"heap_max",   required_argument, 0, heap_max},
        {"heap",       required_argument, 0, heap},
        {"image",      required_argument, 0, image},
        {"help",       no_argument,       0, help},
        {0, 0, 0, 0}
    };
    
    const char* short_options = "h:H:i:";
    int option_index = -1;
    
    while(true)
    {
        int c = getopt_long(argc, argv, short_options, long_options, &option_index);
        switch(c)
        {
            case getopt_end: break;
            case getopt_err: std::exit(1);
            case getopt_set_arg: break;
            
            case image: {
                imagePath = optarg;
            } break;
            case heap: {
                bool good_number = std::istringstream( optarg ) >> heapSize;
                if (!good_number)
                {
                    std::cerr << "A malformed number is given for argument heap_size" << std::endl;
                    std::exit(1);
                }
            } break;
            case heap_max: {
                bool good_number = std::istringstream( optarg ) >> maxHeapSize;
                if (!good_number)
                {
                    std::cerr << "A malformed number is given for argument max_heap_size" << std::endl;
                    std::exit(1);
                }
            } break;
            case help: {
                showHelp = true;
            } break;
        }
        if (c == getopt_end) {
            //We are out of options. Now we have to take the last argument as the imagePath
            if (optind < argc)
                imagePath = argv[argc-1];
            break;
        }
    }
}
