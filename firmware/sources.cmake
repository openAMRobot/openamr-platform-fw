# Portable firmware sources (core, adapters, safety core), shared by the
# STM32 board build (boards/stm32/OpenAMRobot) and the host tests (tests/host/core).
# Nothing listed here may include a HAL, register or communication-stack header.

set(OAMR_FIRMWARE_DIR ${CMAKE_CURRENT_LIST_DIR})

set(OAMR_FIRMWARE_INCLUDE_DIRS
    ${OAMR_FIRMWARE_DIR}/core/include
    ${OAMR_FIRMWARE_DIR}/motor_controller_bridge/include
    ${OAMR_FIRMWARE_DIR}/sensor_bridge/include
    ${OAMR_FIRMWARE_DIR}/battery_monitor/include
    ${OAMR_FIRMWARE_DIR}/safety_io/include
    ${OAMR_FIRMWARE_DIR}/app/include
)

set(OAMR_FIRMWARE_SOURCES
    ${OAMR_FIRMWARE_DIR}/core/src/motion.c
    ${OAMR_FIRMWARE_DIR}/core/src/can_health.c
    ${OAMR_FIRMWARE_DIR}/core/src/commissioning.c
    ${OAMR_FIRMWARE_DIR}/core/src/odometry.c
    ${OAMR_FIRMWARE_DIR}/motor_controller_bridge/src/canopen_sdo.c
    ${OAMR_FIRMWARE_DIR}/motor_controller_bridge/src/zlac8015d.c
    ${OAMR_FIRMWARE_DIR}/sensor_bridge/src/icm42688.c
    ${OAMR_FIRMWARE_DIR}/battery_monitor/src/daly_bms.c
    ${OAMR_FIRMWARE_DIR}/safety_io/src/safety_core.c
)

# Board-independent application composition. Used by the host board
# (boards/host); the STM32 port still has its own copy in port_app.c
# (TODO(Evans): switch it over), so it is a separate list and the STM32
# image does not change.
set(OAMR_APP_SOURCES
    ${OAMR_FIRMWARE_DIR}/app/src/base_app.c
    ${OAMR_FIRMWARE_DIR}/app/src/base_status.c
)
