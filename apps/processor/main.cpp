#include <iostream> 
// #include <processor/processor.h>
// #include <processor/gui.h>
// #include <gui/gui.h>
// #include <opencv2/opencv.hpp>
// #include <mutex>
// #include <thread>
#include <QApplication>
#include <QPushButton>

// int main(int argc, char* argv[]) {

//     if (argc < 2) {

//         return -1;

//     }

//     cv::Mat mat;
//     std::mutex mtx2;
//     skyview::Processor processor(argv[1]);
//     std::thread processor_thread(&skyview::Processor::process_loop, &processor, std::ref(mat), std::ref(mtx2));

//     QApplication app(argc, argv);
//     skyview::ProcessorGui processorGui(mat, mtx2);
//     processorGui.show();    
//     app.exec();


//     if (processor_thread.joinable()) {
        
//         processor_thread.join();
    
//     }

//     return 1;
// }

void clickedButton() {
        std::cout << "Button pressed" << std::endl;
}

int main(int argc, char **argv) {

    QApplication app(argc, argv);
    QWidget* newApp = new QWidget();
    newApp->resize(600,600);
    QPushButton* button1 = new QPushButton("testButton", newApp);
    button1->resize(100,100);
    QPushButton button2 ("testButton2", button1);
    QAbstractButton::connect(button1, &QPushButton::clicked, clickedButton);
    newApp->show();

    return app.exec();

}