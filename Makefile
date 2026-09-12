TOPDIR := $(CURDIR)
TARGET := switch-drive
BUILD := build
SOURCES := switch/source
INCLUDES := switch/include
APP_TITLE := Switch Drive
APP_AUTHOR := Switch Drive contributors
APP_VERSION := 0.2.5
ICON := icon.jpg
ROMFS := romfs

ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
PKG_CONFIG := $(DEVKITPRO)/portlibs/switch/bin/aarch64-none-elf-pkg-config
CFLAGS := `$(PKG_CONFIG) --cflags sdl2 SDL2_ttf` -g -Wall -Wextra -O2 -ffunction-sections $(ARCH)
CXXFLAGS := $(CFLAGS) -std=gnu++20 -fno-rtti -fno-exceptions
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS := -lcurl -ljansson -lmbedcrypto -lz `$(PKG_CONFIG) --libs sdl2 SDL2_ttf` -lnx
LIBDIRS := $(PORTLIBS) $(LIBNX)

include $(DEVKITPRO)/libnx/switch_rules
LIBDIRS := $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export APP_ICON := $(CURDIR)/$(ICON)
export APP_ROMFS := $(CURDIR)/$(ROMFS)
export NROFLAGS := --icon=$(APP_ICON) --nacp=$(OUTPUT).nacp --romfsdir=$(APP_ROMFS)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
export OFILES_SRC := $(CPPFILES:.cpp=.o)
export OFILES := $(OFILES_SRC)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export CXXFLAGS += $(INCLUDE) -D__SWITCH__
export LD := $(CXX)
.PHONY: all clean $(BUILD)
all: $(BUILD)
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf
else
DEPENDS := $(OFILES:.o=.d)
CXXFLAGS += $(INCLUDE) -D__SWITCH__
.PHONY: all
all: $(OUTPUT).nro
$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp $(APP_ICON) $(wildcard $(APP_ROMFS)/*)
$(OUTPUT).elf: $(OFILES)
-include $(DEPENDS)
endif
