project(BRDFExecutor)

set (CMAKE_CXX_STANDARD 17)

set(SOURCES
	BRDFExecutor.cpp
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC ../TaskSystem)
target_include_directories(${PROJECT_NAME} PUBLIC ../libs/glm)
target_include_directories(${PROJECT_NAME} PUBLIC ../libs/urban-spork/src/)

install(TARGETS ${PROJECT_NAME} DESTINATION ${PLUGIN_INSTALL_PATH})
