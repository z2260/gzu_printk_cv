#include <iostream>
#include <opencv2/opencv.hpp>
#include "sensor/camera/hik_robot/hik_robot.hpp"

int main() {
    using namespace sensor::camera;

    constexpr HikRobotModel model = HikRobotModel::MV_CS016_10UC;
    HikRobot<model> cam0;
    HikRobot<model> cam1;

    if (!cam0.init() || !cam0.open(0)) {
        std::cerr << "Failed to init/open camera 0" << std::endl;
        return -1;
    }
    if (!cam1.init() || !cam1.open(1)) {
        std::cerr << "Failed to init/open camera 1" << std::endl;
        return -1;
    }

    cam0.set_exposure_time(10000.0f);
    cam0.set_gain(10.0f);
    cam1.set_exposure_time(10000.0f);
    cam1.set_gain(10.0f);

    if (!cam0.start_capture() || !cam1.start_capture()) {
        std::cerr << "Failed to start capture" << std::endl;
        return -1;
    }

    std::vector<uint8_t> frame0, frame1;
    while (true) {
        bool ok0 = cam0.get_data(frame0);
        bool ok1 = cam1.get_data(frame1);

        if (ok0 && frame0.size() == 1440 * 1080 * 3) {
            cv::Mat img0(cv::Size(1440, 1080), CV_8UC3, frame0.data());
            cv::imshow("Camera 0", img0);
        } else {
            std::cerr << "Camera 0 collection failed" << std::endl;
        }

        if (ok1 && frame1.size() == 1440 * 1080 * 3) {
            cv::Mat img1(cv::Size(1440, 1080), CV_8UC3, frame1.data());
            cv::imshow("Camera 1", img1);
        } else {
            std::cerr << "Camera 1 collection failed" << std::endl;
        }

        if (cv::waitKey(1) == 27) {
            break;
        }
    }

    cam0.stop_capture();
    cam0.close();
    cam1.stop_capture();
    cam1.close();

    cv::destroyAllWindows();
    return 0;
}