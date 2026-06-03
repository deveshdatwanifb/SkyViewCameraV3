#pragma once
#include <opencv2/opencv.hpp>
#include <mutex>
#include <string>
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

class ProcessorGui : public QWidget {

public:

    ProcessorGui(cv::Mat& shared_mat, std::mutex& shared_mutex) : m_mat(shared_mat), m_mutex(shared_mutex) {

        setWindowTitle("SkyView Qt Stream");
        resize(850, 480);

        QHBoxLayout* main_layout = new QHBoxLayout(this);
        
        m_video_label = new QLabel(this);
        m_video_label->setFixedSize(640, 480);
        m_video_label->setStyleSheet("background-color: black;");
        main_layout->addWidget(m_video_label);

        QWidget* side_panel = new QWidget(this);
        side_panel->setFixedWidth(180);
        QVBoxLayout* side_layout = new QVBoxLayout(side_panel);
        side_layout->setAlignment(Qt::AlignTop);

        m_save_radio = new QRadioButton("Save Frame", side_panel);
        side_layout->addWidget(m_save_radio);
        main_layout->addWidget(side_panel);

        QObject::connect(&m_timer, &QTimer::timeout, [this]() {
          
            update_frame();
        
        });
        
        m_timer.start(30);

    }

private:
    
    void update_frame() {
        
        cv::Mat local_frame;
        
        {
        
            std::unique_lock<std::mutex> lock(m_mutex);
        
            if (!m_mat.empty()) {
        
                local_frame = m_mat.clone();
        
            }
        
        }

        if (!local_frame.empty()) {
            
            if (m_save_radio->isChecked()) {
            
                cv::imwrite("saved_frame.png", local_frame);
                m_save_radio->setChecked(false); 
            
            }

            cv::Mat rgb_frame;
            cv::cvtColor(local_frame, rgb_frame, cv::COLOR_BGR2RGB);

            QImage img((const unsigned char*)(rgb_frame.data), rgb_frame.cols, rgb_frame.rows, rgb_frame.step, QImage::Format_RGB888);

            m_video_label->setPixmap(QPixmap::fromImage(img.copy()).scaled(m_video_label->size(), Qt::KeepAspectRatio));
        
        }
    
    }

    cv::Mat& m_mat;
    std::mutex& m_mutex;
    QTimer m_timer;
    QLabel* m_video_label;
    QRadioButton* m_save_radio;

};

}
