# Emit a small non-recursive dependency graph from the metadata captured by
# build/load-tests.mk.  This path is explicit and experimental; the default
# recursive top-level targets stay unchanged until parity is proven.

NR_BUILD_TARGETS :=
NR_INSTALL_TARGETS :=

hvm64-format := $(firstword $(filter elf32-x86-64,$(shell $(OBJCOPY) --help)) elf32-i386)

define xtf_emit_test
TEST_INFO_TARGET_$(1) := $$(TEST_DIR_$(1))/info.json
TEST_CFG_TARGETS_$(1) := $$(if $$(TEST_VARY_CFG_$(1)),$$(foreach env,$$(TEST_ENVS_$(1)),$$(foreach vary,$$(TEST_VARY_CFG_$(1)),$$(TEST_DIR_$(1))/test-$$(env)-$$(TEST_NAME_$(1))~$$(vary).cfg)),$$(foreach env,$$(TEST_ENVS_$(1)),$$(TEST_DIR_$(1))/test-$$(env)-$$(TEST_NAME_$(1)).cfg))

NR_BUILD_TARGETS += $$(TEST_INFO_TARGET_$(1)) $$(TEST_CFG_TARGETS_$(1))
NR_INSTALL_TARGETS += install-nr-info-$(1) $$(foreach env,$$(TEST_ENVS_$(1)),install-nr-bin-$(1)-$$(env) install-nr-cfg-$(1)-$$(env))

$$(TEST_INFO_TARGET_$(1)): $(ROOT)/build/mkinfo.py $$(TEST_DIR_$(1))/Makefile
	cd $(ROOT) && $(PYTHON) $$(call root-path,$$<) $$(call root-path,$$@) "$$(TEST_NAME_$(1))" "$$(TEST_CATEGORY_$(1))" "$$(TEST_ENVS_$(1))" "$$(TEST_VARY_CFG_$(1))"

.PHONY: install-nr-info-$(1)
install-nr-info-$(1): $$(TEST_INFO_TARGET_$(1))
	@$(INSTALL_DIR) $(DESTDIR)$(xtftestdir)/$$(TEST_NAME_$(1))
	$(INSTALL_DATA) $$< $(DESTDIR)$(xtftestdir)/$$(TEST_NAME_$(1))

$$(foreach env,$$(TEST_ENVS_$(1)),$$(eval $$(call xtf_emit_test_env,$(1),$$(env))))
endef

define xtf_emit_test_env
TEST_CFG_INPUT_$(1)_$(2) ?= $$(defcfg-$$($(2)_guest))
TEST_DEPS_$(1)_$(2) = \
	$$(obj-perbits:%.o=%-$$($(2)_arch).o) \
	$$(obj-$(2):%.o=%-$(2).o) \
	$$(obj-perenv:%.o=%-$(2).o) \
	$$(TEST_LOCAL_OBJ_PERENV_$(1):%.o=%-$(2).o)

TEST_BIN_TARGET_$(1)_$(2) := $$(TEST_DIR_$(1))/test-$(2)-$$(TEST_NAME_$(1))
TEST_CFG_TARGET_$(1)_$(2) := $$(TEST_DIR_$(1))/test-$(2)-$$(TEST_NAME_$(1)).cfg
TEST_CFG_DEPS_$(1)_$(2) := $(ROOT)/build/mkcfg.py $$(TEST_CFG_INPUT_$(1)_$(2)) $$(TEST_EXTRA_CFG_$(1)) $$(TEST_DIR_$(1))/Makefile

NR_BUILD_TARGETS += $$(TEST_BIN_TARGET_$(1)_$(2))

$$(call fix-existing-deps,$$(link-$(2):%.lds=%.d) $$(TEST_DEPS_$(1)_$(2):%.o=%.d))
-include $$(link-$(2):%.lds=%.d)
-include $$(TEST_DEPS_$(1)_$(2):%.o=%.d)

ifneq ($(2),hvm64)
$$(TEST_BIN_TARGET_$(1)_$(2)): $$(TEST_DEPS_$(1)_$(2)) $$(link-$(2))
	cd $(ROOT) && $(LD) $$(LDFLAGS_$(2)) \
		$$(foreach dep,$$(TEST_DEPS_$(1)_$(2)),$$(call root-path,$$(dep))) \
		-o $$(call root-path,$$@)
else
$$(TEST_BIN_TARGET_$(1)_$(2)): $$(TEST_DEPS_$(1)_$(2)) $$(link-$(2))
	cd $(ROOT) && $(LD) $$(LDFLAGS_$(2)) \
		$$(foreach dep,$$(TEST_DEPS_$(1)_$(2)),$$(call root-path,$$(dep))) \
		-o $$(call root-path,$$@).tmp
	cd $(ROOT) && $(OBJCOPY) $$(call root-path,$$@).tmp -O $(hvm64-format) $$(call root-path,$$@)
	rm -f $$(call root-path,$$@).tmp
endif

$$(TEST_CFG_TARGET_$(1)_$(2)): $$(TEST_CFG_DEPS_$(1)_$(2))
	cd $(ROOT) && $(PYTHON) $$(call root-path,$$<) $$(call root-path,$$@) "$$(TEST_CFG_INPUT_$(1)_$(2))" "$$(TEST_VCPUS_$(1))" "$$(TEST_EXTRA_CFG_$(1))" ""

$$(TEST_DIR_$(1))/test-$(2)-$$(TEST_NAME_$(1))~%.cfg: $$(TEST_CFG_DEPS_$(1)_$(2)) $$(TEST_DIR_$(1))/%.cfg.in
	cd $(ROOT) && $(PYTHON) $$(call root-path,$$<) $$(call root-path,$$@) "$$(TEST_CFG_INPUT_$(1)_$(2))" "$$(TEST_VCPUS_$(1))" "$$(TEST_EXTRA_CFG_$(1))" "$$(call root-path,$$(TEST_DIR_$(1))/$$*.cfg.in)"

$$(TEST_DIR_$(1))/test-$(2)-$$(TEST_NAME_$(1))~%.cfg: $$(TEST_CFG_DEPS_$(1)_$(2)) $(ROOT)/config/%.cfg.in
	cd $(ROOT) && $(PYTHON) $$(call root-path,$$<) $$(call root-path,$$@) "$$(TEST_CFG_INPUT_$(1)_$(2))" "$$(TEST_VCPUS_$(1))" "$$(TEST_EXTRA_CFG_$(1))" "$$(call root-path,$(ROOT)/config/$$*.cfg.in)"

.PHONY: install-nr-bin-$(1)-$(2) install-nr-cfg-$(1)-$(2)
install-nr-bin-$(1)-$(2): $$(TEST_BIN_TARGET_$(1)_$(2))
	@$(INSTALL_DIR) $(DESTDIR)$(xtftestdir)/$$(TEST_NAME_$(1))
	$(INSTALL_PROGRAM) $$< $(DESTDIR)$(xtftestdir)/$$(TEST_NAME_$(1))

install-nr-cfg-$(1)-$(2): $$(filter $$(TEST_DIR_$(1))/test-$(2)-%,$$(TEST_CFG_TARGETS_$(1)))
	@$(INSTALL_DIR) $(DESTDIR)$(xtftestdir)/$$(TEST_NAME_$(1))
	$(INSTALL_DATA) $$^ $(DESTDIR)$(xtftestdir)/$$(TEST_NAME_$(1))
endef

$(foreach key,$(REGISTERED_TESTS),$(eval $(call xtf_emit_test,$(key))))