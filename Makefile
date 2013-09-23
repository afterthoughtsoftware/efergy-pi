KERNEL_BUILD=../linux

obj-m := efergy.o

all:
	$(MAKE) -C $(KERNEL_BUILD) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNEL_BUILD) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KERNEL_BUILD) M=$(PWD) clean
