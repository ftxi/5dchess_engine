#ifndef ENGINE_CLI_H
#define ENGINE_CLI_H
#include <iosfwd>
#include <memory>
#include "uci.h"
void print_engine_usage(std::ostream &out);
std::unique_ptr<engine> make_engine(int argc, const char *argv[], std::unique_ptr<io_handler> io);
#endif
