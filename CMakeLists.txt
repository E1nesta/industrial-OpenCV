cmake_minimum_required(VERSION 3.21)

option(VISION_USE_VCPKG "Auto-configure vcpkg toolchain/package paths when available" ON)
set(VISION_TOOLS_ROOT "G:/CODE/Tools" CACHE PATH "Local tools root")
set(VISION_QT_ROOT "" CACHE PATH "Qt installation prefix, for example G:/CODE/6.10.2/mingw_64")
set(VISION_VCPKG_ROOT "" CACHE PATH "vcpkg root directory")

macro(_vision_append_prefix _path)
    if(NOT "${_path}" STREQUAL "" AND EXISTS "${_path}")
        list(FIND CMAKE_PREFIX_PATH "${_path}" _vision_prefix_index)
        if(_vision_prefix_index EQUAL -1)
            list(APPEND CMAKE_PREFIX_PATH "${_path}")
        endif()
    endif()
endmacro()

if(NOT DEFINED CMAKE_PREFIX_PATH AND DEFINED ENV{CMAKE_PREFIX_PATH})
    set(CMAKE_PREFIX_PATH "$ENV{CMAKE_PREFIX_PATH}" CACHE PATH "Additional package prefixes")
endif()

if(NOT VISION_QT_ROOT)
    set(_vision_local_qt_root "G:/CODE/6.10.2/mingw_64")
    if(EXISTS "${_vision_local_qt_root}/lib/cmake/Qt6/Qt6Config.cmake")
        set(VISION_QT_ROOT "${_vision_local_qt_root}" CACHE PATH "Qt installation prefix" FORCE)
    endif()
endif()

if(NOT VISION_QT_ROOT AND DEFINED ENV{QTDIR})
    set(VISION_QT_ROOT "$ENV{QTDIR}" CACHE PATH "Qt installation prefix" FORCE)
elseif(NOT VISION_QT_ROOT AND DEFINED ENV{QT_ROOT})
    set(VISION_QT_ROOT "$ENV{QT_ROOT}" CACHE PATH "Qt installation prefix" FORCE)
endif()

if(NOT VISION_VCPKG_ROOT)
    set(_vision_local_vcpkg_root "G:/environment/vcpkg")
    if(EXISTS "${_vision_local_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
        set(VISION_VCPKG_ROOT "${_vision_local_vcpkg_root}" CACHE PATH "vcpkg root directory" FORCE)
    endif()
endif()

if(NOT VISION_VCPKG_ROOT AND DEFINED ENV{VCPKG_ROOT})
    set(VISION_VCPKG_ROOT "$ENV{VCPKG_ROOT}" CACHE PATH "vcpkg root directory" FORCE)
endif()

if(NOT DEFINED Qt6_DIR AND DEFINED ENV{Qt6_DIR})
    set(Qt6_DIR "$ENV{Qt6_DIR}" CACHE PATH "Qt6 package config directory")
endif()

if(NOT DEFINED Qt5_DIR AND DEFINED ENV{Qt5_DIR})
    set(Qt5_DIR "$ENV{Qt5_DIR}" CACHE PATH "Qt5 package config directory")
endif()

if(VISION_QT_ROOT)
    _vision_append_prefix("${VISION_QT_ROOT}")
endif()

if(DEFINED Qt6_DIR AND EXISTS "${Qt6_DIR}")
    get_filename_component(_vision_qt6_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
    _vision_append_prefix("${_vision_qt6_prefix}")
endif()

if(DEFINED Qt5_DIR AND EXISTS "${Qt5_DIR}")
    get_filename_component(_vision_qt5_prefix "${Qt5_DIR}/../../.." ABSOLUTE)
    _vision_append_prefix("${_vision_qt5_prefix}")
endif()

if(VISION_USE_VCPKG AND NOT DEFINED CMAKE_TOOLCHAIN_FILE AND VISION_VCPKG_ROOT)
    set(_vision_vcpkg_toolchain "${VISION_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    if(EXISTS "${_vision_vcpkg_toolchain}")
        set(CMAKE_TOOLCHAIN_FILE
            "${_vision_vcpkg_toolchain}"
            CACHE FILEPATH "vcpkg toolchain file"
        )
    endif()
endif()

if(CMAKE_GENERATOR MATCHES "Ninja" AND NOT DEFINED CMAKE_MAKE_PROGRAM)
    set(_vision_local_ninja "${VISION_TOOLS_ROOT}/Ninja/ninja.exe")
    if(EXISTS "${_vision_local_ninja}")
        set(CMAKE_MAKE_PROGRAM "${_vision_local_ninja}" CACHE FILEPATH "Ninja executable")
    endif()
endif()

if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(_vision_default_triplet "x64-windows")

    if(CMAKE_GENERATOR MATCHES "MinGW Makefiles")
        set(_vision_default_triplet "x64-mingw-static")
    elseif(CMAKE_PREFIX_PATH MATCHES "mingw")
        set(_vision_default_triplet "x64-mingw-static")
    elseif(VISION_QT_ROOT MATCHES "mingw")
        set(_vision_default_triplet "x64-mingw-static")
    elseif(DEFINED ENV{QTDIR} AND "$ENV{QTDIR}" MATCHES "mingw")
        set(_vision_default_triplet "x64-mingw-static")
    endif()

    set(VCPKG_TARGET_TRIPLET "${_vision_default_triplet}" CACHE STRING "vcpkg target triplet")
endif()

project(VisionInspectionSystem VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

message(STATUS "Generator: ${CMAKE_GENERATOR}")
message(STATUS "Tools root: ${VISION_TOOLS_ROOT}")
message(STATUS "VCPKG target triplet: ${VCPKG_TARGET_TRIPLET}")
if(CMAKE_PREFIX_PATH)
    message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")
endif()

if(CMAKE_GENERATOR MATCHES "Visual Studio" AND VCPKG_TARGET_TRIPLET MATCHES "mingw")
    message(WARNING
        "Visual Studio generator is using a MinGW vcpkg triplet (${VCPKG_TARGET_TRIPLET}). "
        "Prefer a MinGW/Ninja kit or override VCPKG_TARGET_TRIPLET=x64-windows."
    )
endif()

if(CMAKE_GENERATOR MATCHES "Visual Studio" AND CMAKE_PREFIX_PATH MATCHES "mingw")
    message(WARNING
        "Visual Studio generator is configured with a MinGW Qt prefix. "
        "Use a MinGW/Ninja kit or point VISION_QT_ROOT/CMAKE_PREFIX_PATH to an MSVC Qt build."
    )
endif()

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Sql Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql Network)

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_standard_project_setup()
endif()

if(NOT DEFINED OpenCV_DIR AND DEFINED ENV{OpenCV_DIR})
    set(OpenCV_DIR "$ENV{OpenCV_DIR}" CACHE PATH "OpenCV package config directory")
endif()

if(NOT DEFINED OpenCV_ROOT AND DEFINED ENV{OpenCV_ROOT})
    set(OpenCV_ROOT "$ENV{OpenCV_ROOT}" CACHE PATH "OpenCV package root")
endif()

if(VISION_USE_VCPKG AND NOT OpenCV_DIR AND VISION_VCPKG_ROOT)
    set(_vision_vcpkg_opencv_dir
        "${VISION_VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/share/opencv4"
    )
    if(EXISTS "${_vision_vcpkg_opencv_dir}/OpenCVConfig.cmake")
        set(OpenCV_DIR "${_vision_vcpkg_opencv_dir}" CACHE PATH "OpenCV package config directory")
    endif()
endif()

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

set(PROJECT_SOURCES
    main.cpp
    app/appcontroller.h
    app/appcontroller.cpp
    camera/icamera.h
    camera/virtualcamera.h
    camera/virtualcamera.cpp
    camera/cameramanager.h
    camera/cameramanager.cpp
    communication/tcpmanager.h
    communication/tcpmanager.cpp
    common/constants.h
    common/utils.h
    common/utils.cpp
    logger/logmanager.h
    logger/logmanager.cpp
    models/deviceconfig.h
    models/detectresult.h
    models/inspectionrecord.h
    models/visionparam.h
    storage/configmanager.h
    storage/configmanager.cpp
    storage/databasemanager.h
    storage/databasemanager.cpp
    storage/recordmanager.h
    storage/recordmanager.cpp
    ui/imageviewwidget.h
    ui/imageviewwidget.cpp
    ui/mainwindow.h
    ui/mainwindow.cpp
    ui/mainwindow.ui
    vision/detectionworker.h
    vision/detectionworker.cpp
    vision/imageprocessor.h
    vision/imageprocessor.cpp
)

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_executable(VisionInspectionSystem
        WIN32
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    add_executable(VisionInspectionSystem WIN32 ${PROJECT_SOURCES})
endif()

target_include_directories(VisionInspectionSystem
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(VisionInspectionSystem
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Sql
        Qt${QT_VERSION_MAJOR}::Network
        ${OpenCV_LIBS}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(VisionInspectionSystem)
endif()
