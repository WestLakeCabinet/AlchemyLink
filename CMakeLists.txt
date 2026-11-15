cmake_minimum_required(VERSION 3.25)

project(
    SkyPotionSKSE
    VERSION 1.0.0
    DESCRIPTION "Skyrim SE/AE WebSocket Plugin for SkyPotion"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ensure CRT matches CommonLibSSE-NG (/MD dynamic CRT with static library linkage)
set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "vcpkg triplet matching /MD CRT")

# Enable MSVC multi-processor parallel compilation (/MP)
if(MSVC)
    add_compile_options(/MP)
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# Disable CommonLibSSE-NG internal tests (prevents dependency on Catch2)
set(BUILD_TESTS OFF CACHE BOOL "Disable CommonLibSSE unit tests" FORCE)

# CommonLibSSE-NG (Automatic download & build)
include(FetchContent)
FetchContent_Declare(
    CommonLibSSE-NG
    GIT_REPOSITORY https://github.com/CharmedBaryon/CommonLibSSE-NG.git
    GIT_TAG        main
)
FetchContent_MakeAvailable(CommonLibSSE-NG)

# Add SkyPotion SKSE plugin library
add_library(
    ${PROJECT_NAME} SHARED
    src/Pch.h
    src/main.cpp
    src/server/WebSocketServer.h
    src/server/WebSocketServer.cpp
    src/game/PerkExtractor.h
    src/game/PerkExtractor.cpp
    src/game/InventoryExtractor.h
    src/game/InventoryExtractor.cpp
    src/game/StatExtractor.h
    src/game/StatExtractor.cpp
)

# Enable Precompiled Headers for ultra-fast compilation & type consistency
target_precompile_headers(
    ${PROJECT_NAME}
    PRIVATE
        src/Pch.h
)

target_link_libraries(
    ${PROJECT_NAME}
    PRIVATE
        CommonLibSSE::CommonLibSSE
        nlohmann_json::nlohmann_json
        ixwebsocket::ixwebsocket
        spdlog::spdlog
        ws2_32
        crypt32
)

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
        src
)
