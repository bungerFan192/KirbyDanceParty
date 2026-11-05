#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "DEVKITPRO is not set. Please install devkitPro.")
endif

include $(DEVKITPRO)/3ds_rules

TARGET      := KirbyDanceParty
BUILD       := build
SOURCES     := source
DATA        := data
INCLUDES    := include

ARCH        := -march=armv6k -mtune=mpcore -mfloat-abi=hard

CFLAGS      := -g -Wall -O2 -mword-relocations -ffunction-sections \
               $(ARCH) $(INCLUDE) -D__3DS__
CXXFLAGS    := $(CFLAGS) -std=gnu++17 -fno-rtti
ASFLAGS     := -g $(ARCH)
LDFLAGS     := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(TARGET).map

LIBS        := -lcitro3d -lctru -lm
LIBDIRS     := $(LIBGCC)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)

export VPATH  := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                 $(foreach dir,$(DATA),$(CURDIR)/$(dir))

CFILES       := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES     := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES       := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES     := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export OFILES := $(addprefix $(BUILD)/,$(CPPFILES:.cpp=.o)) \
                 $(addprefix $(BUILD)/,$(CFILES:.c=.o)) \
                 $(addprefix $(BUILD)/,$(SFILES:.s=.o)) \
                 $(addprefix $(BUILD)/,$(BINFILES))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(SOURCES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(DATA),-I$(CURDIR)/$(dir))

.PHONY: all clean

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh $(TARGET).map

else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).3dsx: $(OUTPUT).elf

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
