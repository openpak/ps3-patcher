#ifndef OPENPAK_PATCHER_H
#define OPENPAK_PATCHER_H

#include <stdbool.h>

#define OPENPAK_TITLE_ID   "OPNP00001"
#define OPENPAK_USRDIR     "/dev_hdd0/game/" OPENPAK_TITLE_ID "/USRDIR"
#define OPENPAK_DIR        "/dev_hdd0/openpak"
#define OPENPAK_BACKUP_DIR OPENPAK_DIR "/backup"
#define OPENPAK_LOG        OPENPAK_DIR "/patcher.log"
#define OPENPAK_PLUGIN_DST "/dev_hdd0/plugins/openpak_ca.sprx"
#define OPENPAK_PLUGIN_DST_B "/dev_hdd0/plugins/openpak_cb.sprx"   /* legacy (v0.2.9, v0.2.10): removed on update */
#define OPENPAK_PLUGIN_DST_C "/dev_hdd0/plugins/openpak_cc.sprx"   /* third wrapper: fself with webMAN's app info */
#define OPENPAK_PLUGIN_DST_D "/dev_hdd0/plugins/openpak_cd.sprx"   /* optional: scetool-signed SELF (webMAN MOD's own packaging) */
#define OPENPAK_BOOT_LIST  "/dev_hdd0/boot_plugins.txt"
#define OPENPAK_STATUS     OPENPAK_DIR "/plugin.status"
#define OPENPAK_STATUS_B   OPENPAK_DIR "/plugin.b.status"
#define OPENPAK_STATUS_C   OPENPAK_DIR "/plugin.c.status"
#define OPENPAK_STATUS_D   OPENPAK_DIR "/plugin.d.status"
#define OPENPAK_DIAG       OPENPAK_DIR "/diag.txt"
#define OPENPAK_INSTALLED  OPENPAK_DIR "/installed.txt"
#define OPENPAK_REDIRECT_STATUS OPENPAK_DIR "/redirect.status"

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
	bool plugin_d_ran;    /* plugin.d.status written (signed wrapper) */
	bool plugin_c_ran;    /* plugin.c.status written (third SELF wrapper) */
	char detail[1500];    /* the details page: PS3MAPI, every slot, boot list, file checks, last load */
	char ps3mapi_note[160];   /* raw PS3MAPI answers, for the status screen */
	bool redirect_ran;    /* redirect.status written by this boot's plugin */
	char redirect_state[16];  /* ok | hen | nolv2 | nothread | restored */
	int  redirect_names, redirect_of, redirect_live, redirect_passes, redirect_failed;
	bool vsh_found;       /* PS3MAPI found the vsh process */
	int  vsh_pid;
	int  live_poke_patched, live_poke_orig, live_poke_total;  /* the u32 pokes, read from vsh memory */
	bool live_cn_known, live_cn_patched;                      /* the pinned CN string field */
	int  live_names_total, live_names_rewritten, live_names_orig;  /* static PSN names in vsh */
	bool plugins_known;   /* PS3MAPI answered the VSH plugin query */
	bool plugin_loaded;   /* Cobra has openpak_ca.sprx in a VSH plugin slot */
	int  plugin_slot;
	int  plugins_other;   /* other VSH plugins loaded */
	bool installed;       /* OpenPak setup present: version marker or plugin line */
	char inst_ver[16];    /* version recorded at install time ("" = unknown, older than 0.2.8) */
	bool plugin_current;  /* the installed sprx is byte-identical to the one this app carries */
	bool update_needed;   /* installed, but version, plugin file, CA files or boot line differ */
	bool backup_ok;       /* both backups exist */
	bool bundle_ok;       /* CA01/CA59/sprx are in USRDIR */
} op_status;

void op_status_read(op_status *s);
bool op_is_openpak(const op_status *s);
      /* CA installed and plugin listed */
bool op_supported(const op_status *s, char *why, int why_len);

/* Both return true on success; on failure err holds a one-line reason. */
bool op_switch_openpak(char *err, int err_len);
bool op_switch_psn(char *err, int err_len);

/* Load the plugin into a free Cobra VSH plugin slot now (Cobra syscall 8 opcode 0x1EE7). ret is the
 * raw return value, slot the slot used (-1 when none was free). */
bool op_load_plugin(int *ret, int *slot);
/* Text of the last op_load_plugin: one line per wrapper tried, with the raw Cobra return code. */
const char *op_load_report(void);

void op_reboot(void);

#endif
