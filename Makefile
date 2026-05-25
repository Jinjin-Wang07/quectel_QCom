ifneq ($(CROSS_COMPILE),)
CROSS-COMPILE:=$(CROSS_COMPILE)
endif
#CROSS-COMPILE:=/workspace/buildroot/buildroot-qemu_mips_malta_defconfig/output/host/usr/bin/mips-buildroot-linux-uclibc-
#CROSS-COMPILE:=/workspace/buildroot/buildroot-qemu_arm_vexpress_defconfig/output/host/usr/bin/arm-buildroot-linux-uclibcgnueabi-
#CROSS-COMPILE:=/workspace/buildroot-git/qemu_mips64_malta/output/host/usr/bin/mips-gnu-linux-
ifeq ($(CC),cc)
CC:=$(CROSS-COMPILE)gcc
endif
LD:=$(CROSS-COMPILE)ld
SRC_DIR := src

QL_CM_SRC=$(SRC_DIR)/QmiWwanCM.c $(SRC_DIR)/GobiNetCM.c $(SRC_DIR)/main.c $(SRC_DIR)/QCQMUX.c $(SRC_DIR)/QMIThread.c $(SRC_DIR)/util.c $(SRC_DIR)/qmap_bridge_mode.c $(SRC_DIR)/mbim-cm.c $(SRC_DIR)/device.c
QL_CM_SRC+=$(SRC_DIR)/atc.c $(SRC_DIR)/atchannel.c $(SRC_DIR)/at_tok.c
#QL_CM_SRC+=$(SRC_DIR)/qrtr.c $(SRC_DIR)/rmnetctl.c
ifeq (1,1)
QL_CM_DHCP=$(SRC_DIR)/udhcpc.c
else
LIBMNL=libmnl/ifutils.c libmnl/attr.c libmnl/callback.c libmnl/nlmsg.c libmnl/socket.c
DHCP=libmnl/dhcp/dhcpclient.c libmnl/dhcp/dhcpmsg.c libmnl/dhcp/packet.c
QL_CM_DHCP=$(SRC_DIR)/udhcpc_netlink.c
QL_CM_DHCP+=${LIBMNL}
endif

CFLAGS += -Wall -Wextra -Werror -O1 #-s
LDFLAGS += -lpthread -ldl -lrt
OUT_DIR := ./out

release: qmi-proxy mbim-proxy atc-proxy | $(OUT_DIR) #qrtr-proxy
	$(CC) ${CFLAGS} ${QL_CM_SRC} ${QL_CM_DHCP} -o $(OUT_DIR)/quectel-CM ${LDFLAGS}

debug: | $(OUT_DIR)
	$(CC) ${CFLAGS} -g -DCM_DEBUG ${QL_CM_SRC} ${QL_CM_DHCP} -o $(OUT_DIR)/quectel-CM -lpthread -ldl -lrt

qmi-proxy: | $(OUT_DIR)
	$(CC) ${CFLAGS} $(SRC_DIR)/quectel-qmi-proxy.c -o $(OUT_DIR)/quectel-qmi-proxy ${LDFLAGS} 

mbim-proxy: | $(OUT_DIR)
	$(CC) ${CFLAGS} $(SRC_DIR)/quectel-mbim-proxy.c -o $(OUT_DIR)/quectel-mbim-proxy ${LDFLAGS} 

qrtr-proxy: | $(OUT_DIR)
	$(CC) ${CFLAGS} $(SRC_DIR)/quectel-qrtr-proxy.c -o $(OUT_DIR)/quectel-qrtr-proxy ${LDFLAGS} 

atc-proxy: | $(OUT_DIR)
	$(CC) ${CFLAGS} $(SRC_DIR)/quectel-atc-proxy.c $(SRC_DIR)/atchannel.c $(SRC_DIR)/at_tok.c $(SRC_DIR)/util.c -o $(OUT_DIR)/quectel-atc-proxy ${LDFLAGS} 

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

clean:
	rm -rf *.o libmnl/*.o $(OUT_DIR)
