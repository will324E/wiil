# Wiil DSi — télécommande DSi pour le menu Wiil (BlocksDS)
BLOCKSDS	?= /opt/blocksds/core

NAME		:= wiil_dsi
GAME_TITLE	:= Wiil DSi
GAME_SUBTITLE	:= Télécommande de Wiil
GAME_AUTHOR	:= Wiil
GAME_ICON	:= icone.gif

ARM7ELF		:= $(BLOCKSDS)/sys/arm7/main_core/arm7_dswifi.elf
LIBS		:= -ldswifi9 -lnds9
LIBDIRS		:= $(BLOCKSDS)/libs/dswifi \
		   $(BLOCKSDS)/libs/libnds

include $(BLOCKSDS)/sys/default_makefiles/rom_arm9/Makefile
