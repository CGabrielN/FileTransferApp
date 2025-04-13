cmake_minimum_required(VERSION 3.15)

option(USE_QT "Use Qt for cross-platform UI" ON)

# Only build the Qt UI if the USE_QT option is enabled
if (USE_QT)
    find_package(Qt6 COMPONENTS Widgets Core Gui Network REQUIRED)

    message(STATUS "Qt6 found - building with Qt support")
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
            windows_main.cpp
            ui_manager.hpp
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

    target_include_directories(desktop_ui PRIVATE ${Qt6Widgets_INCLUDE_DIRS})
    # Link Qt libraries
    target_link_libraries(desktop_ui PUBLIC
            Qt6::Widgets
            Qt6::Core
            Qt6::Gui
            Qt6::Network
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

    # Link the main executable with desktop_ui
    target_link_libraries(file_transfer PRIVATE desktop_ui)

else ()
    # Stop the build process if Qt is not found
    message(FATAL_ERROR "Use QT option is disabled. Please enable it to build the Qt UI.")
endif ()