#ifndef VARIANTS_H
#define VARIANTS_H

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>
#include "pgnparser.h"

class multiverse;

struct variant_setup_t
{
    int size_x;
    int size_y;
    std::vector<std::tuple<std::string, pgnparser_ast::token_t, int, int, bool>> boards;
    bool is_even_timelines;
};

extern const std::map<std::string, variant_setup_t> default_variants;

variant_setup_t derive_variant_setup(const pgnparser_ast::game& g);
std::unique_ptr<multiverse> create_multiverse_from_variant_setup(const variant_setup_t& variant_setup);


#endif // VARIANTS_H
