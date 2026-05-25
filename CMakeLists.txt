cmake_minimum_required(VERSION 2.4) 

project(quectel-CM) 
add_definitions(-Wall -Wextra -Werror -O1)
option(USE_QRTR "Enable QRTR" OFF) 
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

set( QL_CM_SRC
     ${SRC_DIR}/QmiWwanCM.c ${SRC_DIR}/GobiNetCM.c ${SRC_DIR}/main.c ${SRC_DIR}/QCQMUX.c ${SRC_DIR}/QMIThread.c ${SRC_DIR}/util.c ${SRC_DIR}/qmap_bridge_mode.c ${SRC_DIR}/mbim-cm.c ${SRC_DIR}/device.c
     ${SRC_DIR}/atc.c ${SRC_DIR}/atchannel.c ${SRC_DIR}/at_tok.c
     ${SRC_DIR}/udhcpc.c
     )

if(USE_QRTR) 
add_definitions(-DCONFIG_QRTR)
set( QRTR_SRC ${SRC_DIR}/qrtr.c ${SRC_DIR}/rmnetctl.c)
endif() 

add_executable(quectel-CM ${QL_CM_SRC} ${QRTR_SRC})
target_link_libraries(quectel-CM PUBLIC pthread) 
install (TARGETS quectel-CM DESTINATION bin)

add_executable(quectel-qmi-proxy ${SRC_DIR}/quectel-qmi-proxy.c)
target_link_libraries(quectel-qmi-proxy PUBLIC pthread) 
install (TARGETS quectel-qmi-proxy DESTINATION bin)

add_executable(quectel-mbim-proxy ${SRC_DIR}/quectel-mbim-proxy.c)
target_link_libraries(quectel-mbim-proxy PUBLIC pthread) 
install (TARGETS quectel-mbim-proxy DESTINATION bin)

add_executable(quectel-atc-proxy ${SRC_DIR}/quectel-atc-proxy.c ${SRC_DIR}/atchannel.c ${SRC_DIR}/at_tok.c ${SRC_DIR}/util.c)
target_link_libraries(quectel-atc-proxy PUBLIC pthread) 
install (TARGETS quectel-atc-proxy DESTINATION bin)

#add_executable(quectel-qrtr-proxy ${SRC_DIR}/quectel-qrtr-proxy.c)
#target_link_libraries(quectel-qrtr-proxy PUBLIC pthread) 
#install (TARGETS quectel-qrtr-proxy DESTINATION bin)
