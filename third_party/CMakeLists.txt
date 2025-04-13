# third_party/CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Handle Boost differently - instead of fetching the repo, use header-only for system/filesystem
if(NOT USE_SYSTEM_BOOST OR NOT Boost_FOUND)
    message(STATUS "Boost not found on system or system Boost disabled, using header-only solution...")

    # Create interface libraries for boost components
    add_library(boost_system INTERFACE)
    add_library(boost_filesystem INTERFACE)

    # Create an alias that matches the expected target name
    add_library(Boost::system ALIAS boost_system)
    add_library(Boost::filesystem ALIAS boost_filesystem)

    # Create a combined interface
    add_library(boost_interface INTERFACE)
    target_link_libraries(boost_interface INTERFACE boost_system boost_filesystem)

    # Mark that we're using a minimal Boost setup
    add_definitions(-DUSING_MINIMAL_BOOST)
endif()

# Asio for networking (standalone version without Boost dependency)
FetchContent_Declare(
        asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG asio-1-22-1  # Choose a stable version
)
FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
    FetchContent_Populate(asio)
    add_library(asio INTERFACE)
    target_include_directories(asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
    target_compile_definitions(asio INTERFACE ASIO_STANDALONE)
endif()

# JSON for Modern C++ (for configuration and metadata)
FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

# spdlog for logging
FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.10.0
)
FetchContent_MakeAvailable(spdlog)

# Magic Enum for better enum handling
FetchContent_Declare(
        magic_enum
        GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
        GIT_TAG v0.8.2
)
FetchContent_MakeAvailable(magic_enum)

# Protocol Buffers for serialization
option(PROTOBUF_ENABLED "Use Protocol Buffers for serialization" ON)
if(PROTOBUF_ENABLED)
    find_package(Protobuf)
    if(Protobuf_FOUND)
        message(STATUS "Using system Protocol Buffers")
    else()
        message(STATUS "Protocol Buffers not found, fetching it")
        FetchContent_Declare(
                protobuf
                GIT_REPOSITORY https://github.com/protocolbuffers/protobuf.git
                GIT_TAG v3.21.12
                SOURCE_SUBDIR cmake
        )
        set(protobuf_BUILD_TESTS OFF CACHE BOOL "Disable Protocol Buffers tests")
        FetchContent_MakeAvailable(protobuf)
    endif()
endif()

# OpenSSL for encryption (if enabled)
if(ENABLE_ENCRYPTION)
    find_package(OpenSSL)
    if(OpenSSL_FOUND)
        message(STATUS "Using OpenSSL for encryption")
        add_definitions(-DHAS_OPENSSL)
    else()
        message(STATUS "OpenSSL not found. Building with limited encryption support.")
    endif()
endif()

# zlib for compression
FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
)
FetchContent_MakeAvailable(zlib)

# Create an interface target to easily link all third-party libraries
add_library(third_party_libs INTERFACE)
target_link_libraries(third_party_libs INTERFACE
        asio
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        magic_enum::magic_enum
        zlib
)

# Add Boost to the interface
if(USE_SYSTEM_BOOST AND Boost_FOUND)
    target_link_libraries(third_party_libs INTERFACE ${Boost_LIBRARIES})
    target_include_directories(third_party_libs INTERFACE ${Boost_INCLUDE_DIRS})
else()
    target_link_libraries(third_party_libs INTERFACE boost_interface)
endif()

if(PROTOBUF_ENABLED AND (Protobuf_FOUND OR protobuf_FOUND))
    target_link_libraries(third_party_libs INTERFACE protobuf::libprotobuf)
endif()

if(ENABLE_ENCRYPTION AND OpenSSL_FOUND)
    target_link_libraries(third_party_libs INTERFACE OpenSSL::SSL OpenSSL::Crypto)
endif()

# Export all variables to parent scope
set(THIRD_PARTY_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include PARENT_SCOPE)