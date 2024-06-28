project(BRDFExecutor)

set (CMAKE_CXX_STANDARD 17)

set(SOURCES
	BRDFExecutor.cpp
)

add_subdirectory(glm)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC ../TaskSystem)
target_include_directories(${PROJECT_NAME} PUBLIC glm)

install(TARGETS ${PROJECT_NAME} DESTINATION ${PLUGIN_INSTALL_PATH})
