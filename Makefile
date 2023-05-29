obj-m += msgfilefs.o

msgfilefs-objs += msgfilefs_src.o fileops.o dirops.o lib/scth.o syscalls.o bitmask_handler.o
EXTRA_CFLAGS:= -D MAXBLOCKS=20 -D SYNCHRONIZE 

A = $(shell cat /sys/module/the_usctm/parameters/sys_call_table_address)


all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules


insmod:
	insmod ./msgfilefs.ko the_syscall_table=$(A)

rmmod:
	rmmod ./msgfilefs.ko 

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rmdir msgfs

create-fs:
	dd bs=4096 count=10 if=/dev/zero of=image
	./msgfilefs_format image
	mkdir msgfs
	
mount-fs:
	mount -o loop -t msgfilefs image ./msgfs/

unmount-fs:
	umount msgfs
	

git:
	git add bitmask_handler.c 
	git add fileops.c 
	git add dirops.c 
	git add msgfilefs_src.c 
	git add msgfilefs_format.c
	git add msgfilefs.h 
	git add msgfilefs_kernel.h
	git add Makefile
	git add syscalls.c 
	git add test.c 
	git add test2.c

compile:
	gcc msgfilefs_format.c -o msgfilefs_format
	gcc test.c -o test1
	gcc test2.c -o test2
