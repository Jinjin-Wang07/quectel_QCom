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

QL_CM_SRC=QmiWwanCM.c GobiNetCM.c main.c QCQMUX.c QMIThread.c util.c qmap_bridge_mode.c mbim-cm.c device.c
QL_CM_SRC+=atc.c atchannel.c at_tok.c
#QL_CM_SRC+=qrtr.c rmnetctl.c
ifeq (1,1)
QL_CM_DHCP=udhcpc.c
else
LIBMNL=libmnl/ifutils.c libmnl/attr.c libmnl/callback.c libmnl/nlmsg.c libmnl/socket.c
DHCP=libmnl/dhcp/dhcpclient.c libmnl/dhcp/dhcpmsg.c libmnl/dhcp/packet.c
QL_CM_DHCP=udhcpc_netlink.c
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
	$(CC) ${CFLAGS} quectel-qmi-proxy.c -o $(OUT_DIR)/quectel-qmi-proxy ${LDFLAGS} 

mbim-proxy: | $(OUT_DIR)
	$(CC) ${CFLAGS} quectel-mbim-proxy.c -o $(OUT_DIR)/quectel-mbim-proxy ${LDFLAGS} 

qrtr-proxy: | $(OUT_DIR)
	$(CC) ${CFLAGS} quectel-qrtr-proxy.c -o $(OUT_DIR)/quectel-qrtr-proxy ${LDFLAGS} 

atc-proxy: | $(OUT_DIR)
	$(CC) ${CFLAGS} quectel-atc-proxy.c atchannel.c at_tok.c util.c -o $(OUT_DIR)/quectel-atc-proxy ${LDFLAGS} 

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

clean:
	rm -rf *.o libmnl/*.o $(OUT_DIR)
