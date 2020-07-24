#include <iostream>
#include <memory>

class testclass{
public:
    testclass(int i):id(i){}
    int id;
};

struct Deleter {
    void operator()(testclass *l) const { l->id --; }
};

int main(){
    using myp =  std::unique_ptr<testclass,Deleter> ;
    testclass c1 =  testclass(1);
    testclass c2 =  testclass(2);
    {
        myp p1 = myp(&c1);
        myp p2 = myp (&c1);
        std::cout<< p1.get()->id <<std::endl;
        //myp p2 = std::move(p1);
        std::cout<< p2.get()->id <<std::endl;
        testclass * tmp = &c1;
        std::cout<< tmp->id << std::endl;
    }
    std::cout<< c1.id <<std::endl;
    std::cout<< c2.id <<std::endl;
}
