#pragma once
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <algorithm>

namespace skyview {

struct Detection {
    cv::Rect box;
    int class_id;
    float confidence;
};

inline void drawBoundingBoxes (cv::Mat& image, 
                                const std::vector<Detection>& detections, 
                                const std::vector<std::string>& class_names = {}) {
    
    for (const auto& detection : detections) {
                                    cv::Scalar color = cv::Scalar((detection.class_id * 50) % 256, 
                                    (detection.class_id * 80) % 256, 
                                    (detection.class_id * 120) % 256);

        cv::rectangle(image, detection.box, color, 2);

        std::string label = class_names.empty() || detection.class_id >= class_names.size() ? 
                            "Class " + std::to_string(detection.class_id) : 
                            class_names[detection.class_id];
        
        label += " " + std::to_string(detection.confidence).substr(0, 4);

        int baseLine;
        cv::Size label_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
        
        int top = std::max(detection.box.y, label_size.height);
        cv::Point text_bg_top_left(detection.box.x, top - label_size.height - 2);
        cv::Point text_bg_bottom_right(detection.box.x + label_size.width + 2, top + baseLine);

        cv::rectangle(image, text_bg_top_left, text_bg_bottom_right, color, cv::FILLED);
        cv::putText(image, label, cv::Point(detection.box.x + 1, top), 
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
    
    }
}


void draw_time (cv::Mat frame, std::string timestamp) 
{

    int fontFace = cv::FONT_HERSHEY_SIMPLEX;
    double fontScale = 0.5;
    int thickness = 1;
    cv::Point org(10, 50);

    int baseline = 0;
    cv::Size textSize = cv::getTextSize(timestamp, fontFace, fontScale, thickness, &baseline);

    cv::Point topLeft(org.x - 5, org.y - textSize.height - 5);
    cv::Point bottomRight(org.x + textSize.width + 5, org.y + baseline + 5);

    cv::rectangle(frame, topLeft, bottomRight, cv::Scalar(255, 255, 255), cv::FILLED);
    cv::putText(frame, timestamp, org, fontFace, fontScale, cv::Scalar(0, 0, 0), thickness, cv::LINE_AA);

}

void draw_fps(cv::Mat frame, double fps) 
{

    std::stringstream ss;
    ss << "FPS: " << std::fixed << std::setprecision(1) << fps;
    std::string fps_text = ss.str();

    int fontFace = cv::FONT_HERSHEY_SIMPLEX;
    double fontScale = 0.5;
    int thickness = 1;
    cv::Point org(10, 75);

    int baseline = 0;
    cv::Size textSize = cv::getTextSize(fps_text, fontFace, fontScale, thickness, &baseline);

    cv::Point topLeft(org.x - 5, org.y - textSize.height - 5);
    cv::Point bottomRight(org.x + textSize.width + 5, org.y + baseline + 5);

    cv::rectangle(frame, topLeft, bottomRight, cv::Scalar(255, 255, 255), cv::FILLED);
    cv::putText(frame, fps_text, org, fontFace, fontScale, cv::Scalar(0, 0, 0), thickness, cv::LINE_AA);

}

}