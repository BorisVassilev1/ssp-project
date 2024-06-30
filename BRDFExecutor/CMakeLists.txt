project(BRDFExecutor)

set (CMAKE_CXX_STANDARD 17)

if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../libs/glm/CMakeLists.txt)
    message(WARNING "glm not initialized, skipping")
    return()
endif()

if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../libs/urban-spork/CMakeLists.txt)
    message(WARNING "Raytracer not initialized, skipping")
    return()
endif()

set(SOURCES
	BRDFExecutor.cpp
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC ../TaskSystem)
target_include_directories(${PROJECT_NAME} PUBLIC ../libs/glm)
target_include_directories(${PROJECT_NAME} PUBLIC ../libs/urban-spork/src/)

install(TARGETS ${PROJECT_NAME} DESTINATION ${PLUGIN_INSTALL_PATH})
