#include <iostream>
#include <memory>
#include <list>

int main(){
    std::list<int> mylist{ 1,2,3,4,5 };
    auto const a = std::prev(mylist.end());
    //获取一个距离 it 迭代器 2 个元素的迭代器，由于 2 为正数，newit 位于 it 左侧

    std::cout << *a << std::endl;
}
