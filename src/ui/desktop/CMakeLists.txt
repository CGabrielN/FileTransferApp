cmake_minimum_required(VERSION 3.15)

find_package(Qt6 COMPONENTS Widgets Core Gui Network REQUIRED)

# Enable Qt features
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Resource files
set(APP_RESOURCES
        resources.qrc
)

# UI sources
set(DESKTOP_UI_SOURCES
        ui_manager.cpp
        ui_manager.hpp
        main_desktop.cpp
)

# Create library for desktop UI
add_library(desktop_ui STATIC
        ${DESKTOP_UI_SOURCES}
        ${APP_RESOURCES}
)

# Set include directories
target_include_directories(desktop_ui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(desktop_ui PUBLIC
        Qt6::Widgets
        Qt6::Core
        Qt6::Gui
        Qt6::Network
)

target_link_libraries(desktop_ui PUBLIC
        file_transfer_lib
        third_party_libs
)

# Add platform-specific dependencies
if(UNIX AND NOT APPLE)
    # For Linux, we may need X11
    find_package(X11 QUIET)
    if(X11_FOUND)
        target_link_libraries(desktop_ui PUBLIC ${X11_LIBRARIES})
    endif()
endif()
