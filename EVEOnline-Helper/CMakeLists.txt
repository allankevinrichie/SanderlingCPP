# CMakeList.txt: EVEOnline-Helper 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#

include(FetchContent)
#add_subdirectory(libce)
FetchContent_Declare(LoguruGitRepo
        GIT_REPOSITORY "https://github.com/emilk/loguru" # can be a filesystem path
        GIT_TAG        "master"
)
# set any loguru compile-time flags before calling MakeAvailable()
set(LOGURU_WITH_STREAMS TRUE)
FetchContent_MakeAvailable(LoguruGitRepo) # defines target 'loguru::loguru'

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 CONFIG)
set(Boost_NO_WARN_NEW_VERSIONS 1)
find_package(Boost COMPONENTS thread REQUIRED)

# 将源代码添加到此项目的可执行文件。
add_executable (EVEOnline-Helper "EVEOnline-Helper.cpp" "EVEOnline-Helper.h"
        ProcessMemoryReader.h
        EVEOnlineReader.cpp
        EVEOnlineReader.h
        PythonMemoryReader.h
)
#target_link_libraries(EVEOnline-Helper PRIVATE ${LOGURU_INCLUDE_DIRS})
target_link_libraries(EVEOnline-Helper PRIVATE loguru::loguru pybind11::module Boost::boost Boost::thread ${PYTHON_LIBRARIES})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET EVEOnline-Helper PROPERTY CXX_STANDARD 23)
endif()

# TODO: 如有需要，请添加测试并安装目标。
