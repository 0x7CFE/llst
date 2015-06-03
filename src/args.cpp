/*
 *    args.cpp
 *
 *    Helper functions for command line argument parsing
 *
 *    LLST (LLVM Smalltalk or Low Level Smalltalk) version 0.3
 *
 *    LLST is
 *        Copyright (C) 2012-2015 by Dmitry Kashitsyn   <korvin@deeptown.org>
 *        Copyright (C) 2012-2015 by Roman Proskuryakov <humbug@deeptown.org>
 *
 *    LLST is based on the LittleSmalltalk which is
 *        Copyright (C) 1987-2005 by Timothy A. Budd
 *        Copyright (C) 2007 by Charles R. Childers
 *        Copyright (C) 2005-2007 by Danny Reinhold
 *
 *    Original license of LittleSmalltalk may be found in the LICENSE file.
 *
 *
 *    This file is part of LLST.
 *    LLST is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    LLST is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with LLST.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <args.h>
#include <getopt.h>
#include <iostream>
#include <cstdlib>
#include <sstream>

void args::parse(int argc, char **argv)
{
    enum {
        help = 'S',
        version = 'V',
        image = 'i',
        heap_max = 'H',
        heap = 'h',
        mm_type = 'm',

        getopt_set_arg = 0,
        getopt_err = '?',
        getopt_end = -1
    };

    const option long_options[] =
    {
        {"heap_max",   required_argument, 0, heap_max},
        {"heap",       required_argument, 0, heap},
        {"image",      required_argument, 0, image},
        {"mm_type",    required_argument, 0, mm_type},
        {"help",       no_argument,       0, help},
        {"version",    no_argument,       0, version},
        {0, 0, 0, 0}
    };

    const char* short_options = "Vh:H:i:";
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
            case mm_type: {
                memoryManagerType = optarg;
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
            case version: {
                showVersion = true;
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

std::string args::getHelp()
{
    return
        "Usage: llst [options] path-to-image\n"
        "Options:\n"
        "  -h, --heap <number>              Starting <number> of the heap in bytes\n"
        "  -H, --heap_max <number>          Maximum allowed heap size\n"
        "  -i, --image <path>               Path to image\n"
        "      --mm_type arg (=copy)        Choose memory manager. nc - NonCollect, copy - Stop-and-Copy\n"
        "  -V, --version                    Display the version number and copyrights of the invoked LLST\n"
        "      --help                       Display this information and quit";
}

std::string args::getVersion()
{
    return
        "llst 0.3.0\n"
        "Copyright (C) 2012-2015 by Dmitry Kashitsyn   <korvin@deeptown.org>\n"
        "Copyright (C) 2012-2015 by Roman Proskuryakov <humbug@deeptown.org>\n"
        "This is free software; see the source for copying conditions.  There is NO\n"
        "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.";
}
