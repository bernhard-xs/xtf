include $(ROOT)/build/common.mk

NAME      := nested-svm-slave
CATEGORY  := utility
TEST-ENVS := hvm64

TEST-EXTRA-CFG := extra.cfg.in

obj-perenv += main.o
obj-perenv += $(ROOT)/common/nested-svm/setup-l2.o

include $(ROOT)/build/gen.mk
