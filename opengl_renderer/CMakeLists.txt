cmake_minimum_required (VERSION 3.1)

project(ltc-aniso)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-Os -march=native")

find_package(OpenMP)
if (OPENMP_FOUND)
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
endif()

# disable GLFW docs, examples and tests
# see http://www.glfw.org/docs/latest/build_guide.html
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# set path to dependencies
add_subdirectory(submodules/glfw)
include_directories(submodules/glfw/include)
include_directories(submodules/imgui)
include_directories(submodules/dj_opengl)
include_directories(submodules/glm)
include_directories(submodules/stb)

# imgui source files
set(IMGUI_SRC_DIR submodules/imgui)
aux_source_directory(${IMGUI_SRC_DIR} IMGUI_SRC_FILES)

# compiler specific options
if(MSVC OR MSVC_IDE)
	add_definitions("/D_CRT_SECURE_NO_WARNINGS")
endif()

add_executable(sphere ${IMGUI_SRC_FILES} sphere.cpp imgui_impl.cpp glad/glad.c)
target_link_libraries(sphere glfw)
target_compile_definitions(sphere PUBLIC -DPATH_TO_SRC_DIRECTORY="${CMAKE_SOURCE_DIR}/" -DPATH_TO_ASSET_DIRECTORY="${CMAKE_SOURCE_DIR}/assets/")

