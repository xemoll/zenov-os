# ZenUniverse universal catalog compiler and runtime-provider resolver.
ZENUNIVERSE_TOOL := $(BUILD)/zenuniverse
ZENUNIVERSE_CATALOG := $(BUILD)/zenuniverse.zuc
ZENUNIVERSE_SOURCES := $(sort $(wildcard packages/universe/*.zsource))
ZENUNIVERSE_HEADERS := tools/zenuniverse/common.hpp tools/zenuniverse/descriptor.hpp tools/zenuniverse/host_profile.hpp tools/zenuniverse/resolver.hpp
ZENUNIVERSE_CHECK_STAMP := $(BUILD)/zenuniverse-check.stamp
ZENUNIVERSE_RUNTIME_CHECK_STAMP := $(BUILD)/zenuniverse-runtime-check.stamp

$(ZENUNIVERSE_TOOL): tools/zenuniverse/main.cpp $(ZENUNIVERSE_HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Werror -Wpedantic $< -o $@

$(ZENUNIVERSE_CATALOG): $(ZENUNIVERSE_TOOL) $(ZENUNIVERSE_SOURCES)
	$(ZENUNIVERSE_TOOL) validate $(ZENUNIVERSE_SOURCES)
	$(ZENUNIVERSE_TOOL) compile --input packages/universe --output $@

$(ZENUNIVERSE_CHECK_STAMP): $(ZENUNIVERSE_CATALOG) tests/zenuniverse_test.sh
	BUILD=$(BUILD)/zenuniverse-test CXX=$(CXX) bash tests/zenuniverse_test.sh
	@touch $@

$(ZENUNIVERSE_RUNTIME_CHECK_STAMP): $(ZENUNIVERSE_CATALOG) tests/zenuniverse_runtime_provider_test.sh
	BUILD=$(BUILD)/zenuniverse-runtime-test CXX=$(CXX) bash tests/zenuniverse_runtime_provider_test.sh
	@touch $@

.PHONY: universe-check
universe-check: $(ZENUNIVERSE_CHECK_STAMP) $(ZENUNIVERSE_RUNTIME_CHECK_STAMP)
