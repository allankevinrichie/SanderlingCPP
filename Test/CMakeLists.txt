add_executable (Test "test.cpp" "test.h")
target_link_libraries(Test PRIVATE loguru::loguru Boost::boost Boost::thread libsanderling_static)
set_property(TARGET Test PROPERTY CXX_STANDARD 23)