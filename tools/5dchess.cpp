// Accessing the 5dchess engines.
#include <iostream>
#include <string_view>
#include "engine_cli.h"

int main(int argc, const char *argv[])
{
    if(argc == 2 && (std::string_view(argv[1]) == "-h"
                     || std::string_view(argv[1]) == "--help"))
    {
        print_engine_usage(std::cout);
        return 0;
    }
    try
    {
        auto selected = make_engine(argc, argv, std::make_unique<stdio_handler>());
        selected->mainloop();
        return 0;
    }
    catch(const std::exception &error)
    {
        std::cerr << "Error: " << error.what() << '\n';
        print_engine_usage(std::cerr);
        return 2;
    }
}
