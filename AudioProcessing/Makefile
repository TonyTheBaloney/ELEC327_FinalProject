# -------------------------------------------------------
#  Makefile for DaisySeed — libDaisy + DaisySP project
#  Source files: src/
#
#  Override library paths if needed:
#    make LIBDAISY_DIR=/path/to/libDaisy DAISYSP_DIR=/path/to/DaisySP
# -------------------------------------------------------

# Project name
TARGET = MyDaisyProject

# -------------------------------------------------------
# Paths — defaults assume libDaisy and DaisySP sit next
# to this Makefile in the project root
# -------------------------------------------------------
LIBDAISY_DIR ?= ./libDaisy
DAISYSP_DIR  ?= ./DaisySP
DAISYSP_LGPL_DIR = $(PROJECT_DIR)/DaisySP/DaisySP-LGPL/Source

# Source directory
SRC_DIR  = src

# Build directory
BUILD_DIR = build

# -------------------------------------------------------
# Collect source files
# -------------------------------------------------------
CPP_SOURCES = $(wildcard $(SRC_DIR)/*.cpp)
C_SOURCES   = $(wildcard $(SRC_DIR)/*.c)

# -------------------------------------------------------
# Toolchain
# -------------------------------------------------------
ARM_PREFIX = arm-none-eabi
CC         = $(ARM_PREFIX)-gcc
CXX        = $(ARM_PREFIX)-g++
AS         = $(ARM_PREFIX)-as
AR         = $(ARM_PREFIX)-ar
LD         = $(ARM_PREFIX)-g++
OBJCOPY    = $(ARM_PREFIX)-objcopy
SIZE       = $(ARM_PREFIX)-size
GDB        = $(ARM_PREFIX)-gdb

# -------------------------------------------------------
# Target MCU — STM32H750IB (Daisy Seed)
# -------------------------------------------------------
MCU = -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb

# -------------------------------------------------------
# Linker script
# -------------------------------------------------------
LDSCRIPT = $(LIBDAISY_DIR)/core/STM32H750IB_flash.lds

# -------------------------------------------------------
# Preprocessor defines
# -------------------------------------------------------
C_DEFS = \
	-DUSE_HAL_DRIVER \
	-DSTM32H750xx \
	-DCORE_CM7 \
	-DUSE_FULL_LL_DRIVER \
	-DHSE_VALUE=16000000 \
	-DARM_MATH_CM7

# -------------------------------------------------------
# Include paths — auto-discovered from library directories
# -------------------------------------------------------
LIBDAISY_INCLUDES = $(shell find $(LIBDAISY_DIR)/src $(LIBDAISY_DIR)/Drivers $(LIBDAISY_DIR)/Middlewares \
	-type d | sed 's/^/-I/')
DAISYSP_INCLUDES  = $(shell find $(DAISYSP_DIR)/Source $(DAISYSP_DIR)/DaisySP-LGPL \
	-type d | sed 's/^/-I/')

DAISYSP_LGPL_INCLUDES = $(shell find $(DAISYSP_LGPL_DIR) \
	-type d | sed 's/^/-I/')

C_INCLUDES = -I$(SRC_DIR) $(LIBDAISY_INCLUDES) $(DAISYSP_INCLUDES) $(DAISYSP_LGPL_INCLUDES)
# -------------------------------------------------------
# Compiler flags
# -------------------------------------------------------
OPTIMIZE = -O2

COMMON_FLAGS = \
    $(MCU) \
    $(C_DEFS) \
    $(C_INCLUDES) \
    $(OPTIMIZE) \
    -Wall \
    -Wno-missing-attributes \
    -Wno-array-bounds \
    -Wno-stringop-overflow \
    -fdata-sections \
    -ffunction-sections \

CFLAGS   = $(COMMON_FLAGS) -std=gnu11 \
	-Wno-int-conversion \
	-Wno-incompatible-pointer-types \
	-Wno-implicit-function-declaration
CXXFLAGS = $(COMMON_FLAGS) -std=gnu++14 -fno-exceptions -fno-rtti -Wno-register

ASFLAGS = $(MCU) -Wall -fdata-sections -ffunction-sections

# -------------------------------------------------------
# Linker flags
# -------------------------------------------------------
LDFLAGS = \
    $(MCU) \
    -T$(LDSCRIPT) \
    --specs=nano.specs \
    --specs=nosys.specs \
    -L$(LIBDAISY_DIR)/build \
    -L$(DAISYSP_DIR)/build \
	-L$(DAISYSP_DIR)/DaisySP-LGPL/build \
    -ldaisy \
    -ldaisysp \
	-ldaisysp-lgpl \
    -lc -lm \
    -Wl,--gc-sections \
    -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref \
    -Wl,--print-memory-usage

	

# -------------------------------------------------------
# Object files
# -------------------------------------------------------
OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(CPP_SOURCES:.cpp=.o)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))

vpath %.cpp $(SRC_DIR)
vpath %.c   $(SRC_DIR)

# -------------------------------------------------------
# Default target
# -------------------------------------------------------
.PHONY: all
all: check-paths $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin
	@echo ""
	@echo "  Build complete: $(TARGET)"

# -------------------------------------------------------
# Sanity-check that library paths exist before compiling
# -------------------------------------------------------
.PHONY: check-paths
check-paths:
	@test -f $(LIBDAISY_DIR)/src/daisy.h || \
		{ echo ""; \
		  echo "  ERROR: daisy.h not found."; \
		  echo "  LIBDAISY_DIR is set to: $(LIBDAISY_DIR)"; \
		  echo "  Run: make LIBDAISY_DIR=/correct/path/to/libDaisy"; \
		  echo ""; exit 1; }
	@test -d $(DAISYSP_DIR)/Source || \
		{ echo ""; \
		  echo "  ERROR: DaisySP Source dir not found."; \
		  echo "  DAISYSP_DIR is set to: $(DAISYSP_DIR)"; \
		  echo "  Run: make DAISYSP_DIR=/correct/path/to/DaisySP"; \
		  echo ""; exit 1; }

# -------------------------------------------------------
# Compile C++ sources
# -------------------------------------------------------
$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	@echo "  CXX  $<"
	@$(CXX) -c $(CXXFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" $< -o $@

# -------------------------------------------------------
# Compile C sources
# -------------------------------------------------------
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo "  CC   $<"
	@$(CC) -c $(CFLAGS) -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" $< -o $@

# -------------------------------------------------------
# Link
# -------------------------------------------------------
$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS)
	@echo "  LD   $@"
	@$(LD) $(OBJECTS) $(LDFLAGS) -o $@
	@$(SIZE) $@

# -------------------------------------------------------
# Binary output
# -------------------------------------------------------
$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	@echo "  BIN  $@"
	@$(OBJCOPY) -O binary $< $@

# -------------------------------------------------------
# Create build directory
# -------------------------------------------------------
$(BUILD_DIR):
	@mkdir -p $@

# -------------------------------------------------------
# Flash via DFU (USB) — hold BOOT button, then reset
# -------------------------------------------------------
.PHONY: program-dfu
program-dfu: $(BUILD_DIR)/$(TARGET).bin
	@echo "  Flashing via DFU..."
	dfu-util -a 0 -s 0x08000000:leave -D $<

# -------------------------------------------------------
# Flash via ST-Link / OpenOCD
# -------------------------------------------------------
.PHONY: program
program: $(BUILD_DIR)/$(TARGET).elf
	@echo "  Flashing via OpenOCD (ST-Link)..."
	openocd -f interface/stlink.cfg \
	        -f target/stm32h7x.cfg \
	        -c "program $< verify reset exit"

# -------------------------------------------------------
# Debug via GDB + OpenOCD
# -------------------------------------------------------
.PHONY: debug
debug: $(BUILD_DIR)/$(TARGET).elf
	@echo "  Starting OpenOCD GDB server..."
	openocd -f interface/stlink.cfg \
	        -f target/stm32h7x.cfg &
	$(GDB) $< -ex "target extended-remote :3333"

# -------------------------------------------------------
# Clean
# -------------------------------------------------------
.PHONY: clean
clean:
	@echo "  Cleaning build directory..."
	@rm -rf $(BUILD_DIR)

# -------------------------------------------------------
# Auto-generated dependency files
# -------------------------------------------------------
-include $(wildcard $(BUILD_DIR)/*.d)