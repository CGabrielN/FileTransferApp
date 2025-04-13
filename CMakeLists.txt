cmake_minimum_required(VERSION 3.15)

# Project name and version
project(FileTransferApp VERSION 1.0.0 LANGUAGES CXX)

# Specify C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Options
option(BUILD_TESTS "Build the test suite" ON)
option(BUILD_DOCS "Build documentation" OFF)
option(ENABLE_ENCRYPTION "Enable encryption for file transfers" ON)
option(USE_SYSTEM_BOOST "Use system installed Boost instead of fetching it" OFF)

# Set output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Set QT Path
set(Qt6_DIR "C:/Qt/6.9.0/mingw_64/lib/cmake/Qt6")

# Detect platform
if(WIN32)
    set(PLATFORM_NAME "windows")
    add_definitions(-DPLATFORM_WINDOWS)
elseif(APPLE)
    if(IOS)
        set(PLATFORM_NAME "ios")
        add_definitions(-DPLATFORM_IOS)
    else()
        set(PLATFORM_NAME "macos")
        add_definitions(-DPLATFORM_MACOS)
    endif()
elseif(ANDROID)
    set(PLATFORM_NAME "android")
    add_definitions(-DPLATFORM_ANDROID)
elseif(UNIX)
    set(PLATFORM_NAME "linux")
    add_definitions(-DPLATFORM_LINUX)
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# Find dependencies
find_package(Threads REQUIRED)

# Try to find Boost if USE_SYSTEM_BOOST is enabled
if(USE_SYSTEM_BOOST)
    find_package(Boost 1.70 COMPONENTS system filesystem)
endif()

# Include third-party libraries
add_subdirectory(third_party)

# Define source files
set(CORE_SOURCES
        src/core/file_handler.cpp
        src/core/transfer_manager.cpp
        src/core/discovery_service.cpp
)

set(NETWORK_SOURCES
        src/network/socket_handler.cpp
        src/network/protocol.cpp
)

set(UTILS_SOURCES
        src/utils/logging.cpp
)

if(ENABLE_ENCRYPTION)
    list(APPEND UTILS_SOURCES src/utils/encryption.cpp)
    add_definitions(-DENABLE_ENCRYPTION)
endif()

set(PLATFORM_SOURCES
        src/platform/${PLATFORM_NAME}/platform_impl.cpp
)

# Create main library target
add_library(file_transfer_lib STATIC
        ${CORE_SOURCES}
        ${NETWORK_SOURCES}
        ${PLATFORM_SOURCES}
        ${UTILS_SOURCES}
)

# Include directories
target_include_directories(file_transfer_lib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link dependencies
target_link_libraries(file_transfer_lib PUBLIC
        Threads::Threads
        third_party_libs  # This will include Boost or alternatives
)

# Add platform-specific libraries
if(WIN32)
    target_link_libraries(file_transfer_lib PUBLIC ws2_32 iphlpapi Mswsock)
endif()

# Create main executable
add_executable(file_transfer src/main.cpp)
target_link_libraries(file_transfer PRIVATE file_transfer_lib desktop_ui)

# UI component based on platform
if(ANDROID OR IOS)
    add_subdirectory(src/ui/mobile)
else()
    add_subdirectory(src/ui/desktop)
endif()

# Build documentation if enabled
if(BUILD_DOCS)
    add_subdirectory(docs)
endif()

# Installation rules
install(TARGETS file_transfer
        RUNTIME DESTINATION bin
)

# Package information for CPack
include(InstallRequiredSystemLibraries)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Cross-platform file transfer application")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
include(CPack)