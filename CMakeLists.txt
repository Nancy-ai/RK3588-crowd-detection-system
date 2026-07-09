cmake_minimum_required(VERSION 3.10)
project(yolov8_project C CXX)

# 设置 C++ 标准/C标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# 指定头文件路径
include_directories(include)

# 自动寻找源文件 (src 下所有的 .cpp)
file(GLOB SOURCES "src/*.c" "src/*.cpp" "third_party/bytetrack/src/*.cpp")

# 寻找 OpenCV 库
find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(GST REQUIRED
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-rtsp-server-1.0
)

#第三方库文件
link_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${GST_LIBRARY_DIRS}
)

# 添加可执行程序
add_executable(yolov8_project ${SOURCES})

include_directories(
    include 
    third_party/include   # <--- 新增这行，让 CMake 找到 im2d.h, RgaApi.h 等
    third_party/bytetrack/include
    /usr/include/eigen3
    ${GST_INCLUDE_DIRS}
)

# 链接库文件
target_link_libraries(yolov8_project
    ${OpenCV_LIBS}
    ${GST_LIBRARIES}
    pthread         # 多线程必须
    rknnrt          # 瑞芯微 NPU 库
    rga             # 瑞芯微 图像加速库
    m
)