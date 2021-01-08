cmake_minimum_required(VERSION 3.5)
project(chameleonrt)

add_definitions(-DSTBI_NO_SIMD -DPHMAP_HAVE_SSE2=0 -DGLM_ENABLE_EXPERIMENTAL -DSDL_MAIN_HANDLED -DSDL_DISABLE_IMMINTRIN_H)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(Threads REQUIRED)
find_package(glm REQUIRED)
find_package(SDL2 REQUIRED)

add_subdirectory(imgui)
add_subdirectory(util)

include(CMakeDependentOption)


    add_definitions(-DENABLE_VULKAN)
    add_subdirectory(vulkan)

add_executable(chameleonrt main.cpp)

set_target_properties(chameleonrt PROPERTIES
	CXX_STANDARD 14
	CXX_STANDARD_REQUIRED ON)

target_link_libraries(chameleonrt PUBLIC
    util
    display)

	target_link_libraries(chameleonrt PUBLIC render_vulkan)
