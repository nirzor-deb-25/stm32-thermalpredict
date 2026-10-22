# Use Windows Command Prompt for build commands.
SHELL := cmd.exe
.SHELLFLAGS := /C

# Allow recipe lines to start with > instead of a tab.
.RECIPEPREFIX := >

.DEFAULT_GOAL := all

TARGET := stm32_i2c_bme280
BUILD := build
LINKER_SCRIPT := STM32F439ZITX_FLASH.ld

CC := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE := arm-none-eabi-size

CPU_FLAGS := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft

CPPFLAGS := -IInc
CFLAGS := $(CPU_FLAGS) -std=c11 -Og -g3
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -MMD -MP

ASFLAGS := $(CPU_FLAGS) -g3 -x assembler-with-cpp

LDFLAGS := $(CPU_FLAGS) -T$(LINKER_SCRIPT) -nostartfiles
LDFLAGS += --specs=nano.specs --specs=nosys.specs
LDFLAGS += -Wl,--gc-sections
LDFLAGS += -Wl,-Map=$(BUILD)/$(TARGET).map

LDLIBS := -Wl,--start-group -lc -lgcc -Wl,--end-group

C_SOURCES := $(wildcard Src/*.c)
C_OBJECTS := $(patsubst Src/%.c,$(BUILD)/%.o,$(C_SOURCES))
STARTUP_OBJECT := $(BUILD)/startup_stm32f439zitx.o
OBJECTS := $(C_OBJECTS) $(STARTUP_OBJECT)
DEPS := $(C_OBJECTS:.o=.d)

ELF := $(BUILD)/$(TARGET).elf
BIN := $(BUILD)/$(TARGET).bin
HEX := $(BUILD)/$(TARGET).hex

.PHONY: all clean size

all: $(ELF) $(BIN) $(HEX)

$(BUILD):
>if not exist "$(BUILD)" mkdir "$(BUILD)"

# Compile each C source into its own object file.
$(BUILD)/%.o: Src/%.c Makefile | $(BUILD)
>$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Assemble the MCU startup code.
$(STARTUP_OBJECT): Startup/startup_stm32f439zitx.s Makefile | $(BUILD)
>$(CC) $(ASFLAGS) -c $< -o $@

# Link all object files into one executable.
$(ELF): $(OBJECTS) $(LINKER_SCRIPT) Makefile
>$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@
>$(SIZE) $@

# Generate firmware formats used by programming tools.
$(BIN): $(ELF)
>$(OBJCOPY) -O binary $< $@

$(HEX): $(ELF)
>$(OBJCOPY) -O ihex $< $@

size: $(ELF)
>$(SIZE) $(ELF)

clean:
>if exist "$(BUILD)" rmdir /S /Q "$(BUILD)"

# Read generated header dependencies.
-include $(DEPS)



# Override with: make flash PROGRAMMER="path/to/STM32_Programmer_CLI.exe"
PROGRAMMER ?= STM32_Programmer_CLI.exe

.PHONY: flash

flash: $(ELF)
>"$(PROGRAMMER)" -c port=SWD mode=UR -w "$(ELF)" -v -rst