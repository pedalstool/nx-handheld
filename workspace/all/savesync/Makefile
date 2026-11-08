ifeq (,$(CROSS_COMPILE))
$(error missing CROSS_COMPILE for this toolchain)
endif

ifeq (,$(PLATFORM))
PLATFORM=$(UNION_PLATFORM)
endif

TARGET = savesync
PRODUCT = build/$(PLATFORM)/$(TARGET).elf

CC = $(CROSS_COMPILE)gcc
CFLAGS = -Os -s -Wall -Wextra -std=gnu99 -D_GNU_SOURCE

SRC = savesync.c savesync_plan.c savesync_fs.c savesync_net.c savesync_ui.c

all:
	mkdir -p build/$(PLATFORM)
	$(CC) $(SRC) -o $(PRODUCT) $(CFLAGS)

clean:
	rm -f $(PRODUCT)
