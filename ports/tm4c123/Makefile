########################################################
######################################################## 
# Makefile for Micropython Tiva Lauchpad (TM4C123GH6PM) Port
# by Raphael Kriegl
########################################################

########################################################
# User Variables
########################################################

# Enables Debugging friendly settings, change requires full rebuild
DEBUG ?= 1
# Enables Link Time Optimization, only when not compiling for DEBUG, change requires full rebuild
LINK_TIME_OPT = 1

# Location of the TivaWare library
# Can be pointed to a newer version if desired
TIVAWARE_LIB = /mnt/c/ti/TivaWare
#TIVAWARE_LIB = /home/raphael/ti/tivaware

# Name of the Project (and the resulting binary file)
PROJECT_NAME = uPY_TM4C123

# OpenOCD paths
#OPENOCD_PATH = /usr/local/share/openocd
OPENOCD_PATH = openocd

#OPENOCD_BOARD_CFG = $(OPENOCD_PATH)/scripts/board/ek-tm4c123gxl.cfg
OPENOCD_BOARD_CFG = openocd/ek-tm4c123gxl.cfg
OPENOCD_PROG_CFG = openocd/tiva_flash.cfg

########################################################
# Internal Variables
########################################################

# Include variabes for Python and Compiler
include ../../py/mkenv.mk

# Defines the part type that this project uses.
PART=TM4C123GH6PM

# qstr definitions (must come before including py.mk)
QSTR_DEFS = qstrdefsport.h $(BUILD)/pins_qstr.h
QSTR_GLOBAL_DEPENDENCIES = board/mpconfigboard.h

# Implementation of floating point lib, single for TivaBoard
MICROPY_FLOAT_IMPL=single

# Implementation of arbitrary long integers
LONGINT_IMPL=mpz

# include py core make definitions
include $(TOP)/py/py.mk

# Cross compilation prefix
CROSS_COMPILE = arm-none-eabi-
# Cross compilation flag
CROSS = 1

USBDEV_DIR=usbdev

# Included directories
INC += -I.
INC += -I./mods
INC += -I./misc
INC += -I./cmsis
INC += -I$(TOP)
INC += -I$(BUILD)
INC += -I$(BUILD)/genhdr
INC += -I$(TIVAWARE_LIB)
INC += -I$(TIVAWARE_LIB)/driverlib
INC += -I$(PY_SRC)
INC += -I./board
INC += -I../../
INC += -I$(USBDEV_DIR)/core/inc -I$(USBDEV_DIR)/class/inc

DFU = $(TOP)/tools/dfu.py
PYDFU = $(TOP)/tools/pydfu.py
FATFS_DIR=lib/oofatfs

########################################################
# Compiler
########################################################

# Architecture
CFLAGS_CORTEX_M4 = -mthumb -mtune=cortex-m4 -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mabi=aapcs-linux

# Warnings
CFLAGS = -Wall -Werror -Wpointer-arith

# Misc flags
CFLAGS += $(INC) -std=gnu99 -nostdlib $(CFLAGS_CORTEX_M4) $(COPT) -fshort-enums -fdata-sections -ffunction-sections 

# Preprocessor Flags
CFLAGS += $(addprefix -D, \
		PART_$(PART) \
		$(PART) \
		ARMCM4_FP \
		CMSIS_HEADER=\"ARMCM4_FP.h\" \
		TARGET_IS_TM4C123_RB1 \
		MICROPY_PIN_DEFS_PORT_H=\"mods/pin_defs_tm4c123.h\" \
		FFCONF_H=\"lib/oofatfs/ffconf.h\" \
		)

# CFLAGS += -DMICROPY_MPHALPORT_H=\"mphalport.h\"

# Arbitrary Interger Implementation
ifeq ($(LONGINT_IMPL), mpz)
CFLAGS += -DMICROPY_LONGINT_IMPL=MICROPY_LONGINT_IMPL_MPZ
else
CFLAGS += -DMICROPY_LONGINT_IMPL=MICROPY_LONGINT_IMPL_NONE
endif

# Floating point implementation
ifeq ($(MICROPY_FLOAT_IMPL), single)
CFLAGS += -DMICROPY_FLOAT_IMPL=MICROPY_FLOAT_IMPL_FLOAT
CFLAGS += -fsingle-precision-constant -Wdouble-promotion
else 
CFLAGE += -DMICROPY_FLOAT_IMPL=MICROPY_FLOAT_IMPL_NONE
endif

# Tune for Debugging or Optimization
ifeq ($(DEBUG), 1)
CFLAGS += -ggdb -DDEBUG -DPENDSV_DEBUG
COPT += -Og
else
COPT += -Os -DNDEBUG
# Flags for Link Time Optimization
ifeq ($(LINK_TIME_OPT), 1)
CFLAGS += -flto -fuse-linker-plugin
endif
endif

########################################################
# Linker                                               #
########################################################

LINKER_FILE = tm4c123gh6pm.ld

# use gcc as linker frontend for additional funtionality
LD = $(CC)
LDFLAGS = -Wl,-nostdlib,-T,$(LINKER_FILE),-Map=$(@:.axf=.map),--cref,--gc-sections $(CFLAGS)

# Libraries
LIBS = $(TIVAWARE_LIB)/driverlib/gcc/libdriver.a
LIBGCC := ${shell $(CC) $(CFLAGS) --print-libgcc-file-name}
LIBC := ${shell ${CC} ${CFLAGS} --print-file-name=libc.a}
LIBM := ${shell ${CC} ${CFLAGS} --print-file-name=libm.a}

########################################################
# Binutils
########################################################

OD = $(CROSS_COMPILE)objdump

ODFLAGS = -S
CPFLAGS = -O binary

########################################################
# Sources	
########################################################

STARTUP_FILE = startup_TM4C123.c
SYSTEM_FILE = misc/system_TM4C123.c

# lib
SRC_LIB = $(addprefix lib/,\
	libc/string0.c \
	mp-readline/readline.c \
	netutils/netutils.c \
	timeutils/timeutils.c \
	utils/interrupt_char.c \
	utils/pyexec.c \
	utils/sys_stdio_mphal.c \
	oofatfs/ff.c \
	oofatfs/option/unicode.c \
	)

# Module sources
MODS_SRC_C = $(addprefix mods/,\
	mphalport.c \
	pin.c \
	pin_named_pins.c \
	modmachine.c \
	modpyb.c \
	uart.c \
	rtc.c \
	systick.c \
	handlers.c \
	spi.c \
	sdcard.c \
	moduos.c \
	pendsv.c \
	pybthread.c \
	modutime.c \
	irq.c \
	mpthreadport.c \
)
	# fatfs_port.c \
	# irq.c \
	# modutime.c \	
	# moduos.c \
	# pybthread.c \
	# pendsv.c \
	# flashbdev.c \
	# storage.c \
	# flash.c \
	# )
	#	extint.c see forum

# QSTR sources
SRC_QSTR += $(MODS_SRC_C)

# Core sources
CORE_SRC_C = \
	$(STARTUP_FILE) \
	main.c \
	uart_core.c \
	gccollect.c \
	$(SYSTEM_FILE) \
	
ASM_SRC_S = \
	gchelper.s 

# LIBM
ifeq ($(MICROPY_FLOAT_IMPL),single)
SRC_LIBF = $(addprefix lib/libm/, math.c thumb_vfp_sqrtf.c acoshf.c asinfacosf.c asinhf.c \
			atan2f.c atanf.c atanhf.c ef_rem_pio2.c erf_lgamma.c fmodf.c kf_cos.c kf_rem_pio2.c \
			kf_sin.c kf_tan.c log1pf.c nearbyintf.c sf_cos.c sf_erf.c sf_frexp.c sf_ldexp.c \
			sf_modf.c sf_sin.c sf_tan.c wf_lgamma.c wf_tgamma.c )
endif

# Additional sources
# ADD_SRC_C = 
# 	drivers/bus/softspi.c 
		
SRC_C = $(CORE_SRC_C) $(MODS_SRC_C) $(SRC_LIB) $(SRC_LIBF) $(ADD_SRC_C) $(GEN_PINS_SRC)

ASM_OBJ = $(addprefix $(BUILD)/, $(ASM_SRC_S:.s=.o))

C_OBJ = $(addprefix $(BUILD)/, $(SRC_C:.c=.o))# $(BUILD)/board/pins.o

OBJ = $(PY_O) $(ASM_OBJ) $(C_OBJ) 

########################################################
# Optimization
########################################################

# Always compile floating point lib with Optimizations to save space
$(addprefix $(BUILD)/, $(SRC_LIBF:.c=.o)): COPT += -Os

# This file contains performance critical functions so turn up the optimisation
# level.  It doesn't add much to the code size and improves performance a bit.
# Don't use -O3 with this file because gcc tries to optimise memset in terms of itself.
$(BUILD)/lib/libc/string0.o: COPT += -O2

# We put several files into the first 16K section with the ISRs.
# If we compile these using -O0 then it won't fit. So if you really want these
# to be compiled with -O0, then edit boards/common.ld (in the .isr_vector section)
# and comment out the following lines.
$(BUILD)/$(FATFS_DIR)/ff.o: COPT += -Os
$(filter $(PY_BUILD)/../extmod/vfs_fat_%.o, $(PY_O)): COPT += -Os
$(PY_BUILD)/formatfloat.o: COPT += -Os
$(PY_BUILD)/parsenum.o: COPT += -Os
$(PY_BUILD)/mpprint.o: COPT += -Os

########################################################
# Preprocessing
########################################################

# Build frozencode
$(BUILD)/_frozen_mpy.c: frozentest.mpy $(BUILD)/genhdr/qstrdefs.generated.h
	$(ECHO) "MISC freezing bytecode"
	$(Q)$(TOP)/tools/mpy-tool.py -f -q $(BUILD)/genhdr/qstrdefs.preprocessed.h -mlongint-impl=none $< > $@
	
MAKE_PINS = board/make-pins.py
BOARD_PINS = board/pins.csv
AF_FILE = board/tm4c123_af.csv
PREFIX_FILE = board/tm4c123_prefix.c
GEN_PINS_SRC = $(BUILD)/pins.c
GEN_PINS_HDR = $(HEADER_BUILD)/pins.h
GEN_PINS_QSTR = $(BUILD)/pins_qstr.h

INSERT_USB_IDS = $(TOP)/tools/insert-usb-ids.py
FILE2H = $(TOP)/tools/file2h.py

USB_IDS_FILE = mods/usb.h
CDCINF_TEMPLATE = pybcdc.inf_template
GEN_CDCINF_FILE = $(HEADER_BUILD)/pybcdc.inf
GEN_CDCINF_HEADER = $(HEADER_BUILD)/pybcdc_inf.h

# List of sources for qstr extraction
SRC_QSTR += $(SRC_C) $(SRC_MOD) $(SRC_LIB) $(EXTMOD_SRC_C) $(GEN_PINS_SRC)
# Append any auto-generated sources that are needed by sources listed in
# SRC_QSTR
SRC_QSTR_AUTO_DEPS += $(GEN_CDCINF_HEADER)

# Making OBJ use an order-only depenedency on the generated pins.h file
# has the side effect of making the pins.h file before we actually compile
# any of the objects. The normal dependency generation will deal with the
# case when pins.h is modified. But when it doesn't exist, we don't know
# which source files might need it.
$(OBJ): | $(GEN_PINS_HDR)

# With conditional pins, we may need to regenerate qstrdefs.h when config
# options change.
$(HEADER_BUILD)/qstrdefs.generated.h: board/mpconfigboard.h

# main.c can't be even preprocessed without $(GEN_CDCINF_HEADER)
main.c: $(GEN_CDCINF_HEADER)

# Call make-pins.py to generate both pins_gen.c and pins.h
$(GEN_PINS_SRC) $(GEN_PINS_HDR) $(GEN_PINS_QSTR): $(BOARD_PINS) $(MAKE_PINS) $(AF_FILE) $(PREFIX_FILE) | $(HEADER_BUILD)
	$(ECHO) "Create $@"
	$(Q)$(PYTHON) $(MAKE_PINS) --board $(BOARD_PINS) --af $(AF_FILE) --prefix $(PREFIX_FILE) --hdr $(GEN_PINS_HDR) --qstr $(GEN_PINS_QSTR) > $(GEN_PINS_SRC)

CMSIS_DIR = ./cmsis

GEN_STMCONST_HDR = $(HEADER_BUILD)/modstm_const.h
GEN_STMCONST_QSTR = $(BUILD)/modstm_qstr.h
GEN_STMCONST_MPZ = $(HEADER_BUILD)/modstm_mpz.h
CMSIS_MCU_HDR = $(CMSIS_DIR)/core_cm4.h
# Use a pattern rule here so that make will only call make-stmconst.py once to
# make both modstm_const.h and modstm_qstr.h
$(HEADER_BUILD)/%_const.h $(BUILD)/%_qstr.h: $(CMSIS_MCU_HDR) make-stmconst.py | $(HEADER_BUILD)
	$(ECHO) "GEN stmconst $@"
	$(Q)$(PYTHON) make-stmconst.py --qstr $(GEN_STMCONST_QSTR) --mpz $(GEN_STMCONST_MPZ) $(CMSIS_MCU_HDR) > $(GEN_STMCONST_HDR)

$(GEN_CDCINF_HEADER): $(GEN_CDCINF_FILE) $(FILE2H) | $(HEADER_BUILD)
	$(ECHO) "GEN $@"
	$(Q)$(PYTHON) $(FILE2H) $< > $@

$(GEN_CDCINF_FILE): $(CDCINF_TEMPLATE) $(INSERT_USB_IDS) $(USB_IDS_FILE) | $(HEADER_BUILD)
	$(ECHO) "GEN $@"
	$(Q)$(PYTHON) $(INSERT_USB_IDS) $(USB_IDS_FILE) $< > $@

########################################################
# Build Targets
########################################################

# Build axf and elf
${PROJECT_NAME}.axf: $(OBJ)
	$(ECHO)
	$(ECHO) Used these C-Flags: $(CFLAGS) 
	$(ECHO)
	$(ECHO) Making driverlib
	$(Q)$(MAKE) -C ${TIVAWARE_LIB}/driverlib/
	$(ECHO)
	$(ECHO) Linking...
	$(ECHO) " >" ${PROJECT_NAME}.axf
	$(Q)$(LD) $(LDFLAGS) -o ${PROJECT_NAME}.axf $(OBJ) $(LIBS) $(LIBM) $(LIBC) $(LIBGCC)
	$(ECHO) " >" ${PROJECT_NAME}.elf
	$(Q)$(LD) $(LDFLAGS) -o ${PROJECT_NAME}.elf $(OBJ) $(LIBS) $(LIBM) $(LIBC) $(LIBGCC)

# Build binary
${PROJECT_NAME}: ${PROJECT_NAME}.axf
	$(ECHO)
	$(ECHO) -n Copying...
	$(Q)$(OBJCOPY) $(CPFLAGS) ${PROJECT_NAME}.axf ${PROJECT_NAME}.bin
	$(ECHO) " >" ${PROJECT_NAME}.bin
	$(ECHO) -n Creating list file...
	$(Q)$(OD) $(ODFLAGS) ${PROJECT_NAME}.axf > ${PROJECT_NAME}.lst
	$(ECHO) " >" ${PROJECT_NAME}.lst
	$(ECHO)

.PHONY: info
info:
	$(ECHO) +---------------------------------------------------
	$(ECHO) + Info
	$(ECHO) +---------------------------------------------------
	$(ECHO) + DEBUG = $(DEBUG)
	$(ECHO) +---------------------------------------------------
	$(ECHO) + Size = $$(( $(word 7, $(shell $(CROSS_COMPILE)size -Bd $(PROJECT_NAME).axf)) + $(word 8, $(shell $(CROSS_COMPILE)size -Bd $(PROJECT_NAME).axf)) )) kiB
	$(ECHO) +---------------------------------------------------
	$(ECHO) + OPT-Flags: $(COPT)
	$(ECHO) +---------------------------------------------------

.PHONY: new
new: clean all

.PHONY: all
all: $(OBJ) ${PROJECT_NAME}.axf ${PROJECT_NAME} info

########################################################
# Cleanup
########################################################

# Remove executable files
CLEAN_EXTRA += *.axf *.bin *.lst *.elf *.map #$(TIVAWARE_LIB)/driverlib/gcc/* 

include $(TOP)/py/mkrules.mk

