#include <iostream> 
#include "processor/processor.h"
#include "drawing/drawing.h"
#include <opencv2/opencv.hpp>
#include <string>

int main(int argc, char* argv[]){
    std::cout << "Hi there" << std::endl;
    std::string url (argv[1]);
    std::cout << "Reading from " << url << std::endl;
    skyview::process_loop(url);
}