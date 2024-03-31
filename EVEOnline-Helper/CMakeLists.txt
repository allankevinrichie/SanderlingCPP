# CMakeList.txt: EVEOnline-Helper 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#

set(Boost_NO_WARN_NEW_VERSIONS 1)

# list of source files
set(libsrc
      ProcessMemoryReader.h
      EVEOnlineReader.cpp
      EVEOnlineReader.h
      PythonMemoryReader.h
        common.h

)

# this is the "object library" target: compiles the sources only once
add_library(objlib OBJECT ${libsrc})

# shared libraries need PIC
set_property(TARGET objlib PROPERTY POSITION_INDEPENDENT_CODE 1)
set_property(TARGET objlib PROPERTY CXX_STANDARD 23)
target_link_libraries(objlib PRIVATE loguru::loguru Boost::boost Boost::thread)

# shared and static libraries built from the same object files
add_library(libsanderling_shared SHARED $<TARGET_OBJECTS:objlib>)
target_link_libraries(libsanderling_shared PRIVATE loguru::loguru Boost::boost Boost::thread)
add_library(libsanderling_static STATIC $<TARGET_OBJECTS:objlib>)

#target_link_libraries(EVEOnline-Helper PRIVATE ${LOGURU_INCLUDE_DIRS})
#target_link_libraries(libsanderling_static PRIVATE loguru::loguru Boost::boost Boost::thread)

target_include_directories(libsanderling_shared PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(libsanderling_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})


# TODO: 如有需要，请添加测试并安装目标。
