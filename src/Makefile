# http://lxr.free-electrons.com/source/Documentation/kbuild/modules.txt
obj-m = ac_zc.o ac_dimmer.o ac_button.o
ac_zc-y := ac_zc_main.o
ac_dimmer-y := ac_dimmer_main.o
ac_button-y := ac_button_main.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

install:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules_install

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

zc_deploy:
	mkdir -p /lib/modules/$(shell uname -r)/extra/
	cp ac_zc.ko /lib/modules/$(shell uname -r)/extra/
	depmod -a

zc_stop:
	rmmod ac_zc

zc_start:
	modprobe ac_zc ac_zc_gpio=4

zc_restart: zc_stop zc_start

dimmer_deploy:
	mkdir -p /lib/modules/$(shell uname -r)/extra/
	cp ac_dimmer.ko /lib/modules/$(shell uname -r)/extra/
	depmod -a

dimmer_stop:
	rmmod ac_dimmer

dimmer_start:
	modprobe ac_dimmer

dimmer_restart: dimmer_stop dimmer_start

button_deploy:
	mkdir -p /lib/modules/$(shell uname -r)/extra/
	cp ac_button.ko /lib/modules/$(shell uname -r)/extra/
	depmod -a

button_stop:
	rmmod ac_button

button_start:
	modprobe ac_button

button_restart: button_stop button_start

deploy: zc_deploy dimmer_deploy button_deploy

deploy-boot:
	cp mylife-home-ac.conf /etc/modules-load.d/

undeploy:
	rmmod ac_dimmer && 0
	rmmod ac_button && 0
	rmmod ac_zc && 0
	rm -f /lib/modules/$(shell uname -r)/extra/ac_button.ko
	rm -f /lib/modules/$(shell uname -r)/extra/ac_dimmer.ko
	rm -f /lib/modules/$(shell uname -r)/extra/ac_zc.ko
	rm -f /etc/modules-load.d/mylife-home-ac.conf