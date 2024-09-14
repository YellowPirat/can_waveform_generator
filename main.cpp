#include <iostream>
#include "CanGen.h"
#include <string>

int main(int argc, char **argv) {
    std::cout << "CanGen v0.1" << std::endl;

    CanGen canGen;

    if (argc > 1) {
        std::string file_name = "../config/";
        file_name += argv[1];
        canGen.importBaseConfig(file_name);
        canGen.init();
    }
    
    return 0;
}
