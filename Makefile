obj-m := cofs.o
cofs-objs := super.o inode.o dir.o file.o block.o

CFLAGS_super.o :=-DDEBUG
CFLAGS_dir.o :=-DDEBUG

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	sudo umount /mnt || true
	sudo losetup -d /dev/loop4 || true
	sudo rmmod cofs || true

run: all
	sudo insmod cofs.ko
	sudo losetup /dev/loop4 -o 104858112 ../../hdd.img
	sudo mount /dev/loop4 /mnt


