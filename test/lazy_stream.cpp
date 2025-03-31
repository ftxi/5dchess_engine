#include "stream.h"
#include <iostream>
#include <vector>

void foo()
{
    stream<int> nn = naturals();
    std::function<auto(int)->int> square = [](int x)->int{return x*x;};
    print_stream(map(square, take(1000,nn)));
}

void bar()
{
    std::vector<int> v;
    for(int i = 0; i < 20000; i++)
    {
        v.push_back(i*i);
        volatile int x = v[i];
    }
    for(int i = 0; i < 1000; i++)
    {
        std::cout << v[i] << " ";
    }
    std::cout << std::endl;
}

int main()
{
    foo();
    bar();
    return 0;
}
