#---------------------------------------------------------------------------------
# a8retry_nx.nro: the launcher (64-bit) that installs the 32-bit game program
# for a sphaira forwarder's icon, after unpacking the A8R mod's zips. The
# shared launcher (runtime/launcher/); this file names it, and source/ holds
# the mod's hooks (zips).
#---------------------------------------------------------------------------------
APP_TITLE        := Asphalt 8: Airborne Retry
APP_AUTHOR       := aks796, Techboy1997 / Gameloft
APP_VERSION      := 1.0.0
TARGET           := a8retry_nx
PORT_PAYLOAD     := a8retry_nx
LAUNCHER_SOURCES := source
LAUNCHER_LIBS    := -lminizip -lz
TOPDIR ?= $(CURDIR)
A32    ?= $(TOPDIR)/../runtime
include $(A32)/launcher/launcher.mk
