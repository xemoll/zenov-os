# GNU make loads this before Makefile. Preserve the upstream 0.1.1 build graph,
# then layer ZenPkg, the signed offline ZenRepo trust chain and ZenUniverse.
include Makefile
include zenpkg.mk
include universe.mk

# zenpkg-check validates the generated manifest in its recipe. Extending the
# target here makes that dependency explicit without duplicating zenpkg.mk.
$(ZENPKG_CHECK_STAMP): $(BUILD)/zenpkg-manifest.json
