/*
 * Lanceur de démarrage de Wiil, à installer dans Priiloader (« Install File »).
 * Il ne fait qu'une chose : charger sd:/apps/wiil/boot.dol et le lancer.
 * Ainsi, chaque nouvelle version de Wiil copiée sur la carte SD est prise en compte
 * sans rien réinstaller. Sans carte SD (ou sans Wiil), il ouvre le menu Wii normal.
 */
#include <gccore.h>
#include <fat.h>
#include <ogc/irq.h>
#include <sdcard/wiisd_io.h>
#include <stdio.h>
#include <string.h>

#include "booter_bin.h"

#define CHEMIN "sd:/apps/wiil/boot.dol"

/* Mots magiques de Priiloader : « Pune » = démarrer le vrai menu Wii (sans autoboot),
   pour ne jamais tourner en rond si Wiil est introuvable. */
static void menu_wii(void)
{
	const u8 pune[4] = { 'P', 'u', 'n', 'e' };
	memcpy((void *)0x8132FFFB, pune, 4);
	memcpy((void *)0x817FEFF0, pune, 4);
	DCFlushRange((void *)0x8132FFE0, 0x40);
	DCFlushRange((void *)0x817FEFE0, 0x40);
	SYS_ResetSystem(SYS_RETURNTOMENU, 0, 0);
}

int main(int argc, char **argv)
{
	(void)argc; (void)argv;
	if (!fatMountSimple("sd", &__io_wiisd)) menu_wii();
	FILE *f = fopen(CHEMIN, "rb");
	if (!f) { fatUnmount("sd:"); menu_wii(); }
	fseek(f, 0, SEEK_END);
	long taille = ftell(f);
	fseek(f, 0, SEEK_SET);
	u32 haut = (u32)SYS_GetArena2Hi();
	u8 *image = (u8 *)((haut - taille - 64) & ~31u);
	char *args = (char *)(((u32)image - 1024) & ~31u);
	SYS_SetArena2Hi(args);
	if (taille <= 0 || fread(image, 1, taille, f) != (size_t)taille) { fclose(f); fatUnmount("sd:"); menu_wii(); }
	fclose(f);
	fatUnmount("sd:");
	int n = snprintf(args, 512, "%s", CHEMIN) + 1;
	args[n++] = 0;
	DCFlushRange(image, taille);
	DCFlushRange(args, 1024);
	memcpy((void *)0x81700000, booter_bin, booter_bin_size);
	DCFlushRange((void *)0x81700000, booter_bin_size);
	ICInvalidateRange((void *)0x81700000, booter_bin_size);
	SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
	u32 niveau = IRQ_Disable();
	(void)niveau;
	void (*booter)(const u8 *, const char *, u32) = (void (*)(const u8 *, const char *, u32))0x81700000;
	booter(image, args, n);
	return 0;
}
