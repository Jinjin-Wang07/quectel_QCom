bin_PROGRAMS = quectel-CM
QL_CM_SRC=src/QmiWwanCM.c src/GobiNetCM.c src/main.c src/QCQMUX.c src/QMIThread.c src/util.c src/qmap_bridge_mode.c src/mbim-cm.c src/device.c
QL_CM_SRC+=src/atc.c src/atchannel.c src/at_tok.c
#QL_CM_SRC+=src/qrtr.c src/rmnetctl.c
QL_CM_DHCP=src/udhcpc.c
if USE_QRTR
quectel_CM_CFLAGS = -DCONFIG_QRTR 
QL_CM_SRC += src/qrtr.c src/rmnetctl.c
if USE_MSM_IPC
quectel_CM_CFLAGS += -DUSE_LINUX_MSM_IPC
endif
endif

quectel_CM_SOURCES = ${QL_CM_SRC} ${QL_CM_DHCP} 

bin_PROGRAMS += quectel-cm-test
quectel_cm_test_SOURCES = src/cm_test_main.c

bin_PROGRAMS += quectel-qmi-proxy
quectel_qmi_proxy_SOURCES = src/quectel-qmi-proxy.c

bin_PROGRAMS += quectel-mbim-proxy
quectel_mbim_proxy_SOURCES = src/quectel-mbim-proxy.c
LIBS = -l pthread
CFLAGS = -Wall -Wextra -Werror -O1
