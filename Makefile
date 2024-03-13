BINARY 		:= mybdev
KERNEL      := /lib/modules/$(shell uname -r)/build
KMOD_DIR    := $(shell pwd)
OBJECTS 	:= $(patsubst %.c, %.o, $(wildcard *.c))
TARGET_PATH := /lib/modules/$(shell uname -r)/kernel/drivers/block

# use requests based I/O instead of BIO based (submit_bio)
ccflags-y += "-D REQUESTS_BASED"
obj-m += $(BINARY).o
$(BINARY)-objs := main.o mbdev.o

all: driver test

driver:
	make -C $(KERNEL) M=$(KMOD_DIR) modules

install:
	cp -f $(BINARY).ko $(TARGET_PATH)
	depmod -a
test: ctrl.c
	$(CC) -o $@ $^ -DUAPI
clean:
	rm -f *.ko *.order *.symvers
	rm -f *.o *.mod*
	rm -f test ioctl
