# top level project rules for the msm7627_surf project
#
LOCAL_DIR := $(GET_LOCAL_DIR)

TARGET := msm7627a

MODULES += app/aboot

EMMC_BOOT := 1

#DEFINES += WITH_DEBUG_DCC=1
#DEFINES += WITH_DEBUG_UART=1
#DEFINES += WITH_DEBUG_FBCON=1
ENABLE_THUMB := false

ifeq ($(EMMC_BOOT),1)
DEFINES += _EMMC_BOOT=1
endif
