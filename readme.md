
The goal of this project is to make [Under Control dualshock3 controllers](https://duckduckgo.com/?q=under+control+dualshock+3&t=ffab&iax=images&ia=images)
works with bluetooth on linux.

Installation
============

Follow the [gentoo wiki about dualshock controllers](https://wiki.gentoo.org/wiki/Sony_DualShock)
to configure bluetooth and pair the controller.

First you need to patch your kernel with *kernel_patch.diff* :

	patch -d /kernel/sources/ -p1 < kernel_patch.diff
	make -C /kernel/sources/
	make -C /kernel/sources/ install modules_install
	reboot

then build and install the module :

	make
	make install

and before connecting your controller :

	modprobe uc_ds3

Informations
============

This is an expermimental module made only by observing how this controllers
and the linux kernel works. I dont have official ps3 controller nor ps3
console so understanding the communication expected by this unofficial ps3
controller is hard, informations obout this are welcome.  Also this is my
first time writing code in the kernel environnement so my approach and my
coding style is probably not the best, feedback on this would be very
appreciated.

Notes
=====

This module was tested on a x86_64 machine with a 5.4.60 linux kernel with up
to 2 Under Control controllers connected at the same time. Others dualshock
controllers and others bluetooth devices may not work with this module loaded.

Todo
====

- Find a way to modify L2CAP packets without patching the kernel
- Make the led/forcefeedback works
