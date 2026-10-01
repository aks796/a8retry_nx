#---------------------------------------------------------------------------------
# Asphalt 8: Airborne Retry -- Nintendo Switch wrapper (32-bit / AArch32)
#
# Ships NO game code and NO game assets: the engine is put together on the
# first launch from the user's own A8R.apk (source/a8r_setup.c), and the game
# reads the user's own data folder.
#
# The build is the android32 runtime's (runtime/runtime.mk: devkitARM +
# libnx32 + mesa32 from portlibs32/); ./build.sh runs it in the toolchain
# container. Output: a8retry_nx.nsp, which the launcher NRO carries (launcher/).
#---------------------------------------------------------------------------------
TARGET               := a8retry_nx
PORT_NPDM_PROGRAM_ID := 0x0100000000001014
PORT_NPDM_ADDRSPACE  := 2
PORT_BUILD_H_USERS   := a8r_setup
include runtime/runtime.mk

# a8r_perf.c times how long the GL thread waits for a display buffer, and
# counts glthread's syncs per GL call
ifeq ($(DCR_GL_MESA),1)
LDFLAGS += -Wl,--wrap=nwindowDequeueBuffer -Wl,--wrap=_mesa_glthread_finish_before
endif

.PHONY: check
check:
	@echo "run on the host: python3 runtime/tools/gen_imports.py --check"
