include $(ROOT)/build/common.mk

NAME      := nested-svm-vmrun
CATEGORY  := in-development
TEST-ENVS := hvm64

TEST-EXTRA-CFG := extra.cfg.in

obj-perenv += ../nested-svm/l2.o ../nested-svm/vmrun.o main.o

include $(ROOT)/build/gen.mk
