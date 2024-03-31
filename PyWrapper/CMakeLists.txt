find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 CONFIG)

pybind11_add_module(pysanderling MODULE sanderling.cpp)
target_link_libraries(pysanderling PRIVATE Python3::Python Python3::Module pybind11::module libsanderling_static)