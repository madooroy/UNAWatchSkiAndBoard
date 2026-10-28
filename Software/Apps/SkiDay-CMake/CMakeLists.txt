# Ski Day: a ski and snowboard day tracker for the UNA Watch. Counts runs and
# lift rides from the barometer and GPS, records a FIT file and warns of a
# storm. Service in Software/Libs, GUI on LVGL.
#
#   export UNA_SDK=/path/to/una-sdk
#   cmake -G "Unix Makefiles" -S Software/Apps/SkiDay-CMake -B build
#   cmake --build build
#
# The .uapp is written to the build directory and copied to Output/.
cmake_minimum_required(VERSION 3.21)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build")

# App configuration
set(APP_NAME "SkiDay")
set(APP_USER_NAME "Ski Day")      # name shown in the watch launcher
set(APP_FILE_NAME "SkiDay")       # .uapp base name
set(APP_TYPE "Activity")
set(DEV_ID "UNA")
# Replace with the App ID generated for this app at https://apps.unawatch.com
# before publishing to the community app store (see README).
set(APP_ID "DC480768426D7784")
set(BUILD_VERSION "1.0.2")        # app version shown in the store; bump per release
set(RESOURCES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Resources")
set(OUTPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../Output")
set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../Libs")
set(GUI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../LVGL-GUI")

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR "UNA_SDK environment variable is not set. Point it at the SDK root.")
endif()

include($ENV{UNA_SDK}/cmake/una-app.cmake)
include($ENV{UNA_SDK}/cmake/una-sdk.cmake)
include(${LIBS_PATH}/libs.cmake)
include(${GUI_PATH}/lvgl-gui.cmake)

project(${APP_NAME})

una_app_setup_version(BUILD_VERSION ${CMAKE_CURRENT_SOURCE_DIR})

# Service: sensors, run tracking, storm watch, FIT file, cues, settings
set(SERVICE_SOURCES
    ${LIBS_SOURCES}
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_SERVICE}
)
set(SERVICE_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${UNA_SDK_INCLUDE_DIRS_SERVICE}
)
una_app_build_service(${APP_NAME}Service.elf)

# GUI (LVGL)
set(GUI_SOURCES
    ${GUI_APP_SOURCES}
    ${LIBS_PATH}/Sources/Ski/Format.cpp     # number formatting
    ${LIBS_PATH}/Sources/SkiTypes.cpp       # day log types
    ${UNA_SDK_SOURCES_COMMON}
    ${UNA_SDK_SOURCES_GUI_LVGL}
)
set(GUI_INCLUDE_DIRS
    ${LIBS_INCLUDE_DIRS}
    ${UNA_SDK_INCLUDE_DIRS_COMMON}
    ${UNA_SDK_INCLUDE_DIRS_GUI_LVGL}
    ${GUI_APP_INCLUDE_DIRS}
)
set(GUI_COMPILE_DEFINITIONS
    ${UNA_SDK_DEFINES_GUI_LVGL}
    "LV_MEM_SIZE=(96U*1024U)"   # four screens built once at start (GUI RAM is 600K)
)
# LVGL's software renderer recurses deeper than TouchGFX.
set(GUI_STACK_SIZE "24*1024")
una_app_build_gui(${APP_NAME}GUI.elf)

una_app_build_app()
