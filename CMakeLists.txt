cmake_minimum_required(VERSION 3.16)

project(NewCrawler VERSION 1.0 LANGUAGES CXX)

# ------------------------------------------------------------
# C++ Standard
# ------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)

# ------------------------------------------------------------
# nlohmann/json
# ------------------------------------------------------------
find_package(nlohmann_json 3.11 QUIET)

if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found via find_package — fetching instead")

    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )

    FetchContent_MakeAvailable(nlohmann_json)
endif()

# ------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# ------------------------------------------------------------
# MongoDB C++ Driver
# ------------------------------------------------------------
find_package(bsoncxx CONFIG REQUIRED)
find_package(mongocxx CONFIG REQUIRED)

# ------------------------------------------------------------
# Link against dslibrary (external project — NOT copied in)
# Provides List.h, Hashmap.h, etc. via the "dslibrary" INTERFACE
# target defined in dslibrary/CMakeLists.txt
# ------------------------------------------------------------
add_subdirectory(/Users/aayushsharma/dslibrary ds_library_build)

# ------------------------------------------------------------
# Include directories (this project's own headers)
# ------------------------------------------------------------
include_directories(${PROJECT_SOURCE_DIR}/include)

# ------------------------------------------------------------
# Main executable
# ------------------------------------------------------------
add_executable(new_crawler
    main.cpp
    # Add any other .cpp translation units here.
    # Do NOT list .tpp files — they are #included at the bottom
    # of their matching .h file, never compiled as their own unit.
)

target_include_directories(new_crawler PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(new_crawler
    PRIVATE
        dslibrary
        CURL::libcurl
        Threads::Threads
        nlohmann_json::nlohmann_json
        mongo::bsoncxx_shared
        mongo::mongocxx_shared
)

target_compile_options(new_crawler PRIVATE
    -fsanitize=address
    -fno-omit-frame-pointer
    -g
)

target_link_options(new_crawler PRIVATE
    -fsanitize=address
)