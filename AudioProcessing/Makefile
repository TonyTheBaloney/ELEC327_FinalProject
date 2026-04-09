# ==== Project ====
TARGET = Blink
BUILD_DIR = build

# ==== Sources ====
CPP_SOURCES = src/Blink.cpp

# ==== Paths ====
LIBDAISY_DIR = ./libDaisy
DAISYSP_DIR  = ./DaisySP

# ==== Toolchain ====
PREFIX  = arm-none-eabi
CC      = $(PREFIX)-gcc
CXX     = $(PREFIX)-g++
AS      = $(PREFIX)-as
CP      = $(PREFIX)-objcopy
SZ      = $(PREFIX)-size

# ==== MCU Flags (Daisy Seed STM32H750) ====
CPU = -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard

CFLAGS = $(CPU) -O2 -Wall -fdata-sections -ffunction-sections
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
LDFLAGS = $(CPU) -Wl,--gc-sections
LDFLAGS += -T$(LIBDAISY_DIR)/core/STM32H750IB_flash.lds

# ==== Includes ====
INCLUDES = \
-I$(LIBDAISY_DIR) \
-I$(LIBDAISY_DIR)/src \
-I$(DAISYSP_DIR)

# ==== Libraries ====
LIBDAISY_LIB = $(LIBDAISY_DIR)/build/libdaisy.a
DAISYSP_LIB  = $(DAISYSP_DIR)/build/libdaisysp.a

LIBS = $(LIBDAISY_LIB) $(DAISYSP_LIB)

# ==== Build Files ====
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(CPP_SOURCES:.cpp=.o)))
TARGET_ELF = $(BUILD_DIR)/$(TARGET).elf
TARGET_BIN = $(BUILD_DIR)/$(TARGET).bin

# ==== Default Target ====
all: $(TARGET_BIN)

# ==== Build libs first ====
$(LIBDAISY_LIB):
	$(MAKE) -C $(LIBDAISY_DIR)

$(DAISYSP_LIB):
	$(MAKE) -C $(DAISYSP_DIR)

# ==== Compile ====
$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# ==== Link ====
$(TARGET_ELF): $(OBJECTS) libs

# ==== Convert to BIN ====
$(TARGET_BIN): $(TARGET_ELF)
	$(CP) -O binary $< $@
	$(SZ) $<

# ==== Build dir ====
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

libs:
	$(MAKE) -C $(LIBDAISY_DIR)
	$(MAKE) -C $(DAISYSP_DIR)

# ==== Flash ====
flash: $(TARGET_BIN)
	sudo dfu-util -a 0 -i 0 -s 0x08000000:leave -D $(TARGET_BIN)

# ==== Clean ====
clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C $(LIBDAISY_DIR) clean
	$(MAKE) -C $(DAISYSP_DIR) clean

.PHONY: all clean flash