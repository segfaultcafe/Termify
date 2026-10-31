.PHONY: all build run test clean package install

BUILD_DIR := ./bin
BINARY_NAME := termify
DEBUG_DIR := Debug

all: build

build:
	./scripts/build.sh

run:
	$(BUILD_DIR)/$(DEBUG_DIR)/$(BINARY_NAME)

test: build
	ctest --test-dir $(BUILD_DIR)/$(DEBUG_DIR) --output-on-failure

clean:
	cmake -E remove_directory $(BUILD_DIR)/$(DEBUG_DIR)

package:
	./scripts/package-release.sh

install:
	./scripts/release.sh
