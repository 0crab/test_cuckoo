
#include <iostream>
#include <string>

#include <libcuckoo/cuckoohash_map.hh>

int main() {
    libcuckoo::cuckoohash_map<int, std::string> Table;

    for (int i = 0; i < 100; i++) {
        std::string a = "hello" + std::to_string(i);
        Table.insert(i,a);
    }

    for (int i = 0; i < 101; i++) {
        std::string out;


        if (Table.find_free(i, out)) {
            std::cout << i << "  " << out << std::endl;
        } else {
            std::cout << i << "  NOT FOUND" << std::endl;
        }
    }

}
