#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <set>
#include <vector>

#include "integer_set.h"
#include "utils.h"


template <typename T>
std::vector<uint32_t> snapshot(const T &values)
{
    return std::vector<uint32_t>(values.begin(), values.end());
}

int main()
{
    integer_set s{1,2,3,5,64,65,98};
    std::set<uint32_t> _s{1,2,3,5,64,65,98};
    print_range("s: ", s);
    for(uint32_t i = 0; i < 16; i++)
    {
        std::cout << "Does s contain " << i << "? ";
        std::cout << s.contains(i) << "\n";
    }
    if(snapshot(s) != snapshot(_s))
    {
        std::cerr << "Test failed: snapshot does not match reference\n";
        return 1;
    }
    s.erase(3);
    _s.erase(3);
    print_range("s after erase 3: ", s);
    if(snapshot(s) != snapshot(_s))
    {
        std::cerr << "Test failed after erase\n";
        print_range("reference: ", _s);
        return 1;
    }
    s.insert(7);
    _s.insert(7);
    print_range("s after insert 7: ", s);
    if(snapshot(s) != snapshot(_s))
    {
        std::cerr << "Test failed after insert\n";
        print_range("reference: ", _s);
        return 1;
    }
    integer_set t{5,6,7,64,128};
    std::set<uint32_t> _t{5,6,7,64,128};
    s.minus(t);
    print_range("s after minus t: ", s);
    for(uint32_t value : _t)
    {
        _s.erase(value);
    }
    if(snapshot(s) != snapshot(_s))
    {
        std::cerr << "Test failed after set minus\n";
        print_range("reference: ", _s);
        return 1;
    }
    integer_set u = s.transform([](uint32_t x) { return x * x; });
    std::set<uint32_t> _u;
    for(uint32_t value : snapshot(s))
    {
        _u.insert(value * value);
    }
    print_range("u = s transform by square = ", u);
    if(snapshot(u) != snapshot(_u))
    {
        std::cerr << "Test failed after transform\n";
        print_range("reference: ", _u);
        return 1;
    }
    u.erase_if([](uint32_t x) { return x % 2 == 0; });
    print_range("u after erase_if even: ", u);
    std::erase_if(_u, [](uint32_t x) { return x % 2 == 0; });
    if(snapshot(u) != snapshot(_u))
    {
        std::cerr << "Test failed after erase_if\n";
        print_range("reference: ", _u);
        return 1;
    }
    u |= integer_set{1, 2, 3, 200, 300};
    _u.insert({1, 2, 3, 200, 300});
    print_range("u after union with {1,2,3,200,300}: ", u);
    if(snapshot(u) != snapshot(_u))    {
        std::cerr << "Test failed after union\n";
        print_range("reference: ", _u);
        return 1;
    }
    u &= integer_set{2, 3, 4, 200};
    std::erase_if(_u, [](uint32_t x) { return !(x == 2 || x == 3 || x == 4 || x == 200); });
    print_range("u after intersection with {2,3,4,200}: ", u);
    if(snapshot(u) != snapshot(_u))
    {   
        std::cerr << "Test failed after intersection\n";
        print_range("reference: ", _u);
        return 1;
    }
    std::cerr << "---= integer_set.cpp: all passed =---" << std::endl;
    return 0;
}
