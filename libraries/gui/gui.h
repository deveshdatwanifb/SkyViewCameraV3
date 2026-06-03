#pragma once
#include <opencv2/opencv.hpp>
#include <mutex>
#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QRadioButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTimer>
#include <QImage>
#include <QPixmap>

namespace skyview {

inline int run_gui_app(int argc, char* argv[], cv::Mat& mat, std::mutex& mtx2) {
    
    QApplication app(argc, argv);

    QWidget main_window;
    main_window.setWindowTitle("SkyView Qt Stream");
    main_window.resize(850, 480);

    QHBoxLayout* main_layout = new QHBoxLayout(&main_window);

    QLabel* video_window = new QLabel(&main_window);
    video_window->setFixedSize(640, 480);
    video_window->setStyleSheet("background-color: black;");
    main_layout->addWidget(video_window);

    QWidget* side_panel = new QWidget(&main_window);
    side_panel->setFixedWidth(180);
    QVBoxLayout* side_layout = new QVBoxLayout(side_panel);
    side_layout->setAlignment(Qt::AlignTop);

    QRadioButton* save_frame_radio = new QRadioButton("Save Frame", side_panel);
    side_layout->addWidget(save_frame_radio);

    main_layout->addWidget(side_panel);
    main_window.show();

    QTimer timer;
    
    QObject::connect(&timer, &QTimer::timeout, [&]() {
        
        cv::Mat local_frame;
        
        {
        
            std::unique_lock<std::mutex> lock(mtx2);
        
            if (!mat.empty()) {
        
                local_frame = mat.clone();
        
            }
        
        }

        if (!local_frame.empty()) {
            
            if (save_frame_radio->isChecked()) {
            
                cv::imwrite("saved_frame.png", local_frame);
                save_frame_radio->setChecked(false); 
            
            }

            cv::Mat rgb_frame;

            cv::cvtColor(local_frame, rgb_frame, cv::COLOR_BGR2RGB);

            QImage img((const unsigned char*)(rgb_frame.data), 
                       rgb_frame.cols, rgb_frame.rows, 
                       rgb_frame.step, QImage::Format_RGB888);

            video_window->setPixmap(QPixmap::fromImage(img.copy()).scaled(video_window->size(), Qt::KeepAspectRatio));
    
        }
    
    });
    
    timer.start(30);

    return app.exec();
}

}
