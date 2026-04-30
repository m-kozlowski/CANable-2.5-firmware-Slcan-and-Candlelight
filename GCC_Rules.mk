
# CANable Makefile (from normaldotcom, modified by ElmüSoft)
# https://netcult.ch/elmue/CANable Firmware Update

#######################################
# user configuration:
#######################################

# Define the firmware version in BCD format.
# Version 0x250914 is displayed as "25.09.14" and means 14th september 2025
# The year and month are stored in the device descriptor.
# The entire version is returned by Slcan command "V" and by Candlelight command GS_ReqGetDeviceVersion
# Do not use totally meaningless version numbers like "b158aa7" in legacy firmware on Github.
FIRMWARE_VERSION = 0x260416

# TARGET_BOARD, TARGET_FIRMWARE, TARGET_FILE and TARGET_MCU must be set in the main makefile before including this file

# directory to place output files in
BUILD_DIR = Build_$(TARGET_MCU)_$(TARGET_FIRMWARE)_$(TARGET_BOARD)

# File trunk (without extension) of build files: *.bin, *.hex, *.elf
# Example: Trunk = "STM32G431_Slcan2.5_Multiboard_0x250914"
BUILD_TRUNK = $(BUILD_DIR)/$(TARGET_FILE)_$(FIRMWARE_VERSION)

# Per-chip configuration: contains only the project-specific files that
# genuinely differ from chip to chip and aren't supplied by upstream
CONFIG_DIR = STM32/$(TARGET_MCU)_Config

# location of the linker script
LD_SCRIPT = $(CONFIG_DIR)/$(TARGET_MCU).ld

# user C flags (enable warnings, enable debug info)
# Flag -O3 optimizes for higher speed, Flag -Os optimizes for smaller size
USER_CFLAGS = -Wall -g -ffunction-sections -fdata-sections -O3

# user LD flags
USER_LDFLAGS = -fno-exceptions -ffunction-sections -fdata-sections -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs

#######################################
# binaries
#######################################
CC = arm-none-eabi-gcc
AR = arm-none-eabi-ar
RANLIB = arm-none-eabi-ranlib
SIZE = arm-none-eabi-size
OBJCOPY = arm-none-eabi-objcopy

ifeq ($(OS), Windows_NT)
    # On Windows mkdir is already implemented in the console.
    # But Windows mkdir takes other parameters than Linux mkdir.
    # Simply rename the file mkdir.exe in your MingW installation folder into mmkdir.exe
    MKDIR = mmkdir -p
else
    # Linux
    MKDIR = mkdir -p
endif

#######################################
# build configuration
#######################################

# Detect the MCU family from TARGET_MCU and select toolchain options.
# Add new families here as they are ported.
#
# FAMILY_LC      lowercase family code, used to find the per-family HAL
#                driver and CMSIS-Device repos as git submodules under
#                STM32/stm32{family_lc}xx_hal_driver and
#                STM32/cmsis_device_{family_lc}.
# CPU            -mcpu= value passed to arm-none-eabi-gcc.
# SYSTEM_C       CMSIS system file (provides SystemCoreClockUpdate); read
#                directly from cmsis_device_X/Source/Templates.
# CAN_C          which CAN driver source compiles in: the FDCAN driver
#                (can.c, used by G4 and G0) or the bxCAN port (can_bxcan.c, F0).
ifneq (,$(findstring STM32G4,$(TARGET_MCU)))
    FAMILY_LC  = g4
    CPU        = cortex-m4
    SYSTEM_C   = system_stm32g4xx.c
    CAN_C      = can.c
else ifneq (,$(findstring STM32G0,$(TARGET_MCU)))
    FAMILY_LC  = g0
    CPU        = cortex-m0plus
    SYSTEM_C   = system_stm32g0xx.c
    CAN_C      = can.c
else ifneq (,$(findstring STM32F0,$(TARGET_MCU)))
    FAMILY_LC  = f0
    CPU        = cortex-m0
    SYSTEM_C   = system_stm32f0xx.c
    CAN_C      = can_bxcan.c
else
    $(error TARGET_MCU '$(TARGET_MCU)' is not recognized. Add a family branch in GCC_Rules.mk.)
endif

# where to build STM32Cube
CUBELIB_BUILD_DIR = $(BUILD_DIR)/STM32Cube

# Paths into the upstream ST submodules
# After cloning this repo, run:
#     git submodule update --init --recursive
DRIVER_PATH    = STM32/stm32$(FAMILY_LC)xx_hal_driver
CMSIS_DEV_PATH = STM32/cmsis_device_$(FAMILY_LC)

# Friendly error if the user forgot to init submodules.
ifeq ($(wildcard $(DRIVER_PATH)/Inc/.),)
    $(error $(DRIVER_PATH) is empty. Run: git submodule update --init --recursive)
endif

# includes for gcc
INCLUDES  = -ISTM32/CMSIS/Include
INCLUDES += -I$(CMSIS_DEV_PATH)/Include
INCLUDES += -I$(DRIVER_PATH)/Inc
INCLUDES += -I$(CONFIG_DIR)
INCLUDES += -ISource
INCLUDES += -ISource/USB
INCLUDES += -ISource/$(TARGET_FIRMWARE)

# compile gcc flags
CFLAGS =  $(INCLUDES)
CFLAGS += -mcpu=$(CPU) -mthumb
CFLAGS += $(USER_CFLAGS)
CFLAGS += -D$(TARGET_BOARD)
CFLAGS += -D$(TARGET_FIRMWARE)
CFLAGS += -D$(TARGET_MCU)
CFLAGS += -DTARGET_BOARD=\"$(TARGET_BOARD)\"
CFLAGS += -DTARGET_FIRMWARE=\"$(TARGET_FIRMWARE)\"
CFLAGS += -DTARGET_MCU=\"$(TARGET_MCU)\"
CFLAGS += -DHSE_VALUE=$(QUARTZ_FREQU)
CFLAGS += -DFIRMWARE_VERSION_BCD=$(FIRMWARE_VERSION)

# default action: build the user application
all: $(BUILD_TRUNK).bin $(BUILD_TRUNK).hex

flash: all
	sudo dfu-util -w -d 0483:df11 -c 1 -i 0 -a 0 -s 0x08000000:leave -D $(BUILD_TRUNK).bin


#######################################
# build the ST micro peripherial library
# (STM32 and CMSIS)
#######################################

CUBELIB = $(CUBELIB_BUILD_DIR)/libstm32cube.a

# List of stm32 driver objects
# The HAL driver comes with some template files that are not meant to be compiled, like stm32g4xx_hal_timebase_tim_template.c
# STM did not put these templates into a separate subdirectory. If we filter them out here, this allows
# building against an external driver directory without further modification.
CUBELIB_DRIVER_OBJS = $(addprefix $(CUBELIB_BUILD_DIR)/, $(patsubst %.c, %.o, $(filter-out %_template.c, $(notdir $(wildcard $(DRIVER_PATH)/Src/*.c)))))

# shortcut for building core library (make cubelib)
cubelib: $(CUBELIB)

$(CUBELIB): $(CUBELIB_DRIVER_OBJS)
	$(AR) rc $@ $(CUBELIB_DRIVER_OBJS)
	$(RANLIB) $@

$(CUBELIB_BUILD_DIR)/%.o: $(DRIVER_PATH)/Src/%.c | $(CUBELIB_BUILD_DIR)
	$(CC) -c $(CFLAGS) -o $@ $^

$(CUBELIB_BUILD_DIR):
	$(MKDIR) $@

#######################################
# build the firmware specific files
#######################################

FIRM_BUILD_DIR = $(BUILD_DIR)/$(TARGET_FIRMWARE)
FIRM_SOURCES += control.c buffer.c usb_class.c usb_interface.c
# list of firmware specific library objects
FIRM_OBJECTS += $(addprefix $(FIRM_BUILD_DIR)/,$(notdir $(FIRM_SOURCES:.c=.o)))

firm: $(FIRM_OBJECTS)

$(FIRM_BUILD_DIR)/%.o: Source/$(TARGET_FIRMWARE)/%.c | $(FIRM_BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $^

$(FIRM_BUILD_DIR):
	@echo $(FIRM_BUILD_DIR)
	$(MKDIR) $@

#######################################
# build the user application
#######################################

# list of common source files (CMSIS system file + CAN driver vary by family)
SOURCES = main.c $(SYSTEM_C) system.c interrupts.c $(CAN_C) error.c led.c dfu.c utils.c usb_ctrlreq.c usb_ioreq.c usb_core.c usb_lowlevel.c

# list of user program objects
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(SOURCES:.c=.o)))
# add an object for the startup code
OBJECTS += $(BUILD_DIR)/startup_$(TARGET_MCU).o

# use the periphlib core library, plus generic ones (libc, libm, libnosys)
LIBS = -lstm32cube -lc -lm -lnosys
LDFLAGS = -T $(LD_SCRIPT) -L $(CUBELIB_BUILD_DIR) -static $(LIBS) $(USER_LDFLAGS)

$(BUILD_TRUNK).hex: $(BUILD_TRUNK).elf
	$(OBJCOPY) -O ihex $(BUILD_TRUNK).elf $@

$(BUILD_TRUNK).bin: $(BUILD_TRUNK).elf
	$(OBJCOPY) -O binary $(BUILD_TRUNK).elf $@

$(BUILD_TRUNK).elf: $(OBJECTS) $(FIRM_OBJECTS) $(CUBELIB)
	$(CC) -o $@ $(CFLAGS) $(OBJECTS) $(FIRM_OBJECTS) \
		$(LDFLAGS) -Xlinker \
		-Map=$(BUILD_TRUNK).map
	$(SIZE) $@

$(BUILD_DIR)/%.o: Source/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $^

# CMSIS template files come from the cmsis_device_X submodule
# Filenames inside the submodule are lowercase while TARGET_MCU is mixed case
TARGET_MCU_LC := $(shell echo $(TARGET_MCU) | tr A-Z a-z)
STARTUP_S      = $(CMSIS_DEV_PATH)/Source/Templates/gcc/startup_$(TARGET_MCU_LC).s
SYSTEM_C_PATH  = $(CMSIS_DEV_PATH)/Source/Templates/$(SYSTEM_C)

$(BUILD_DIR)/startup_$(TARGET_MCU).o: $(STARTUP_S) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/$(SYSTEM_C:.c=.o): $(SYSTEM_C_PATH) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	$(MKDIR) $@

# delete all user application files, keep the libraries
clean:
		-rm $(BUILD_DIR)/*.o
		-rm $(BUILD_DIR)/*.elf
		-rm $(BUILD_DIR)/*.hex
		-rm $(BUILD_DIR)/*.map
		-rm $(BUILD_DIR)/*.bin

.PHONY: clean all cubelib
