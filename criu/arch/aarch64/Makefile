builtin-name		:= crtools.built-in.o

ccflags-y		+= -iquote $(obj) -iquote $(SRC_DIR)
ccflags-y		+= -iquote $(obj)/include -iquote $(SRC_DIR)/criu/include
ccflags-y		+= -iquote $(SRC_DIR)/include
ccflags-y		+= -I compel/include/uapi
ccflags-y		+= -I compel/plugins/include/uapi

obj-y			+= cpu.o
obj-y			+= crtools.o
obj-y			+= sigframe.o
