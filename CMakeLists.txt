cmake_minimum_required(VERSION 3.10)
project(commit-wizard)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v6.1.9
)

FetchContent_MakeAvailable(ftxui)

file(GLOB SRC_FILES src/*.cpp)
add_executable(main ${SRC_FILES})

target_include_directories(main
    PRIVATE ${CMAKE_SOURCE_DIR}/includes
)

target_link_libraries(main
    PRIVATE ftxui::screen
    PRIVATE ftxui::dom
    PRIVATE ftxui::component
)
