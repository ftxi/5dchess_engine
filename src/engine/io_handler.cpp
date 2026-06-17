#include "io_handler.h"

#include <iostream>

std::string stdio_handler::read_line()
{
    std::string line;
    std::getline(std::cin, line);
    return line;
}

void stdio_handler::write_line(const std::string &line)
{
    std::cout << line << std::endl;
}

bool stdio_handler::is_open()
{
    return static_cast<bool>(std::cin) && static_cast<bool>(std::cout);
}
