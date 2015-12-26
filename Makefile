
modules:
	(cd drivers && make)

modules_install:
	(cd drivers && make modules_install)

clean:
	(cd drivers && make clean)

zc_deploy:
	mkdir -p /lib/modules/$(shell uname -r)/extra/
	cp drivers/ac_zc.ko /lib/modules/$(shell uname -r)/extra/
	depmod -a

zc_stop:
	rmmod ac_zc

zc_start:
	modprobe ac_zc ac_zc_gpio=4

zc_restart: zc_stop zc_start

dimmer_deploy:
	mkdir -p /lib/modules/$(shell uname -r)/extra/
	cp drivers/ac_dimmer.ko /lib/modules/$(shell uname -r)/extra/
	depmod -a

dimmer_stop:
	rmmod ac_dimmer

dimmer_start:
	modprobe ac_dimmer

dimmer_restart: dimmer_stop dimmer_start

button_deploy:
	mkdir -p /lib/modules/$(shell uname -r)/extra/
	cp drivers/ac_button.ko /lib/modules/$(shell uname -r)/extra/
	depmod -a

button_stop:
	rmmod ac_button

button_start:
	modprobe ac_button

button_restart: button_stop button_start

admin_deploy:
	(cd admin && make install)

admin_undeploy:
	(cd admin && make uninstall)

deploy: zc_deploy dimmer_deploy button_deploy admin_deploy

stop: dimmer_stop button_stop zc_stop

start: zc_start dimmer_start button_start

restart: stop start

deploy-boot:
	cp modules-load.d_mylife-home-ac.conf /etc/modules-load.d/mylife-home-ac.conf
	cp modprobe.d_mylife-home-ac.conf /etc/modprobe.d/mylife-home-ac.conf

undeploy: admin_undeploy
	rmmod ac_dimmer && 0
	rmmod ac_button && 0
	rmmod ac_zc && 0
	rm -f /lib/modules/$(shell uname -r)/extra/ac_button.ko
	rm -f /lib/modules/$(shell uname -r)/extra/ac_dimmer.ko
	rm -f /lib/modules/$(shell uname -r)/extra/ac_zc.ko
	rm -f /etc/modules-load.d/mylife-home-ac.conf