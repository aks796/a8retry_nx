#---------------------------------------------------------------------------------
# Asphalt 8: Airborne Retry -- Nintendo Switch wrapper (32-bit / AArch32)
#
# Ships NO game code and NO game assets: the engine is put together on the
# first launch from the user's own A8R.apk (source/a8r_setup.c), and the game
# reads the user's own data folder.
#
# Builds with devkitARM + libnx32 (the vita2hos AArch32 port of libnx). Run it
# inside the toolchain container via ./build.sh, which is what this file assumes.
#
# Output: a8retry_nx.nsp -- an ExeFS NSP (main + main.npdm, 32-bit), launched via
# Atmosphere's hbl override with override_any_app_address_space=32_bit, or in
# an emulator directly. A 32-bit program cannot be an NRO: hbloader is 64-bit.
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO is not set. Build with ./build.sh (toolchain container).")
endif
include $(DEVKITPRO)/devkitARM/base_rules

TARGET   := a8retry_nx
BUILD    := build
SOURCES  := source
NX       := $(DEVKITPRO)/libnx32

# softfp is not a preference, it is the ABI: armeabi-v7a passes float/double in
# core registers, and so do libnx32 and newlib (built soft-float). Building the
# host softfp makes every shim, callback and engine entry point agree without
# per-function pcs("aapcs") annotations, while still emitting VFP/NEON code.
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mfloat-abi=softfp \
        -mfpu=neon-fp-armv8 -mtp=soft -fPIE -ftls-model=local-exec

# Renderer: 1 = mesa/nouveau from portlibs32/ (mesa32's prefix/, built by its
# ./build.sh, or its release tarball), 0 = the null renderer. Default: mesa when
# its libraries are present.
PORTLIBS := $(CURDIR)/portlibs32
ifeq ($(origin DCR_GL_MESA),undefined)
DCR_GL_MESA := $(if $(wildcard $(PORTLIBS)/lib/libEGL.a),1,0)
endif

CFLAGS := -g -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
          -ffunction-sections -fdata-sections $(ARCH) -D__SWITCH__ \
          -I$(NX)/include -I$(SOURCES) -I$(BUILD) -I$(PORTLIBS)/include \
          -DDCR_GL_MESA=$(DCR_GL_MESA) \
          -Werror=implicit-function-declaration -Werror=implicit-int \
          -Werror=int-conversion -Werror=incompatible-pointer-types \
          -Werror=return-type
ASFLAGS := -g $(ARCH)

# dcr32.specs/.ld: -z notext + a page-0 relocator. See source/crt0_reloc.c.
LDFLAGS := -specs=$(CURDIR)/dcr32.specs -T $(CURDIR)/dcr32.ld $(ARCH) -g \
           -Wl,-Map,$(BUILD)/$(TARGET).map
# Mesa format checks, instrumented (gl_mesa.c: a format outside Mesa's table
# is logged with its caller)
ifeq ($(DCR_GL_MESA),1)
LDFLAGS += -Wl,--wrap=_mesa_is_format_srgb -Wl,--wrap=nouveau_bo_new -Wl,--wrap=nwindowDequeueBuffer \
           -Wl,--wrap=_mesa_glthread_finish_before
endif
# libnx's own svcSetThreadCoreMask calls through dcr_sched.c's (the image's
# libnx32 passed the mask's high word as garbage; fixed in the fork, c6c53d20)
LDFLAGS += -Wl,--wrap=svcSetThreadCoreMask
ifeq ($(DCR_GL_MESA),1)
GL_LIBS := -L$(PORTLIBS)/lib -lEGL -lGLESv2 -lglapi -ldrm_nouveau -lstdc++
endif
LIBS    := $(GL_LIBS) -L$(NX)/lib -lminiz -lnx -lm

CFILES := $(notdir $(wildcard $(SOURCES)/*.c))
SFILES := $(notdir $(wildcard $(SOURCES)/*.S))
OFILES := $(addprefix $(BUILD)/,$(CFILES:.c=.o) $(SFILES:.S=.o))
DEPS   := $(OFILES:.o=.d)

.PHONY: all clean
all: $(TARGET).nsp $(TARGET).build

$(BUILD):
	@mkdir -p $@

# The relocator runs before BSS/TLS exist and before its own relocations are
# applied: no builtins may turn its loops into memset/memcpy calls.
# CRT0_EXTRA=-DDCR_TEST_ALIAS_RELOC (delete build/crt0_reloc.o first) makes a
# test build that takes the hardware alias path under an emulator too.
$(BUILD)/crt0_reloc.o: $(SOURCES)/crt0_reloc.c | $(BUILD)
	@echo $(notdir $<)
	@$(CC) -MMD -MP $(CFLAGS) -fno-builtin -fno-tree-loop-distribute-patterns $(CRT0_EXTRA) -c $< -o $@

# Switching renderers changes the compile flags: rebuild everything.
RENDERER_STAMP := $(BUILD)/.renderer-$(DCR_GL_MESA)
$(RENDERER_STAMP): | $(BUILD)
	@rm -f $(BUILD)/.renderer-*
	@touch $@

$(BUILD)/%.o: $(SOURCES)/%.c $(RENDERER_STAMP) | $(BUILD)
	@echo $(notdir $<)
	@$(CC) -MMD -MP $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SOURCES)/%.S $(RENDERER_STAMP) | $(BUILD)
	@echo $(notdir $<)
	@$(CC) -MMD -MP $(ASFLAGS) -c $< -o $@

$(TARGET).elf: $(OFILES)
	@echo linking $@
	@$(CC) $(LDFLAGS) $(OFILES) $(LIBS) -o $@

$(BUILD)/$(TARGET).nso: $(TARGET).elf
	@elf2nso $< $@ >/dev/null

$(BUILD)/$(TARGET).npdm: $(TARGET).json | $(BUILD)
	@npdmtool $< $@ 2>&1 | grep -v "Failed to get" || true

# The build number (UTC minutes): compiled in (a8r_setup.c) and written next to
# the NSP as $(TARGET).build. The launcher NRO carries both; the wrapper updates
# its ExeFS override from an NRO only when the NRO's number is higher.
BUILD_NUMBER := $(shell date -u +%Y%m%d%H%M)
.PHONY: FORCE
$(BUILD)/dcr_build.h: FORCE | $(BUILD)
	@echo '#define DCR_BUILD $(BUILD_NUMBER)ULL' > $@.tmp
	@cmp -s $@.tmp $@ && rm -f $@.tmp || mv $@.tmp $@
$(BUILD)/a8r_setup.o $(BUILD)/dcr_config.o: $(BUILD)/dcr_build.h


$(TARGET).build: $(TARGET).nsp
	@sed -n 's/.*DCR_BUILD \([0-9]*\)ULL.*/\1/p' $(BUILD)/dcr_build.h > $@
	@echo build number `cat $@`

$(TARGET).nsp: $(BUILD)/$(TARGET).nso $(BUILD)/$(TARGET).npdm
	@rm -rf $(BUILD)/exefs && mkdir -p $(BUILD)/exefs
	@cp $(BUILD)/$(TARGET).nso $(BUILD)/exefs/main
	@cp $(BUILD)/$(TARGET).npdm $(BUILD)/exefs/main.npdm
	@build_pfs0 $(BUILD)/exefs $@ >/dev/null
	@echo built ... $@

clean:
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).nsp $(TARGET).build

-include $(DEPS)
