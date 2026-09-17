.SILENT:
.PHONY: $(MAKECMDGOALS) _
$(MAKECMDGOALS): _

_:
	@$(MAKE) --no-print-directory -C $(PWD)/build $(MAKECMDGOALS)

define TEST_RECIPE
$(notdir $1): $1
	@$$<

endef
$(foreach f, $(wildcard $(PWD)/build/test/*.test),$(eval $(call TEST_RECIPE,$f)))
