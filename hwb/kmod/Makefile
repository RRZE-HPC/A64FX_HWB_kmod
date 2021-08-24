INSTALL_DIR=modules

ifneq (${KERNELRELEASE},)
	obj-m := a64fx_hwb.o
	#a64fx-objs := a64fx_sysfs.o
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
	@rm -rf ${INSTALL_DIR}
	@mkdir ${INSTALL_DIR}
	@mv -f *.o *.ko *.mod.c .*.cmd ${INSTALL_DIR}
clean:
	rm -rf ${INSTALL_DIR} *.o *.mod.c .*.cmd
	rm -f *.dwo *.mod modules.order Module.symvers

endif
