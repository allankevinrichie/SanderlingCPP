# CMakeList.txt: EVEOnline-Helper 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#

# 将源代码添加到此项目的可执行文件。
add_executable (Test "EVEOnline-Helper.cpp" "EVEOnline-Helper.h")
target_link_libraries(Test PRIVATE loguru::loguru Boost::boost Boost::thread libsanderling_static ${PYTHON_LIBRARIES})
set_property(TARGET Test PROPERTY CXX_STANDARD 23)

# TODO: 如有需要，请添加测试并安装目标。
