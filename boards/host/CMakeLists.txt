# SPDX-License-Identifier: MIT
# Host board port of the base controller (software-in-the-loop chain).
#
#   cmake -S boards/host -B build/host -DMICRO_ROS_PREFIX=$HOME/uros/install
#   cmake --build build/host -j
#
# MICRO_ROS_PREFIX is the install space of a micro_ros_setup "host" firmware
# workspace built with RMW_UXRCE_TRANSPORT=custom (tools/sil_setup.sh does it).
# The core, the adapters and firmware/app are compiled from the same source
# lists as the host unit tests (firmware/sources.cmake).
cmake_minimum_required(VERSION 3.16)
project(oamr_base_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

get_filename_component(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
set(OAMR_FIRMWARE_DIR ${REPO_ROOT}/firmware)
include(${OAMR_FIRMWARE_DIR}/sources.cmake)

if(NOT MICRO_ROS_PREFIX)
  set(MICRO_ROS_PREFIX $ENV{MICRO_ROS_PREFIX})
endif()
if(NOT MICRO_ROS_PREFIX OR NOT EXISTS ${MICRO_ROS_PREFIX}/rclc)
  message(FATAL_ERROR "MICRO_ROS_PREFIX must point to the micro-ROS host install space "
                      "(run tools/sil_setup.sh)")
endif()
if(NOT EXISTS ${MICRO_ROS_PREFIX}/rmw_microxrcedds/include/rmw_microxrcedds_c/config.h)
  message(FATAL_ERROR "rmw_microxrcedds not found under ${MICRO_ROS_PREFIX}")
endif()
file(READ ${MICRO_ROS_PREFIX}/rmw_microxrcedds/include/rmw_microxrcedds_c/config.h _rmwcfg)
if(NOT _rmwcfg MATCHES "define RMW_UXRCE_TRANSPORT_CUSTOM")
  message(FATAL_ERROR "rmw_microxrcedds was not built with RMW_UXRCE_TRANSPORT=custom")
endif()

# Every package in the micro-ROS install space; the ROS 2 base (rcl, rcutils,
# rosidl runtime) comes from /opt/ros/jazzy, as for the micro_ros_setup host
# platform.
set(ROS_PREFIX /opt/ros/jazzy CACHE PATH "ROS 2 Jazzy prefix")
file(GLOB _uros_pkgs LIST_DIRECTORIES true ${MICRO_ROS_PREFIX}/*)
set(UROS_INCLUDES "")
set(UROS_LIBDIRS "")
foreach(p ${_uros_pkgs})
  if(IS_DIRECTORY ${p}/include)
    file(GLOB _subs LIST_DIRECTORIES true ${p}/include/*)
    list(APPEND UROS_INCLUDES ${p}/include ${_subs})
  endif()
  if(IS_DIRECTORY ${p}/lib)
    list(APPEND UROS_LIBDIRS ${p}/lib)
  endif()
endforeach()
file(GLOB _ros_inc LIST_DIRECTORIES true ${ROS_PREFIX}/include/*)

set(UROS_LIBS
    rclc rcl rcutils rmw rmw_microxrcedds microxrcedds_client microcdr
    rosidl_runtime_c
    geometry_msgs__rosidl_typesupport_c geometry_msgs__rosidl_generator_c
    nav_msgs__rosidl_typesupport_c nav_msgs__rosidl_generator_c
    sensor_msgs__rosidl_typesupport_c sensor_msgs__rosidl_generator_c
    diagnostic_msgs__rosidl_typesupport_c diagnostic_msgs__rosidl_generator_c
    std_msgs__rosidl_typesupport_c std_msgs__rosidl_generator_c
    builtin_interfaces__rosidl_typesupport_c builtin_interfaces__rosidl_generator_c)

add_library(oamr_fw STATIC ${OAMR_FIRMWARE_SOURCES} ${OAMR_APP_SOURCES})
target_include_directories(oamr_fw PUBLIC ${OAMR_FIRMWARE_INCLUDE_DIRS})
target_compile_options(oamr_fw PRIVATE -Wall -Wextra -Werror)
target_link_libraries(oamr_fw PUBLIC m)

add_executable(oamr_base_host
    src/main.c src/host_can.c src/host_pty.c src/host_imu.c src/host_ctl.c
    uros/uros_app.c uros/uros_status.c)
target_include_directories(oamr_base_host PRIVATE src uros ${UROS_INCLUDES} ${_ros_inc})
target_compile_options(oamr_base_host PRIVATE -Wall -Wextra -Werror)
target_link_directories(oamr_base_host PRIVATE ${UROS_LIBDIRS} ${ROS_PREFIX}/lib)
target_link_libraries(oamr_base_host PRIVATE oamr_fw ${UROS_LIBS} pthread m)
# Run without sourcing the micro-ROS workspace: every library directory in the
# rpath (target_link_directories alone does not add them).
set(_rpath ${UROS_LIBDIRS} ${ROS_PREFIX}/lib)
set_target_properties(oamr_base_host PROPERTIES
    BUILD_RPATH "${_rpath}"
    SKIP_BUILD_RPATH OFF)
target_link_options(oamr_base_host PRIVATE -Wl,--disable-new-dtags)
