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

# Enable MSVC multi-processor parallel compilation (/MP) and UTF-8 source/exec charset
if(MSVC)
    add_compile_options(/MP /utf-8)
endif()

# 여기에 추가 (라인 20 부근)
add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

find_package(protobuf CONFIG REQUIRED)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(boost_beast CONFIG REQUIRED)
find_package(boost_asio CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

set(PROTO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${PROTO_GEN_DIR}")

# proto 파일은 별도 정적 라이브러리로 분리 (PCH 충돌 방지)
add_library(skypotion_proto STATIC "${PROTO_SRC_DIR}/game_sync.proto")
target_link_libraries(skypotion_proto PUBLIC protobuf::libprotobuf)
target_include_directories(skypotion_proto PUBLIC "${PROTO_GEN_DIR}")

# CommonLibSSE-NG build options (SE/AE enabled, tests and VR disabled)
set(BUILD_TESTS OFF CACHE BOOL "Disable CommonLibSSE unit tests" FORCE)
set(ENABLE_SKYRIM_SE ON CACHE BOOL "Enable Skyrim SE support" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "Enable Skyrim AE (1.6+ / 1.7+) support" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "Disable Skyrim VR support" FORCE)

# CommonLibSSE-NG (Maintained upstream supporting 1.7.104 & Address Library Format 5)
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Skip git fetch on reconfigure" FORCE)
FetchContent_Declare(
    CommonLibSSE-NG
    GIT_REPOSITORY https://github.com/alandtse/CommonLibSSE-NG.git
    GIT_TAG        ng
)
FetchContent_MakeAvailable(CommonLibSSE-NG)


protobuf_generate(
    TARGET skypotion_proto
    LANGUAGE cpp
    IMPORT_DIRS "${PROTO_SRC_DIR}"
    PROTOC_OUT_DIR "${PROTO_GEN_DIR}"
)


# Add SkyPotion SKSE plugin library
add_library(
    ${PROJECT_NAME} SHARED
    src/Pch.h
    src/Config.h
    src/Config.cpp
    src/main.cpp
    src/server/WebSocketServer.h
    src/server/WebSocketServer.cpp
    src/sync/ProtoPacketBuilder.h
    src/sync/ProtoPacketBuilder.cpp
    src/game/PerkExtractor.h
    src/game/PerkExtractor.cpp
    src/game/InventoryExtractor.h
    src/game/InventoryExtractor.cpp
    src/game/StatExtractor.h
    src/game/StatExtractor.cpp
    src/game/AlchemyDataExtractor.h
    src/game/AlchemyDataExtractor.cpp
    src/game/AlchemyController.h
    src/game/AlchemyController.cpp
)

# Enable Precompiled Headers for ultra-fast compilation & type consistency
target_precompile_headers(${PROJECT_NAME} PRIVATE src/Pch.h)

target_link_libraries(
    ${PROJECT_NAME} PRIVATE
    CommonLibSSE::CommonLibSSE
    skypotion_proto
    nlohmann_json::nlohmann_json
    Boost::beast
    Boost::asio
    spdlog::spdlog
    ws2_32
)

target_include_directories(${PROJECT_NAME} PRIVATE src)
