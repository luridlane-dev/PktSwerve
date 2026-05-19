cmake_minimum_required(VERSION 3.16)
project(PktSwerve VERSION 1.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network Concurrent)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Concurrent)

if(WIN32)
    add_definitions(-DPLATFORM_WINDOWS)
elseif(APPLE)
    add_definitions(-DPLATFORM_MACOS)
elseif(UNIX)
    add_definitions(-DPLATFORM_LINUX)
endif()

set(PROJECT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/TrayApplication.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/TrayApplication.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/SettingsWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/SettingsWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/PopupWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/PopupWindow.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/BypassEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/BypassEngine.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/PacketUtils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/IPlatformBackend.h
    ${CMAKE_CURRENT_SOURCE_DIR}/resources.qrc
)

if(WIN32)
    list(APPEND PROJECT_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows/WinDivertBackend.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows/WinDivertBackend.h
        ${CMAKE_CURRENT_SOURCE_DIR}/app_icon.rc
    )
elseif(APPLE)
    list(APPEND PROJECT_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/mac/PfBackend.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/mac/PfBackend.h
    )
elseif(UNIX)
    list(APPEND PROJECT_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/linux/NFQueueBackend.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/linux/NFQueueBackend.h
    )
endif()

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(PktSwerve MANUAL_FINALIZATION ${PROJECT_SOURCES}
        src/app/Logger.h src/app/Logger.cpp
        src/app/ToggleSwitch.h
    )
else()
    if(ANDROID)
        add_library(PktSwerve SHARED ${PROJECT_SOURCES})
    else()
        add_executable(PktSwerve ${PROJECT_SOURCES})
    endif()
endif()

target_include_directories(PktSwerve PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(PktSwerve PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Concurrent
)

if(WIN32)
    target_include_directories(PktSwerve PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/windivert/include
    )
    target_link_libraries(PktSwerve PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/windivert/lib/WinDivert.lib
        ws2_32
        iphlpapi
    )
elseif(UNIX AND NOT APPLE)
    find_library(NETFILTER_QUEUE_LIB netfilter_queue REQUIRED)
    find_library(NFNETLINK_LIB       nfnetlink       REQUIRED)
    target_link_libraries(PktSwerve PRIVATE
        ${NETFILTER_QUEUE_LIB}
        ${NFNETLINK_LIB}
    )
elseif(APPLE)
    target_link_libraries(PktSwerve PRIVATE
        "-framework CoreFoundation"
        "-framework SystemConfiguration"
    )
endif()

set_target_properties(PktSwerve PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION       ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
)

if(WIN32)
    set_target_properties(PktSwerve PROPERTIES WIN32_EXECUTABLE TRUE)
endif()


include(GNUInstallDirs)
install(TARGETS PktSwerve
    BUNDLE  DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(PktSwerve)
endif()