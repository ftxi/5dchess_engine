#include "stream.h"
#include <iostream>
#include <vector>
#include <cmath>

stream<int> filter_prime(stream<int> s)
{
    return stream<int>(s.car(), [p=s.car(), rest=s.ptr_cdr()](){
        std::function<bool(int)> pndivides = [p](int x){
            return x % p != 0;
        };
        return std::make_shared<stream<int>>(filter_prime(filter(pndivides, *rest)));
    });
}

int main()
{
    stream<int> primes = filter_prime(naturals(2));
    
    for(auto x : take(1000,primes))
        std::cout << x << " ";
    std::cout << std::endl;
    return 0;
}
