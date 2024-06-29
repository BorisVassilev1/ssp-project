project(TaskSystem)

set (CMAKE_CXX_STANDARD 17)

set(SOURCES
    TaskSystem.cpp
    main.cpp
	../libs/imgui/backends/imgui_impl_glfw.cpp
	../libs/imgui/backends/imgui_impl_opengl3.cpp
	../libs/imgui/imgui.cpp
	../libs/imgui/imgui_demo.cpp
	../libs/imgui/imgui_draw.cpp
	../libs/imgui/imgui_widgets.cpp
	../libs/imgui/imgui_tables.cpp
)

set(HEADERS
    Task.h
    Executor.h
    TaskSystem.h
)

add_executable(${PROJECT_NAME} "${SOURCES};${HEADERS}")

set(GLFW_BUILD_DOCS OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_VULKAN_STATIC OFF)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../libs/glfw ${CMAKE_CURRENT_BINARY_DIR}/libs/glfw)

target_include_directories(${PROJECT_NAME} PUBLIC ../libs/imgui/ ../libs/imgui/backends/)
target_include_directories(${PROJECT_NAME} PUBLIC ../libs/glfw/include)

target_compile_definitions(${PROJECT_NAME} PRIVATE TS_EXECUTOR_PATH="${PLUGIN_INSTALL_PATH}")
target_compile_options(${PROJECT_NAME} PRIVATE "-g")

target_link_libraries(${PROJECT_NAME} PUBLIC glfw)

install(TARGETS ${PROJECT_NAME} DESTINATION ${PLUGIN_INSTALL_PATH})
