#ifndef OPENPAK_PATCHER_H
#define OPENPAK_PATCHER_H

#include <stdbool.h>

#define OPENPAK_TITLE_ID   "OPNP00001"
#define OPENPAK_USRDIR     "/dev_hdd0/game/" OPENPAK_TITLE_ID "/USRDIR"
#define OPENPAK_DIR        "/dev_hdd0/openpak"
#define OPENPAK_BACKUP_DIR OPENPAK_DIR "/backup"
#define OPENPAK_LOG        OPENPAK_DIR "/patcher.log"
#define OPENPAK_PLUGIN_DST "/dev_hdd0/plugins/openpak_ca.sprx"
#define OPENPAK_BOOT_LIST  "/dev_hdd0/boot_plugins.txt"
#define OPENPAK_STATUS     OPENPAK_DIR "/plugin.status"
#define OPENPAK_DNS        "145.241.199.19"

typedef struct {
	char fw[16];          /* "4.93" */
	bool fw_known;        /* version.txt was readable */
	bool fw_ok;           /* 4.93 CEX */
	bool peek;            /* lv2 peek (syscall 6) answers: CFW or HEN */
	bool cobra;           /* Cobra syscall 8 opcode 0x7000 answers: Cobra active */
	bool stage2;          /* an active stage2 file exists (sys/stage2.bin, rebug/cobra/stage2.cex|dex) */
	bool stage2_off;      /* only the renamed (disabled) stage2 files exist */
	char payload[24];     /* "Cobra 8.5", "Mamba ..", "PS3HEN .." or "" */
	bool hen;             /* PS3HEN marker present */
	unsigned long long peek_raw, cobra_raw;
	unsigned int version_raw;   /* the u32 Cobra wrote through the pointer (0x99999999 = untouched) */
	bool ca_openpak;      /* both CA files in dev_flash equal the bundled OpenPak CA */
	bool plugin_listed;   /* our line is in boot_plugins.txt */
	bool plugin_file;     /* sprx present in /dev_hdd0/plugins */
	bool plugin_ran;      /* plugin.status written by this boot's plugin */
	int  pokes_applied, pokes_verified, pokes_total;
	bool backup_ok;       /* both backups exist */
	bool bundle_ok;       /* CA01/CA59/sprx are in USRDIR */
} op_status;

void op_status_read(op_status *s);
bool op_is_openpak(const op_status *s);      /* CA installed and plugin listed */
bool op_supported(const op_status *s, char *why, int why_len);

/* Both return true on success; on failure err holds a one-line reason. */
bool op_switch_openpak(char *err, int err_len);
bool op_switch_psn(char *err, int err_len);

void op_reboot(void);

#endif
