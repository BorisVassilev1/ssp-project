cmake_minimum_required(VERSION 3.25)
project(TaskSystemExample)

SET(CMAKE_INSTALL_PREFIX ./ts_executors/)

SET(PLUGIN_INSTALL_PATH "${CMAKE_BINARY_DIR}/../ts_executors" CACHE STRING "Path to collect dynamic libs and application")

SET(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -g)

add_subdirectory(TaskSystem)
add_subdirectory(PrinterExecutor)
add_subdirectory(RaytracerExecutor)
add_subdirectory(BRDFExecutor)
