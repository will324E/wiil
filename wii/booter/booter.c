/*
 * Booter de Wiil : petit programme autonome copié en haut de la mémoire (0x81700000).
 * Il recopie une appli (.dol ou .elf) déjà chargée en MEM2 à ses adresses définitives,
 * lui transmet ses arguments (protocole « _arg » des homebrews), puis saute dessus.
 * Aucune bibliothèque : il doit survivre pendant que la mémoire basse est écrasée.
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define ARGV_MAGIC 0x5f617267

typedef struct {
	u32 text_pos[7], data_pos[11];
	u32 text_start[7], data_start[11];
	u32 text_size[7], data_size[11];
	u32 bss_start, bss_size, entry;
} dol_hdr;

typedef struct {
	u8 ident[16]; u16 type, machine; u32 version, entry, phoff, shoff, flags;
	u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} elf_hdr;

typedef struct { u32 type, offset, vaddr, paddr, filesz, memsz, flags, align; } elf_phdr;

typedef struct { int magic; char *cmd; int length; int argc; char **argv; char **end; } argv_t;

static void copie(u8 *d, const u8 *s, u32 n) { while (n--) *d++ = *s++; }
static void efface(u8 *d, u32 n) { while (n--) *d++ = 0; }

static void vide_cache(u32 debut, u32 n)
{
	u32 a = debut & ~31u, fin = debut + n;
	for (; a < fin; a += 32)
		__asm__ volatile("dcbf 0,%0\n\tsync\n\ticbi 0,%0" :: "r"(a));
	__asm__ volatile("sync\n\tisync");
}

static u32 adr(u32 a) { return (a & 0x3fffffff) | 0x80000000; }

/* fin la plus haute de l'appli copiée (MEM1) : les arguments seront rangés juste après */
static u32 fin_appli = 0;
static void note_fin(u32 a, u32 n) { if (a < 0x81700000 && a + n > fin_appli) fin_appli = a + n; }

/* Point d'entrée : se donne sa propre pile tout en haut de la MEM1 (l'ancienne pile
   sera écrasée par l'appli copiée), puis appelle booter_c avec r3/r4/r5 intacts. */
__asm__(".section .text.boot,\"ax\"\n"
        ".globl booter\n"
        "booter:\n"
        "\tlis 1,0x817F\n"
        "\tori 1,1,0xFF00\n"
        "\tb booter_c\n"
        ".previous\n");

void booter_c(const u8 *image, const char *args, u32 args_len) __attribute__((noreturn, used));
void booter_c(const u8 *image, const char *args, u32 args_len)
{
	u32 entree;
	if (image[0] == 0x7f && image[1] == 'E' && image[2] == 'L' && image[3] == 'F') {
		const elf_hdr *e = (const elf_hdr *)image;
		const elf_phdr *ph = (const elf_phdr *)(image + e->phoff);
		for (int i = 0; i < e->phnum; i++) {
			if (ph[i].type != 1 || ph[i].memsz == 0) continue;
			u32 dst = adr(ph[i].paddr ? ph[i].paddr : ph[i].vaddr);
			copie((u8 *)dst, image + ph[i].offset, ph[i].filesz);
			if (ph[i].memsz > ph[i].filesz) efface((u8 *)(dst + ph[i].filesz), ph[i].memsz - ph[i].filesz);
			vide_cache(dst, ph[i].memsz);
			note_fin(dst, ph[i].memsz);
		}
		entree = adr(e->entry);
	} else {
		const dol_hdr *h = (const dol_hdr *)image;
		for (int i = 0; i < 7; i++) if (h->text_size[i]) {
			copie((u8 *)adr(h->text_start[i]), image + h->text_pos[i], h->text_size[i]);
			vide_cache(adr(h->text_start[i]), h->text_size[i]);
			note_fin(adr(h->text_start[i]), h->text_size[i]);
		}
		for (int i = 0; i < 11; i++) if (h->data_size[i]) {
			copie((u8 *)adr(h->data_start[i]), image + h->data_pos[i], h->data_size[i]);
			vide_cache(adr(h->data_start[i]), h->data_size[i]);
			note_fin(adr(h->data_start[i]), h->data_size[i]);
		}
		if (h->bss_size) { efface((u8 *)adr(h->bss_start), h->bss_size); vide_cache(adr(h->bss_start), h->bss_size); note_fin(adr(h->bss_start), h->bss_size); }
		entree = adr(h->entry);
	}
	/* Arguments : l'appli les accepte si « _arg » suit son instruction d'entrée */
	if (args_len && *(volatile u32 *)(entree + 4) == ARGV_MAGIC) {
		/* comme la Chaîne Homebrew : juste après l'appli, en MEM1. libogc protège
		   cette zone (son tas commence après), alors qu'en haut de la MEM2 l'appli
		   finirait par écraser les arguments avant de les lire. */
		char *cmd = (char *)args;
		if (fin_appli && fin_appli + args_len + 1024 < 0x81700000) {
			cmd = (char *)((fin_appli + 31) & ~31u);
			copie((u8 *)cmd, (const u8 *)args, args_len);
			vide_cache((u32)cmd, args_len);
		}
		argv_t *a = (argv_t *)(entree + 8);
		a->magic = ARGV_MAGIC; a->cmd = cmd; a->length = (int)args_len;
		a->argc = 0; a->argv = 0; a->end = 0;
		vide_cache(entree, 64);
	}
	void (*lancer)(void) = (void (*)(void))entree;
	lancer();
	for (;;) ;
}
