# Fixed build-tool command interface: host gcc/g++ bootstrap ICK; every test
# and target runtime below is compiled by the materialized ICK frontend.
REPOSITORY_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
BUILD_ROOT ?= $(REPOSITORY_ROOT)/build/native-polar
REFERENCE ?= $(REPOSITORY_ROOT)/gcc
SOURCE := $(BUILD_ROOT)/source
OBJECTS := $(BUILD_ROOT)/objects
STAGE := $(BUILD_ROOT)/stage
ICK ?= $(STAGE)/bin/gcc
NATIVE_CC1 := $(STAGE)/libexec/gcc/x86_64-pc-linux-gnu/17.0.0/cc1
BUILD_JOBS ?= 2
TEST_OUTPUT ?= $(BUILD_ROOT)/tests
FIXTURES := $(REPOSITORY_ROOT)/ick/source/gcc/testsuite/gcc.dg
BIGMUL := $(REPOSITORY_ROOT)/benchmarks/bigmul
RUNTIME_TESTS := ick-complex-polar-layout ick-complex-round ick-complex-polar-float128-run ick-complex-runtime-byte-load ick-assignment-arrow ick-multiplication-glyph
EXTRA_FLAGS_ick-complex-runtime-byte-load := -fno-strict-aliasing
OPTIMIZATIONS := O0 O2 O3
RUN_TARGETS := $(foreach optimization,$(OPTIMIZATIONS),$(addsuffix -$(optimization),$(addprefix run-,$(RUNTIME_TESTS))))
BYTE_FOLD_TARGETS := $(addprefix run-object-byte-fold-,$(OPTIMIZATIONS))

.PHONY: build check check-bigmul $(RUN_TARGETS) $(BYTE_FOLD_TARGETS)
build: $(BUILD_ROOT)/.built

$(SOURCE)/.materialized: $(REPOSITORY_ROOT)/ick/OVERLAY.sha256 $(REPOSITORY_ROOT)/ick/PRUNE $(REPOSITORY_ROOT)/ick/SOURCE.lock
	mkdir -p "$(BUILD_ROOT)"
	sh "$(REPOSITORY_ROOT)/ick/materialize.sh" "$(REFERENCE)" "$(SOURCE)"
	touch "$@"

$(OBJECTS)/Makefile: $(SOURCE)/.materialized
	mkdir -p "$(OBJECTS)"
	cd "$(OBJECTS)" && "$(SOURCE)/configure" --prefix="$(STAGE)" --enable-languages=c --disable-bootstrap --disable-multilib --disable-nls --disable-shared --disable-libsanitizer --disable-libquadmath --disable-libgomp --without-isl --without-zstd

$(BUILD_ROOT)/.built: $(OBJECTS)/Makefile
	$(MAKE) -C "$(OBJECTS)" -j$(BUILD_JOBS) all-gcc all-target-libgcc all-target-libatomic
	$(MAKE) -C "$(OBJECTS)" install-gcc install-target-libgcc install-target-libatomic
	cmp "$(OBJECTS)/x86_64-pc-linux-gnu/libgcc/libgcc.a" "$(STAGE)/lib/gcc/x86_64-pc-linux-gnu/17.0.0/libgcc.a"
	cmp "$(OBJECTS)/x86_64-pc-linux-gnu/libatomic/.libs/libatomic.a" "$(STAGE)/lib64/libatomic.a"
	$(ICK) --version
	dpkg-query -W libc6 libc6-dev > "$(BUILD_ROOT)/scalar-runtime-packages.tsv"
	sha256sum "$(STAGE)/lib/gcc/x86_64-pc-linux-gnu/17.0.0/libgcc.a" "$(STAGE)/lib64/libatomic.a" > "$(BUILD_ROOT)/ick-runtime.sha256"
	touch "$@"

# Run build before check.  check also accepts an already-built ICK path for
# local verification without replacing the source-build receipt.
check: $(RUN_TARGETS) $(BYTE_FOLD_TARGETS) $(TEST_OUTPUT)/ick-complex-polar-float16.o $(TEST_OUTPUT)/ick-complex-polar-float128.o

# Use the already built native stage for the portable benchmark controls.
check-bigmul:
	test "$(ICK)" = "$(STAGE)/bin/gcc"
	test "$$(realpath "$$("$(ICK)" -print-prog-name=cc1)")" = "$(NATIVE_CC1)"
	git -C "$(REPOSITORY_ROOT)" rev-parse HEAD
	sha256sum "$(ICK)" "$(NATIVE_CC1)"
	CC="$(ICK)" python3 "$(BIGMUL)/test_mulbench.py"
	CC="$(ICK)" python3 "$(BIGMUL)/acceptance/check_mb01.py"
	CC="$(ICK)" python3 "$(BIGMUL)/abi/check_mb02.py"

define runtime_test
$(TEST_OUTPUT)/$(1)-$(2): $(FIXTURES)/$(1).c
	mkdir -p "$$(@D)"
	$(ICK) -std=gnu17 -$(2) $(EXTRA_FLAGS_$(1)) "$$<" -lm -o "$$@"
run-$(1)-$(2): $(TEST_OUTPUT)/$(1)-$(2)
	"$$<"
endef
$(foreach optimization,$(OPTIMIZATIONS),$(foreach fixture,$(RUNTIME_TESTS),$(eval $(call runtime_test,$(fixture),$(optimization)))))

define byte_fold_test
$(TEST_OUTPUT)/object-byte-fold-$(1): $(FIXTURES)/ick-complex-object-byte-fold.c $(REPOSITORY_ROOT)/qualification/native-polar/object-byte-fold-main.c
	mkdir -p "$$(@D)"
	$(ICK) -std=gnu17 -$(1) $(FIXTURES)/ick-complex-object-byte-fold.c $(REPOSITORY_ROOT)/qualification/native-polar/object-byte-fold-main.c -lm -o "$$@"
run-object-byte-fold-$(1): $(TEST_OUTPUT)/object-byte-fold-$(1)
	"$$<"
endef
$(foreach optimization,$(OPTIMIZATIONS),$(eval $(call byte_fold_test,$(optimization))))

$(TEST_OUTPUT)/%.o: $(FIXTURES)/%.c
	mkdir -p "$(@D)"
	$(ICK) -std=gnu17 -O2 -c "$<" -o "$@"
