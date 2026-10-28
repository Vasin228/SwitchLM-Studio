.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=/opt/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

TARGET      := SwitchLM-Studio
BUILD       := build
SOURCES     := source ../common
DATA        := data
INCLUDES    := source ../external/llama.cpp/include ../external/llama.cpp/ggml/include ../common
ROMFS       := romfs

APP_TITLE   := SwitchLM Studio
APP_AUTHOR  := vasin228
APP_VERSION := 1.0.0

ARCH        := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS      := -g -Wall -O3 -ffast-math $(ARCH) $(DEFINES)
CFLAGS      += $(INCLUDE) -D__SWITCH__

CXXFLAGS    := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++20

ASFLAGS     := -g $(ARCH)
LDFLAGS     := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS        := -L$(TOPDIR)/../external/llama.cpp/build2/src -L$(TOPDIR)/../external/llama.cpp/build2/ggml/src -Wl,--start-group -lllama -lggml -lggml-cpu -lggml-base -Wl,--end-group -lnx -lm -lpthread
LIBDIRS     := $(DEVKITPRO)/libnx $(TOPDIR)/../external/llama.cpp/build2/src $(TOPDIR)/../external/llama.cpp/build2/ggml/src

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
    export LD := $(CC)
else
    export LD := $(CXX)
endif

export OFILES_BIN   := $(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES       := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES_BIN   := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(CONFIG_JSON)),)
    jsons := $(wildcard *.json)
    ifneq (,$(findstring $(TARGET).json,$(jsons)))
        export APP_JSON := $(TOPDIR)/$(TARGET).json
    else
        ifneq (,$(findstring config.json,$(jsons)))
            export APP_JSON := $(TOPDIR)/config.json
        endif
    endif
else
    export APP_JSON := $(TOPDIR)/$(CONFIG_JSON)
endif

ifeq ($(strip $(ICON)),)
    icons := $(wildcard *.jpg)
    ifneq (,$(findstring $(TARGET).jpg,$(icons)))
        export APP_ICON := $(TOPDIR)/$(TARGET).jpg
    else
        ifneq (,$(findstring icon.jpg,$(icons)))
            export APP_ICON := $(TOPDIR)/icon.jpg
        endif
    endif
else
    export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
    export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
    export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(strip $(APP_TITLE)),)
    export NACPFLAGS += --title="$(APP_TITLE)"
endif

ifneq ($(strip $(APP_AUTHOR)),)
    export NACPFLAGS += --author="$(APP_AUTHOR)"
endif

ifneq ($(strip $(APP_VERSION)),)
    export NACPFLAGS += --version="$(APP_VERSION)"
endif

ifneq ($(strip $(ROMFS)),)
    ifneq ($(wildcard $(TOPDIR)/$(ROMFS)),)
        export NROFLAGS += --romfsdir="$(TOPDIR)/$(ROMFS)"
    endif
endif

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

else

.PHONY: all

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nro

$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
	@elf2nro $< $@ $(NROFLAGS)
	@echo "built ... $(notdir $@)"

$(OUTPUT).elf: $(OFILES)

$(OFILES_SOURCES): $(HFILES_BIN)

-include $(DEPENDS)

endif
