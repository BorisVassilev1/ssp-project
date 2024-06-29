project(RaytracerExecutor)

if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../libs/urban-spork/CMakeLists.txt)
    message(WARNING "Raytracer not initialized, skipping")
    return()
endif()

set (CMAKE_CXX_STANDARD 17)

set(SOURCES
	${CMAKE_CURRENT_SOURCE_DIR}/../libs/urban-spork/src/Material.h
	${CMAKE_CURRENT_SOURCE_DIR}/../libs/urban-spork/src/Material.cpp

	${CMAKE_CURRENT_SOURCE_DIR}/../libs/urban-spork/src/Image.hpp

	${CMAKE_CURRENT_SOURCE_DIR}/../libs/urban-spork/src/Primitive.h
	${CMAKE_CURRENT_SOURCE_DIR}/../libs/urban-spork/src/Primitive.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../libs/urban-spork/src/Accelerators.cpp

	${CMAKE_CURRENT_SOURCE_DIR}/../libs/urban-spork/src/Utils.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/../libs/urban-spork/src/Threading.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/../libs/urban-spork/src/Mesh.h
	${CMAKE_CURRENT_SOURCE_DIR}/../libs/urban-spork/src/Mesh.cpp

	${CMAKE_CURRENT_SOURCE_DIR}/../libs/urban-spork/src/Mesh.cpp

	${CMAKE_CURRENT_SOURCE_DIR}/../libs/urban-spork/src/third_party/stb_image_write.h
	${CMAKE_CURRENT_SOURCE_DIR}/../libs/urban-spork/src/third_party/tiny_obj_loader.h
    Raytracer.cpp
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

target_compile_definitions(${PROJECT_NAME} PRIVATE
    MESH_FOLDER="${CMAKE_CURRENT_SOURCE_DIR}/../libs/urban-spork/mesh"
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ../TaskSystem
    ../libs/urban-spork/src
)

install(TARGETS ${PROJECT_NAME} DESTINATION ${PLUGIN_INSTALL_PATH})
