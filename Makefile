.SILENT:
.PHONY: $(MAKECMDGOALS) _
$(MAKECMDGOALS): _

_:
	@$(MAKE) --no-print-directory -C $(PWD)/build $(MAKECMDGOALS)

