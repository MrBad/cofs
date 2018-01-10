obj-m := cofs.o
cofs-objs := super.o inode.o dir.o file.o block.o

CFLAGS_super.o :=-DDEBUG
CFLAGS_inode.o :=-DDEBUG
CFLAGS_block.o :=-DDEBUG
CFLAGS_dir.o :=-DDEBUG

MYFLAGS = -g -Wall -Wextra -std=c99 -pedantic
CFLAGS =
all: mkfs
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

mkfs: mkfs.c
	$(CC) $(CFLAGS) $(MYFLAGS) -o $@ \
		$(LDFLAGS) $(LOADLIBES) $(LDLIBS) $<

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	sudo umount /mnt || true
	sudo losetup -d /dev/loop4 || true
	sudo rmmod cofs || true
	rm mkfs

run: all
	sudo insmod cofs.ko
	#sudo losetup /dev/loop4 -o 104858112 ../../hdd.img
	sudo losetup /dev/loop4 hdd.img
	sleep 1
	sudo mount /dev/loop4 /mnt


