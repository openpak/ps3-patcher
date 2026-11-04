# OpenPak PS3 Patcher. Two steps:
#   make stage    fetch the OpenPak CA (pin-checked) and produce dist/ + the plugin poke header.
#   make plugin   build openpak_ca.sprx (needs PSL1GHT; see plugin/Makefile).
#   make pkg      build the installable OpenPak-PS3-Patcher-v<VERSION>.pkg (needs PSL1GHT)
#   make          stage + plugin.
# Override the vsh addresses for a non-4.93 firmware by exporting ADDR_* (see tools/stage.sh).

all: stage plugin

stage:
	sh tools/stage.sh $(CA)

plugin:
	$(MAKE) -C plugin

pkg: stage plugin
	$(MAKE) -C app pkg APP_VERSION=$(VERSION)

clean:
	$(MAKE) -C app clean
	$(MAKE) -C plugin clean
	rm -rf dist

VERSION ?= dev

.PHONY: all stage plugin pkg clean
