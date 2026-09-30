.SUFFIXES:
export DEVKITPRO 	:= /opt/devkitpro
export DEVKITPPC	:= $(DEVKITPRO)/devkitPPC
include $(DEVKITPPC)/wii_rules
CC 					:= $(DEVKITPPC)/bin/powerpc-eabi-g++
MACHDEP := -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -fsigned-char -ffast-math -funroll-loops -fauto-inc-dec -finline-functions
INCLUDES += -I/opt/devkitpro/portlibs/wii/include -I/opt/devkitpro/portlibs/ppc/include -I/opt/devkitpro/libogc/include
CXXFLAGS += -O2 \
-DENDIAN_BIG \
$(INCLUDES) $(MACHDEP) 
LDFLAGS += $(CXXFLAGS) \
-Wl,-Map,$(notdir $@).map \
-L/opt/devkitpro/portlibs/wii/lib \
-L/opt/devkitpro/portlibs/ppc/lib \
-L/opt/devkitpro/libogc/lib \
-L/opt/devkitpro/libogc/lib/wii \
-lasnd -lwiikeyboard -lfat -lwiiuse -lbte -logc -lm
TARGET  := NooDS-Wii
EXCLUDE :=
SRCDIR 	:= NooDS-Wii
# Build-time exclusion is usable before runtime integration exists.
NOODS_JIT ?= 1
ifeq ($(NOODS_JIT),0)
EXCLUDE += $(wildcard $(SRCDIR)/jit_*.cpp)
CXXFLAGS += -DNOODS_NO_JIT
endif
C_SOURCES = $(foreach dir, $(SRCDIR), $(filter-out $(EXCLUDE), $(wildcard $(dir)/*.cpp)))
SOURCES = $(C_SOURCES) 
OBJECTS = $(SOURCES:.cpp=.o)

ELF2DOL  := elf2dol
all: $(TARGET).dol
$(TARGET).dol: $(TARGET).elf
	$(ELF2DOL) $< $@
$(TARGET).elf: $(OBJECTS)
	$(CC) $^ -o $@ $(LDFLAGS)
%.o: %.cpp
	$(CC) -c $< -o $@ $(CXXFLAGS)
clean:
	rm -f $(TARGET).elf $(TARGET).dol $(TARGET).map $(OBJECTS)
.PHONY: clean test
