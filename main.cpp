#include <iostream>
#include <chrono>
#include <thread>
#include <opencv2/opencv.hpp>

#include "sensor/camera/virtual_camera/virtual_camera.hpp"

// 用于显示帧率的辅助函数
void displayFPS(cv::Mat& frame, double fps) {
    std::stringstream ss;
    ss << "FPS: " << std::fixed << std::setprecision(1) << fps;
    cv::putText(frame, ss.str(), cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);
}

int main() {
    {
        std::cout << "示例 1: 从图像文件创建虚拟相机" << std::endl;

        sensor::camera::ImageCamera image_camera(PROJECT_ASSETS_DIR "/image/test_image_01.jpg");

        if (!image_camera.init()) {
            std::cerr << "无法初始化图像相机" << std::endl;
            return -1;
        }

        if (!image_camera.open()) {
            std::cerr << "无法打开图像文件" << std::endl;
            return -1;
        }

        std::pair<int, int> resolution = {640, 480};
        if (!image_camera.set_resolution(resolution)) {
            std::cerr << "无法设置分辨率" << std::endl;
        }

        if (!image_camera.start_capture()) {
            std::cerr << "无法开始捕获" << std::endl;
            return -1;
        }

        if (!image_camera.is_captured()) {
            std::cerr << "<UNK>" << std::endl;
        }

        cv::Mat frame;
        if (image_camera.get_frame(frame)) {
            cv::imwrite("image_camera_output.jpg", frame);
            std::cout << "Image saved to image_camera_output.jpg" << std::endl;
        }

        image_camera.close();
    }
    //
    // // 2. 视频源示例
    // {
    //     std::cout << "示例 2: 从视频文件或摄像头创建虚拟相机" << std::endl;
    //
    //     // 创建一个从视频文件或摄像头读取的虚拟相机
    //     // 可以是文件路径或摄像头索引 (例如 "0" 表示第一个摄像头)
    //     sensor::camera::VideoCamera video_camera("0");
    //
    //     // 初始化并打开相机
    //     if (!video_camera.init()) {
    //         std::cerr << "无法初始化视频相机" << std::endl;
    //         return -1;
    //     }
    //
    //     if (!video_camera.open()) {
    //         std::cerr << "无法打开视频源" << std::endl;
    //         return -1;
    //     }
    //
    //     // 设置分辨率和帧率
    //     std::pair<int, int> resolution = {1280, 720};
    //     video_camera.set_resolution(resolution);
    //
    //     int fps = 30;
    //     video_camera.set_max_frame_rate(fps);
    //
    //     // 开始捕获
    //     if (!video_camera.start_capture()) {
    //         std::cerr << "无法开始捕获" << std::endl;
    //         return -1;
    //     }
    //
    //     // 帧率计算变量
    //     int frame_count = 0;
    //     auto start_time = std::chrono::steady_clock::now();
    //     double current_fps = 0.0;
    //
    //     // 捕获和显示循环
    //     while (true) {
    //         cv::Mat frame;
    //         if (video_camera.get_frame(frame)) {
    //             if (frame.empty()) {
    //                 std::cerr << "空帧" << std::endl;
    //                 break;
    //             }
    //
    //             // 计算帧率
    //             frame_count++;
    //             auto current_time = std::chrono::steady_clock::now();
    //             auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
    //                 current_time - start_time).count();
    //
    //             if (elapsed >= 1) {
    //                 current_fps = frame_count / (double)elapsed;
    //                 frame_count = 0;
    //                 start_time = current_time;
    //             }
    //
    //             // 显示帧率
    //             displayFPS(frame, current_fps);
    //
    //             // 显示帧
    //             cv::imshow("Video Camera", frame);
    //
    //             // 按ESC退出
    //             int key = cv::waitKey(1);
    //             if (key == 27) { // ESC键
    //                 break;
    //             }
    //         }
    //     }
    //
    //     // 停止捕获并关闭相机
    //     video_camera.stop_capture();
    //     video_camera.close();
    //     cv::destroyAllWindows();
    // }
    //
    // // 3. 高级用法：同时使用两个相机源
    // {
    //     std::cout << "示例 3: 同时使用两个相机源" << std::endl;
    //
    //     sensor::camera::ImageCamera image_camera("path/to/background.jpg");
    //     sensor::camera::VideoCamera video_camera("0");
    //
    //     if (!image_camera.init() || !video_camera.init() ||
    //         !image_camera.open() || !video_camera.open()) {
    //         std::cerr << "无法初始化或打开相机" << std::endl;
    //         return -1;
    //     }
    //
    //     // 设置相同的分辨率以便合成
    //     std::pair<int, int> resolution = {640, 480};
    //     image_camera.set_resolution(resolution);
    //     video_camera.set_resolution(resolution);
    //     video_camera.set_max_frame_rate(15); // 降低帧率以减少处理负担
    //
    //     image_camera.start_capture();
    //     video_camera.start_capture();
    //
    //     // 获取背景图片
    //     cv::Mat background;
    //     image_camera.get_frame(background);
    //
    //     while (true) {
    //         cv::Mat frame;
    //         if (video_camera.get_frame(frame)) {
    //             // 创建合成画面：将视频叠加在图像上
    //             cv::Mat result;
    //             cv::addWeighted(frame, 0.7, background, 0.3, 0, result);
    //
    //             cv::imshow("Combined View", result);
    //
    //             int key = cv::waitKey(1);
    //             if (key == 27) { // ESC键
    //                 break;
    //             }
    //         }
    //     }
    //
    //     video_camera.stop_capture();
    //     image_camera.stop_capture();
    //     video_camera.close();
    //     image_camera.close();
    //     cv::destroyAllWindows();
    // }

    return 0;
}