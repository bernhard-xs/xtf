include $(ROOT)/build/common.mk

NAME      := nested-svm-vmload-negative
CATEGORY  := nested-svm
TEST-ENVS := $(SVM_ENVIRONMENTS)

TEST-EXTRA-CFG := extra.cfg.in

obj-perenv += main.o $(ROOT)/common/nested-svm/test-harness.o

include $(ROOT)/build/gen.mk
