FONT_SIZE ?= 8
FONT_DIR ?= /usr/share/fonts/TTF
FONTCONVERT ?= ../../scripts/fontconvert.py

.PHONY: fonts main/*
fonts: main/firacode.h main/firacode_bold.h main/firasans.h main/firasans_bold.h

main/firacode.h:
	 python3 $(FONTCONVERT) FiraCode $(FONT_SIZE) $(FONT_DIR)/FiraCode-Regular.ttf $(FONT_DIR)/Symbola.ttf > $@

main/firacode_bold.h:
	 python3 $(FONTCONVERT) FiraCode_Bold $(FONT_SIZE) $(FONT_DIR)/FiraCode-Bold.ttf $(FONT_DIR)/Symbola.ttf > $@

main/firasans.h:
	 python3 $(FONTCONVERT) FiraSans $(FONT_SIZE) $(FONT_DIR)/FiraSans-Regular.ttf $(FONT_DIR)/Symbola.ttf $(FONT_DIR)/FiraCode-Regular.ttf > $@

main/firasans_bold.h:
	 python3 $(FONTCONVERT) FiraSans_Bold $(FONT_SIZE) $(FONT_DIR)/FiraSans-Bold.ttf $(FONT_DIR)/Symbola.ttf $(FONT_DIR)/FiraCode-Bold.ttf > $@
