# OpenPak PS3 Patcher app -> OpenPak-PS3-Patcher-<tag>.pkg (PSL1GHT).
#   make -C app pkg APP_VERSION=0.2.0     (needs PS3DEV/PSL1GHT; run from the repo root via `make pkg`)
# The package carries USRDIR/ca/{CA01,CA59}.cer (staged by tools/stage.sh) and USRDIR/openpak_ca.sprx.

ifeq ($(strip $(PS3DEV)),)
$(error PS3DEV is not set)
endif
ifeq ($(strip $(PSL1GHT)),)
export PSL1GHT := $(PS3DEV)
endif

include $(PSL1GHT)/ppu_rules

TARGET     := openpak-ps3-patcher
APP_VERSION?= dev
TITLE      := OpenPak PS3 Patcher
APPID      := OPNP00001
CONTENTID  := UP0001-$(APPID)_00-0000000000000000
SFOXML     := $(CURDIR)/sfo.xml
ICON0      := $(CURDIR)/ICON0.PNG
PKGFILES   := $(CURDIR)/build/pkgfiles
BUILDDIR   := $(CURDIR)/build

CFLAGS     := -O2 -Wall -mcpu=cell $(MACHDEP) $(LIBPSL1GHT_INC) -I../plugin -DOPENPAK_VERSION=\"$(APP_VERSION)\"
LDFLAGS    := $(MACHDEP) -Wl,-Map,$(BUILDDIR)/$(TARGET).map $(LIBPSL1GHT_LIB)
LIBS       := -lrsx -lgcm_sys -lio -lsysfs -lsysutil -lsysmodule -lrt -llv2 -lm

OBJS := build/main.o build/patcher.o

$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) $(LIBS) -o $@

build/%.o: source/%.c source/patcher.h source/vshnames.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

# stage the package payload
stage-pkg: $(TARGET).elf
	@test -f ../plugin/openpak_ca.sprx -a -f ../plugin/openpak_cb.sprx || { echo "build the plugin first (make -C ../plugin)"; exit 1; }
	@test -f ../dist/dev_flash/data/cert/CA01.cer || { echo "run make stage first"; exit 1; }
	rm -rf $(PKGFILES)
	mkdir -p $(PKGFILES)/USRDIR/ca
	cp ../dist/dev_flash/data/cert/CA01.cer ../dist/dev_flash/data/cert/CA59.cer $(PKGFILES)/USRDIR/ca/
	cp ../plugin/openpak_ca.sprx ../plugin/openpak_cb.sprx $(PKGFILES)/USRDIR/

pkg: stage-pkg $(TARGET).pkg
	cp $(TARGET).pkg OpenPak-PS3-Patcher-v$(APP_VERSION).pkg

clean:
	rm -rf build *.elf *.self *.pkg

.PHONY: stage-pkg pkg clean
