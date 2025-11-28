# indicating that target "all", "clean", "load" and "unload" are not files
.PHONY: all clean load unload
obj-m := kfetch_mod_313552022.o
# set some variables
EXTRA_CFLAGS := -w
PWD := $(shell pwd)


# first command of make
all: 
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

# load and unload the module
load:
	sudo insmod kfetch_mod_313552022.ko

unload:
	sudo rmmod kfetch_mod_313552022