cmake_minimum_required(VERSION 3.21)

option(VISION_USE_VCPKG "Auto-configure vcpkg toolchain/package paths when available" ON)
option(VISION_REQUIRE_VIDEO_RUNTIME "Require dynamic OpenCV videoio/FFmpeg runtime deployment support" ON)
set(VISION_TOOLS_ROOT "" CACHE PATH "Optional local tools root for Ninja fallback")
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

if(NOT VISION_QT_ROOT AND DEFINED ENV{QTDIR})
    set(VISION_QT_ROOT "$ENV{QTDIR}" CACHE PATH "Qt installation prefix" FORCE)
elseif(NOT VISION_QT_ROOT AND DEFINED ENV{QT_ROOT})
    set(VISION_QT_ROOT "$ENV{QT_ROOT}" CACHE PATH "Qt installation prefix" FORCE)
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
        set(_vision_default_triplet "x64-mingw-dynamic")
    elseif(CMAKE_PREFIX_PATH MATCHES "mingw")
        set(_vision_default_triplet "x64-mingw-dynamic")
    elseif(VISION_QT_ROOT MATCHES "mingw")
        set(_vision_default_triplet "x64-mingw-dynamic")
    elseif(DEFINED ENV{QTDIR} AND "$ENV{QTDIR}" MATCHES "mingw")
        set(_vision_default_triplet "x64-mingw-dynamic")
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
message(STATUS "Vision require video runtime: ${VISION_REQUIRE_VIDEO_RUNTIME}")
message(STATUS "Tools root: ${VISION_TOOLS_ROOT}")
message(STATUS "Qt root: ${VISION_QT_ROOT}")
message(STATUS "vcpkg root: ${VISION_VCPKG_ROOT}")
message(STATUS "VCPKG target triplet: ${VCPKG_TARGET_TRIPLET}")
if(CMAKE_PREFIX_PATH)
    message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")
endif()
if(NOT VISION_QT_ROOT)
    message(WARNING
        "QTDIR/QT_ROOT is not configured yet. Set VISION_QT_ROOT, QTDIR, QT_ROOT, "
        "or CMAKE_PREFIX_PATH before analyzing OpenCV or video runtime issues."
    )
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
find_package(Qt${QT_VERSION_MAJOR} OPTIONAL_COMPONENTS Test)

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

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio)

message(STATUS "Resolved OpenCV_DIR: ${OpenCV_DIR}")
message(STATUS "OpenCV shared build: ${OpenCV_SHARED}")

if(VISION_REQUIRE_VIDEO_RUNTIME)
    set(_vision_has_explicit_opencv_override OFF)
    if((DEFINED OpenCV_ROOT AND NOT "${OpenCV_ROOT}" STREQUAL "")
            OR (DEFINED OpenCV_DIR AND NOT "${OpenCV_DIR}" STREQUAL ""))
        set(_vision_has_explicit_opencv_override ON)
    endif()

    if(NOT VISION_VCPKG_ROOT AND NOT _vision_has_explicit_opencv_override)
        message(FATAL_ERROR
            "VISION_REQUIRE_VIDEO_RUNTIME=ON requires either a vcpkg environment with deployable "
            "dynamic OpenCV runtimes or an explicit dynamic OpenCV override via "
            "OpenCV_DIR/OpenCV_ROOT."
        )
    endif()

    if(VISION_VCPKG_ROOT AND VCPKG_TARGET_TRIPLET MATCHES "static")
        message(FATAL_ERROR
            "This project requires a dynamic OpenCV runtime for video preview support. "
            "The selected vcpkg triplet (${VCPKG_TARGET_TRIPLET}) appears to be static. "
            "Use a dynamic triplet such as x64-mingw-dynamic or x64-windows."
        )
    endif()

    if(DEFINED OpenCV_SHARED AND NOT OpenCV_SHARED)
        message(FATAL_ERROR
            "OpenCV was resolved as a static build, but this project requires a dynamic OpenCV "
            "runtime with videoio/FFmpeg deployment support."
        )
    endif()
endif()

set(_vision_opencv_runtime_search_roots)
set(_vision_opencv_runtime_search_mode "vcpkg-standard")
set(_vision_runtime_bin_suffixes)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND _vision_runtime_bin_suffixes "debug/bin")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release"
        OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo"
        OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    list(APPEND _vision_runtime_bin_suffixes "bin")
else()
    list(APPEND _vision_runtime_bin_suffixes "bin" "debug/bin")
endif()

if(VISION_VCPKG_ROOT AND VCPKG_TARGET_TRIPLET)
    set(_vision_vcpkg_installed_root "${VISION_VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}")
    foreach(_vision_runtime_suffix IN LISTS _vision_runtime_bin_suffixes)
        list(APPEND _vision_opencv_runtime_search_roots
            "${_vision_vcpkg_installed_root}/${_vision_runtime_suffix}"
        )
    endforeach()
endif()

if(OpenCV_ROOT AND EXISTS "${OpenCV_ROOT}")
    set(_vision_opencv_runtime_search_mode "explicit-root")
    foreach(_vision_runtime_suffix IN LISTS _vision_runtime_bin_suffixes)
        list(APPEND _vision_opencv_runtime_search_roots
            "${OpenCV_ROOT}/${_vision_runtime_suffix}"
        )
    endforeach()
elseif(OpenCV_DIR AND EXISTS "${OpenCV_DIR}")
    get_filename_component(_vision_opencv_prefix "${OpenCV_DIR}/../.." ABSOLUTE)
    if(EXISTS "${_vision_opencv_prefix}")
        set(_vision_opencv_runtime_search_mode "explicit-dir")
        foreach(_vision_runtime_suffix IN LISTS _vision_runtime_bin_suffixes)
            list(APPEND _vision_opencv_runtime_search_roots
                "${_vision_opencv_prefix}/${_vision_runtime_suffix}"
            )
        endforeach()
    endif()
endif()

list(REMOVE_DUPLICATES _vision_opencv_runtime_search_roots)
message(STATUS "OpenCV runtime search mode: ${_vision_opencv_runtime_search_mode}")
message(STATUS "OpenCV runtime search roots: ${_vision_opencv_runtime_search_roots}")

if(VISION_REQUIRE_VIDEO_RUNTIME AND NOT _vision_opencv_runtime_search_roots)
    message(FATAL_ERROR
        "No OpenCV runtime search roots were resolved. "
        "Use a dynamic vcpkg install or set OpenCV_DIR/OpenCV_ROOT explicitly."
    )
endif()

set(_vision_opencv_runtime_dlls)
set(_vision_opencv_videoio_plugins)
set(_vision_opencv_videoio_runtime_deps)
foreach(_vision_runtime_root IN LISTS _vision_opencv_runtime_search_roots)
    if(NOT EXISTS "${_vision_runtime_root}")
        continue()
    endif()

    file(GLOB _vision_runtime_opencv_dlls
        "${_vision_runtime_root}/opencv_*.dll"
        "${_vision_runtime_root}/libopencv_*.dll"
    )
    if(_vision_runtime_opencv_dlls)
        list(APPEND _vision_opencv_runtime_dlls ${_vision_runtime_opencv_dlls})
    endif()

    file(GLOB _vision_runtime_plugins
        "${_vision_runtime_root}/opencv_videoio_*.dll"
        "${_vision_runtime_root}/libopencv_videoio*.dll"
    )
    if(_vision_runtime_plugins)
        list(APPEND _vision_opencv_videoio_plugins ${_vision_runtime_plugins})
    endif()

    file(GLOB _vision_runtime_ffmpeg_deps
        "${_vision_runtime_root}/avcodec-*.dll"
        "${_vision_runtime_root}/avformat-*.dll"
        "${_vision_runtime_root}/avutil-*.dll"
        "${_vision_runtime_root}/swresample-*.dll"
        "${_vision_runtime_root}/swscale-*.dll"
        "${_vision_runtime_root}/postproc-*.dll"
    )
    if(_vision_runtime_ffmpeg_deps)
        list(APPEND _vision_opencv_videoio_runtime_deps ${_vision_runtime_ffmpeg_deps})
    endif()
endforeach()

list(REMOVE_DUPLICATES _vision_opencv_runtime_dlls)
list(REMOVE_DUPLICATES _vision_opencv_videoio_plugins)
list(REMOVE_DUPLICATES _vision_opencv_videoio_runtime_deps)

if(_vision_opencv_runtime_dlls)
    message(STATUS "OpenCV runtime DLLs detected: ${_vision_opencv_runtime_dlls}")
elseif(VISION_REQUIRE_VIDEO_RUNTIME)
    message(FATAL_ERROR
        "No OpenCV runtime DLLs were found in the resolved runtime search roots. "
        "The configured build expects deployable dynamic OpenCV runtimes."
    )
elseif(OpenCV_SHARED)
    message(WARNING
        "OpenCV is configured as shared, but no runtime DLLs were found under the resolved runtime search roots. "
        "Applications may fail to start until the OpenCV runtime is deployed manually."
    )
endif()

if(_vision_opencv_videoio_plugins)
    message(STATUS "OpenCV videoio plugins detected: ${_vision_opencv_videoio_plugins}")
elseif(VISION_REQUIRE_VIDEO_RUNTIME)
    message(FATAL_ERROR
        "No OpenCV videoio runtime DLLs were found in the resolved runtime search roots. "
        "VideoCapture would likely fall back to CAP_IMAGES for video files."
    )
else()
    message(WARNING
        "No OpenCV videoio runtime DLLs were found under the resolved runtime search roots. "
        "VideoCapture may fall back to CAP_IMAGES for video files. "
    )
endif()

if(_vision_opencv_videoio_runtime_deps)
    message(STATUS "OpenCV videoio runtime dependencies detected: ${_vision_opencv_videoio_runtime_deps}")
elseif(VISION_REQUIRE_VIDEO_RUNTIME)
    message(FATAL_ERROR
        "No FFmpeg runtime dependency DLLs were found in the resolved runtime search roots. "
        "The configured build expects deployable FFmpeg dependencies for video playback."
    )
else()
    message(WARNING
        "No FFmpeg runtime dependency DLLs were found under the resolved runtime search roots. "
        "Video playback may fail at runtime."
    )
endif()

set(PROJECT_SOURCES
    main.cpp
    application/controllers/appcontroller.h
    application/controllers/appcontroller.cpp
    application/orchestrators/inspectionorchestrator.h
    application/orchestrators/inspectionorchestrator.cpp
    application/orchestrators/resultdispatcher.h
    application/orchestrators/resultdispatcher.cpp
    application/state/inspectionsessionstate.h
    common/config/constants.h
    common/logging/logevent.h
    common/logging/logmanager.h
    common/logging/logmanager.cpp
    common/logging/logworker.h
    common/logging/logworker.cpp
    common/utils/utils.h
    common/utils/utils.cpp
    domain/entities/capturedframe.h
    domain/entities/deviceconfig.h
    domain/entities/inputsource.h
    domain/entities/inspectionoutput.h
    domain/entities/inspectionrecord.h
    domain/entities/inspectionresult.h
    domain/entities/inspectiontask.h
    domain/entities/persistenceresult.h
    domain/entities/recipe.h
    domain/services/imageprocessor.h
    domain/services/imageprocessor.cpp
    infrastructure/capture/icamera.h
    infrastructure/capture/captureworker.h
    infrastructure/capture/captureworker.cpp
    infrastructure/capture/videocapturesource.h
    infrastructure/capture/videocapturesource.cpp
    infrastructure/capture/virtualcamera.h
    infrastructure/capture/virtualcamera.cpp
    infrastructure/capture/cameramanager.h
    infrastructure/capture/cameramanager.cpp
    infrastructure/communication/tcpmanager.h
    infrastructure/communication/tcpmanager.cpp
    infrastructure/communication/tcpworker.h
    infrastructure/communication/tcpworker.cpp
    infrastructure/config/configmanager.h
    infrastructure/config/configmanager.cpp
    infrastructure/storage/databasemanager.h
    infrastructure/storage/databasemanager.cpp
    infrastructure/storage/inspectionpersistenceworker.h
    infrastructure/storage/inspectionpersistenceworker.cpp
    infrastructure/storage/recordmanager.h
    infrastructure/storage/recordmanager.cpp
    infrastructure/vision/inspectionworker.h
    infrastructure/vision/inspectionworker.cpp
    ui/imageviewwidget.h
    ui/imageviewwidget.cpp
    ui/mainwindow.h
    ui/mainwindow.cpp
    ui/mainwindow.ui
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

if(_vision_opencv_runtime_dlls OR _vision_opencv_videoio_plugins OR _vision_opencv_videoio_runtime_deps)
    foreach(_vision_runtime_artifact IN LISTS
            _vision_opencv_runtime_dlls
            _vision_opencv_videoio_plugins
            _vision_opencv_videoio_runtime_deps)
        if(EXISTS "${_vision_runtime_artifact}")
            add_custom_command(
                TARGET VisionInspectionSystem POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_vision_runtime_artifact}"
                    "$<TARGET_FILE_DIR:VisionInspectionSystem>"
                VERBATIM
            )
        endif()
    endforeach()
endif()

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(VisionInspectionSystem)
endif()

add_executable(VisionOpenCvDiagnostics
    tools/opencv_diagnostics.cpp
)

target_include_directories(VisionOpenCvDiagnostics
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(VisionOpenCvDiagnostics
    PRIVATE
        ${OpenCV_LIBS}
)

if(_vision_opencv_runtime_dlls OR _vision_opencv_videoio_plugins OR _vision_opencv_videoio_runtime_deps)
    foreach(_vision_runtime_artifact IN LISTS
            _vision_opencv_runtime_dlls
            _vision_opencv_videoio_plugins
            _vision_opencv_videoio_runtime_deps)
        if(EXISTS "${_vision_runtime_artifact}")
            add_custom_command(
                TARGET VisionOpenCvDiagnostics POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_vision_runtime_artifact}"
                    "$<TARGET_FILE_DIR:VisionOpenCvDiagnostics>"
                VERBATIM
            )
        endif()
    endforeach()
endif()

include(CTest)

if(BUILD_TESTING)
    if(NOT TARGET Qt${QT_VERSION_MAJOR}::Test)
        message(WARNING "Qt Test component not found; skipping test target setup.")
    else()
        set(_vision_test_path_entries)
        if(VISION_QT_ROOT AND EXISTS "${VISION_QT_ROOT}/bin")
            list(APPEND _vision_test_path_entries "${VISION_QT_ROOT}/bin")
        endif()
        foreach(_vision_runtime_root IN LISTS _vision_opencv_runtime_search_roots)
            if(EXISTS "${_vision_runtime_root}")
                list(APPEND _vision_test_path_entries "${_vision_runtime_root}")
            endif()
        endforeach()
        list(APPEND _vision_test_path_entries "${CMAKE_CURRENT_BINARY_DIR}")
        list(REMOVE_DUPLICATES _vision_test_path_entries)
        string(JOIN ";" _vision_test_path "${_vision_test_path_entries}")

        add_executable(VisionImageProcessorTests
            tests/imageprocessor_tests.cpp
            common/config/constants.h
            common/logging/logevent.h
            common/logging/logmanager.h
            common/logging/logmanager.cpp
            common/logging/logworker.h
            common/logging/logworker.cpp
            domain/entities/capturedframe.h
            domain/entities/inputsource.h
            domain/entities/inspectionresult.h
            domain/entities/recipe.h
            domain/services/imageprocessor.h
            domain/services/imageprocessor.cpp
        )

        target_include_directories(VisionImageProcessorTests
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}
                ${OpenCV_INCLUDE_DIRS}
        )

        target_link_libraries(VisionImageProcessorTests
            PRIVATE
                Qt${QT_VERSION_MAJOR}::Core
                Qt${QT_VERSION_MAJOR}::Gui
                Qt${QT_VERSION_MAJOR}::Test
                ${OpenCV_LIBS}
        )

        add_executable(VisionUtilsTests
            tests/utils_tests.cpp
            common/utils/utils.h
            common/utils/utils.cpp
            domain/entities/capturedframe.h
            domain/entities/inputsource.h
            domain/entities/inspectionresult.h
            domain/entities/recipe.h
        )

        target_include_directories(VisionUtilsTests
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}
                ${OpenCV_INCLUDE_DIRS}
        )

        target_link_libraries(VisionUtilsTests
            PRIVATE
                Qt${QT_VERSION_MAJOR}::Core
                Qt${QT_VERSION_MAJOR}::Gui
                Qt${QT_VERSION_MAJOR}::Test
                ${OpenCV_LIBS}
        )

        add_executable(VisionConfigManagerTests
            tests/configmanager_tests.cpp
            common/config/constants.h
            domain/entities/deviceconfig.h
            domain/entities/inputsource.h
            domain/entities/recipe.h
            infrastructure/config/configmanager.h
            infrastructure/config/configmanager.cpp
        )

        add_executable(VisionInspectionSessionStateTests
            tests/inspectionsessionstate_tests.cpp
            application/state/inspectionsessionstate.h
        )

        add_executable(VisionInspectionOrchestratorTests
            tests/inspectionorchestrator_tests.cpp
            application/orchestrators/inspectionorchestrator.h
            application/orchestrators/inspectionorchestrator.cpp
            application/state/inspectionsessionstate.h
            common/utils/utils.h
            common/utils/utils.cpp
            domain/entities/capturedframe.h
            domain/entities/deviceconfig.h
            domain/entities/inputsource.h
            domain/entities/inspectiontask.h
            domain/entities/recipe.h
        )

        add_executable(VisionResultDispatcherTests
            tests/resultdispatcher_tests.cpp
            application/orchestrators/resultdispatcher.h
            application/orchestrators/resultdispatcher.cpp
            common/logging/logevent.h
            common/logging/logmanager.h
            common/logging/logmanager.cpp
            common/logging/logworker.h
            common/logging/logworker.cpp
            common/utils/utils.h
            common/utils/utils.cpp
            domain/entities/capturedframe.h
            domain/entities/deviceconfig.h
            domain/entities/inputsource.h
            domain/entities/inspectionoutput.h
            domain/entities/inspectionresult.h
            domain/entities/inspectiontask.h
            domain/entities/recipe.h
        )

        add_executable(VisionInspectionWorkerTests
            tests/inspectionworker_tests.cpp
            common/logging/logevent.h
            common/logging/logmanager.h
            common/logging/logmanager.cpp
            common/logging/logworker.h
            common/logging/logworker.cpp
            common/utils/utils.h
            common/utils/utils.cpp
            domain/entities/capturedframe.h
            domain/entities/inputsource.h
            domain/entities/inspectionoutput.h
            domain/entities/inspectionresult.h
            domain/entities/inspectiontask.h
            domain/entities/recipe.h
            domain/services/imageprocessor.h
            domain/services/imageprocessor.cpp
            infrastructure/vision/inspectionworker.h
            infrastructure/vision/inspectionworker.cpp
        )

        add_executable(VisionInspectionPersistenceTests
            tests/persistenceworker_tests.cpp
            common/config/constants.h
            common/utils/utils.h
            common/utils/utils.cpp
            domain/entities/capturedframe.h
            domain/entities/inputsource.h
            domain/entities/inspectionoutput.h
            domain/entities/inspectionrecord.h
            domain/entities/inspectionresult.h
            domain/entities/inspectiontask.h
            domain/entities/persistenceresult.h
            domain/entities/recipe.h
            infrastructure/storage/databasemanager.h
            infrastructure/storage/databasemanager.cpp
            infrastructure/storage/inspectionpersistenceworker.h
            infrastructure/storage/inspectionpersistenceworker.cpp
            infrastructure/storage/recordmanager.h
            infrastructure/storage/recordmanager.cpp
        )

        target_include_directories(VisionConfigManagerTests
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}
                ${OpenCV_INCLUDE_DIRS}
        )

        target_link_libraries(VisionConfigManagerTests
            PRIVATE
                Qt${QT_VERSION_MAJOR}::Core
                Qt${QT_VERSION_MAJOR}::Test
                ${OpenCV_LIBS}
        )

        target_include_directories(VisionInspectionSessionStateTests
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}
        )

        target_link_libraries(VisionInspectionSessionStateTests
            PRIVATE
                Qt${QT_VERSION_MAJOR}::Core
                Qt${QT_VERSION_MAJOR}::Test
        )

        target_include_directories(VisionInspectionOrchestratorTests
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}
                ${OpenCV_INCLUDE_DIRS}
        )

        target_link_libraries(VisionInspectionOrchestratorTests
            PRIVATE
                Qt${QT_VERSION_MAJOR}::Core
                Qt${QT_VERSION_MAJOR}::Gui
                Qt${QT_VERSION_MAJOR}::Test
                ${OpenCV_LIBS}
        )

        target_include_directories(VisionResultDispatcherTests
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}
                ${OpenCV_INCLUDE_DIRS}
        )

        target_link_libraries(VisionResultDispatcherTests
            PRIVATE
                Qt${QT_VERSION_MAJOR}::Core
                Qt${QT_VERSION_MAJOR}::Gui
                Qt${QT_VERSION_MAJOR}::Test
                ${OpenCV_LIBS}
        )

        target_include_directories(VisionInspectionWorkerTests
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}
                ${OpenCV_INCLUDE_DIRS}
        )

        target_link_libraries(VisionInspectionWorkerTests
            PRIVATE
                Qt${QT_VERSION_MAJOR}::Core
                Qt${QT_VERSION_MAJOR}::Gui
                Qt${QT_VERSION_MAJOR}::Test
                ${OpenCV_LIBS}
        )

        target_include_directories(VisionInspectionPersistenceTests
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}
                ${OpenCV_INCLUDE_DIRS}
        )

        target_link_libraries(VisionInspectionPersistenceTests
            PRIVATE
                Qt${QT_VERSION_MAJOR}::Core
                Qt${QT_VERSION_MAJOR}::Gui
                Qt${QT_VERSION_MAJOR}::Sql
                Qt${QT_VERSION_MAJOR}::Test
                ${OpenCV_LIBS}
        )

        add_test(NAME VisionImageProcessorTests COMMAND VisionImageProcessorTests)
        add_test(NAME VisionUtilsTests COMMAND VisionUtilsTests)
        add_test(NAME VisionConfigManagerTests COMMAND VisionConfigManagerTests)
        add_test(NAME VisionInspectionSessionStateTests COMMAND VisionInspectionSessionStateTests)
        add_test(NAME VisionInspectionOrchestratorTests COMMAND VisionInspectionOrchestratorTests)
        add_test(NAME VisionResultDispatcherTests COMMAND VisionResultDispatcherTests)
        add_test(NAME VisionInspectionWorkerTests COMMAND VisionInspectionWorkerTests)
        add_test(NAME VisionInspectionPersistenceTests COMMAND VisionInspectionPersistenceTests)
        set_tests_properties(
            VisionImageProcessorTests
            VisionUtilsTests
            VisionConfigManagerTests
            VisionInspectionSessionStateTests
            VisionInspectionOrchestratorTests
            VisionResultDispatcherTests
            VisionInspectionWorkerTests
            VisionInspectionPersistenceTests
            PROPERTIES
                ENVIRONMENT "PATH=${_vision_test_path};$ENV{PATH}"
        )
    endif()
endif()
