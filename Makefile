
DRIVER=am2320
VERSION=0.1

obj-m = $(DRIVER).o

DKMS_FLAGS= -m $(DRIVER) -v $(VERSION)
DKMS_ROOT_PATH=/usr/src/$(DRIVER)-$(VERSION)

KERNEL_BUILD=/lib/modules/`uname -r`/build

.PHONY: all modules clean dkms dkms_clean dtoverlay dtoverlay_clean

all: modules

modules:
	@$(MAKE) -C $(KERNEL_BUILD) M=$(PWD) $@

clean:
	@$(MAKE) -C $(KERNEL_BUILD) M=$(PWD) $@

dkms:
	@mkdir $(DKMS_ROOT_PATH)
	@cp `pwd`/dkms.conf $(DKMS_ROOT_PATH)
	@cp `pwd`/Makefile $(DKMS_ROOT_PATH)
	@cp `pwd`/$(DRIVER).c $(DKMS_ROOT_PATH)
	@dkms add $(DKMS_FLAGS)
	@dkms build $(DKMS_FLAGS)
	@dkms install --force $(DKMS_FLAGS)
	@modprobe $(DRIVER)

dkms_clean:
	@dkms remove $(DKMS_FLAGS) --all
	@rm -rf $(DKMS_ROOT_PATH)

dtoverlay:
	@dtc -@ -I dts -O dtb -o am2320.dtbo am2320.dts
	@cp am2320.dtbo /boot/overlays/
	@echo "dtoverlay=am2320" >> /boot/firmware/config.txt

dtoverlay_clean:
	@rm /boot/overlays/am2320.dtbo
	@sed -i s/^dtoverlay=am2320//g /boot/firmware/config.txt
