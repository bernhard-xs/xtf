MAKEFLAGS += -rR
ROOT := $(abspath $(CURDIR))
export ROOT

# Default to the all rule
all:

# Local settings and rules
-include Makefile.local

# $(xtfdir) defaults to $(ROOT) so development and testing can be done
# straight out of the working tree.
xtfdir  ?= $(ROOT)
DESTDIR ?= $(ROOT)/dist

ifeq ($(filter /%,$(xtfdir)),)
$(error $$(xtfdir) must be absolute, not '$(xtfdir)')
endif

ifneq ($(DESTDIR),)
ifeq ($(filter /%,$(DESTDIR)),)
$(error $$(DESTDIR) must be absolute, not '$(DESTDIR)')
endif
endif

xtftestdir := $(xtfdir)/tests

export DESTDIR xtfdir xtftestdir

ifeq ($(LLVM),) # GCC toolchain
CC              := $(CROSS_COMPILE)gcc
LD              := $(CROSS_COMPILE)ld
OBJCOPY         := $(CROSS_COMPILE)objcopy

else # LLVM toolchain

# Optional -$NUM version when multiple toolchains are installed
ver := $(filter -%,$(LLVM))
CC              := clang$(ver) $(if $(CROSS_COMPILE),--target=$(notdir $(CROSS_COMPILE:%-=%)))
LD              := ld.lld$(ver)
OBJCOPY         := llvm-objcopy$(ver)
undefine ver

endif

CPP             := $(CC) -E
INSTALL         := install
INSTALL_DATA    := $(INSTALL) -m 644 -p
INSTALL_DIR     := $(INSTALL) -d -p
INSTALL_PROGRAM := $(INSTALL) -p

# Best effort attempt to find a python interpreter, defaulting to Python 3 if
# available.  Fall back to just `python`.
PYTHON_INTERPRETER := $(word 1,$(shell command -v python3 || command -v python || command -v python2) python)
PYTHON             ?= $(PYTHON_INTERPRETER)

export CC LD CPP INSTALL INSTALL_DATA INSTALL_DIR INSTALL_PROGRAM OBJCOPY PYTHON

# By default enable all the tests
TESTS ?= $(wildcard $(ROOT)/tests/*)

ifneq ($(filter metadata-tests nonrecursive-build nonrecursive-install ninja-vars,$(MAKECMDGOALS)),)
include $(ROOT)/build/load-tests.mk
endif

ifneq ($(filter nonrecursive-build nonrecursive-install ninja-vars,$(MAKECMDGOALS)),)
include $(ROOT)/build/common.mk
endif

ifneq ($(filter nonrecursive-build nonrecursive-install,$(MAKECMDGOALS)),)
include $(ROOT)/build/emit-tests.mk
endif

# Convert the selected test directories into explicit top-level targets so GNU
# make can schedule independent tests in parallel, rather than hiding the work
# behind one shell loop.

TEST_MAKEFILES := $(wildcard $(TESTS:%=%/Makefile))
BUILD_TARGETS := $(patsubst %/Makefile,%/.build,$(TEST_MAKEFILES))
INSTALL_TARGETS := $(patsubst %/Makefile,%/.install,$(TEST_MAKEFILES))

# Multiple test sub-makes rebuild the same objects under common/ and arch/.
# Seed those shared artefacts once before the parallel fan-out, but skip the
# bootstrap entirely when only one test was selected so TESTS filtering keeps
# its expected no-op behaviour.

ifneq ($(word 2,$(BUILD_TARGETS)),)
SHARED_BOOTSTRAP_TARGET := $(firstword $(BUILD_TARGETS:.build=.shared-ready))
endif

.PHONY: all $(BUILD_TARGETS) $(INSTALL_TARGETS)
all: $(SHARED_BOOTSTRAP_TARGET) $(BUILD_TARGETS)

# Leading '+' preserves jobserver recursion when the parent was run with -j.
$(SHARED_BOOTSTRAP_TARGET):
	+$(MAKE) -C $(@D) build

# Each selected test directory now appears as a first-class prerequisite.
$(BUILD_TARGETS): | $(SHARED_BOOTSTRAP_TARGET)
	+$(MAKE) -C $(@D) build

.PHONY: install
install:
	@$(INSTALL_DIR) $(DESTDIR)$(xtfdir)
	$(INSTALL_PROGRAM) xtf-runner $(DESTDIR)$(xtfdir)

install: $(SHARED_BOOTSTRAP_TARGET) $(INSTALL_TARGETS)

$(INSTALL_TARGETS): | $(SHARED_BOOTSTRAP_TARGET)
	+$(MAKE) -C $(@D) install

define all_sources
	find include/ arch/ common/ tests/ -name "*.[hcsS]"
endef

.PHONY: cscope
cscope:
	$(all_sources) > cscope.files
	cscope -b -q -k

.PHONY: metadata-tests
metadata-tests:
	@$(if $(REGISTERED_TESTS),:,echo "No test metadata loaded" && false)
	@$(foreach key,$(REGISTERED_TESTS),printf '%s\n' '$(key): dir=$(TEST_DIR_$(key)) name=$(TEST_NAME_$(key)) category=$(TEST_CATEGORY_$(key)) envs=$(TEST_ENVS_$(key)) extra_cfg=$(TEST_EXTRA_CFG_$(key)) vary_cfg=$(TEST_VARY_CFG_$(key)) vcpus=$(TEST_VCPUS_$(key)) objs=$(TEST_LOCAL_OBJ_PERENV_$(key))';)

.PHONY: nonrecursive-build nonrecursive-install ninja-vars ninja-file ninja-build
nonrecursive-build: $(NR_BUILD_TARGETS)

nonrecursive-install: $(NR_INSTALL_TARGETS)

ninja-vars:
	@printf 'global\t%s\t%s\n' ROOT '$(ROOT)'
	@printf 'global\t%s\t%s\n' CC '$(CC)'
	@printf 'global\t%s\t%s\n' CPP '$(CPP)'
	@printf 'global\t%s\t%s\n' LD '$(LD)'
	@printf 'global\t%s\t%s\n' OBJCOPY '$(OBJCOPY)'
	@printf 'global\t%s\t%s\n' PYTHON '$(PYTHON)'
	@printf 'global\t%s\t%s\n' HVM64_FORMAT '$(firstword $(filter elf32-x86-64,$(shell $(OBJCOPY) --help)) elf32-i386)'
	@printf 'objects\t%s\t%s\n' perbits '$(obj-perbits)'
	@printf 'objects\t%s\t%s\n' perenv '$(obj-perenv)'
	@$(foreach env,$(ALL_ENVIRONMENTS),printf 'env\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' '$(env)' '$($(env)_guest)' '$($(env)_arch)' '$(AFLAGS_$($(env)_arch))' '$(CFLAGS_$($(env)_arch))' '$(AFLAGS_$(env))' '$(CFLAGS_$(env))' '$(link-$(env))' '$(LDFLAGS_$(env))' '$(defcfg-$($(env)_guest))';)
	@$(foreach env,$(ALL_ENVIRONMENTS),printf 'env_objects\t%s\t%s\n' '$(env)' '$(obj-$(env))';)
	@$(foreach key,$(REGISTERED_TESTS),printf 'test\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' '$(key)' '$(TEST_DIR_$(key))' '$(TEST_NAME_$(key))' '$(TEST_CATEGORY_$(key))' '$(TEST_ENVS_$(key))' '$(TEST_EXTRA_CFG_$(key))' '$(TEST_VARY_CFG_$(key))' '$(TEST_VCPUS_$(key))' '$(TEST_LOCAL_OBJ_PERENV_$(key))';)

ninja-file:
	@$(MAKE) -s ninja-vars TESTS='$(TESTS)' > $(ROOT)/build/xtf.ninja.vars
	@cd $(ROOT) && $(PYTHON) build/gen-ninja.py build/xtf.ninja.vars build/xtf.ninja

ninja-build: ninja-file
	cd $(ROOT) && ninja -f build/xtf.ninja

.PHONY: gtags
gtags:
	$(all_sources) | gtags -f -

.PHONY: clean
clean:
	find . \( -name "*.o" -o -name "*.d" -o -name "*.lds" \) -delete
	find tests/ \( -perm -a=x -name "test-*" -o -name "test-*.cfg" \
		-o -name "info.json" \) -delete

.PHONY: distclean
distclean: clean
	find . \( -name "*~" -o -name "cscope*" \) -delete
	rm -rf docs/autogenerated/ dist/

.PHONY: doxygen
doxygen: Doxyfile
	doxygen Doxyfile > /dev/null

.PHONY: pylint
pylint:
	-pylint --rcfile=.pylintrc xtf-runner
