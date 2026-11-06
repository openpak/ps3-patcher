/*
 * OpenPak PS3 Patcher -- XMB app. Flow mirrors the Switch Patcher: a status screen, one choice
 * (switch to OpenPak, or back to PSN), a result, then a restart prompt. The PS3 has no stock UI
 * toolkit for homebrew, so every screen is a system message dialog over a plain background.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <ppu-lv2.h>
#include <sysutil/sysutil.h>
#include <sysutil/msg.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include <lv2/process.h>
#include <io/pad.h>

#include "patcher.h"

#ifndef OPENPAK_VERSION
#define OPENPAK_VERSION "dev"
#endif

#define CB_SIZE   0x100000
#define HOST_SIZE (32 * 1024 * 1024)

static gcmContextData *ctx;
static u32 width, height, pitch;
static u32 *fb[2];
static int cur_fb;
static volatile bool want_exit;

static void sysutil_cb(u64 status, u64 param, void *usrdata)
{
	(void)param; (void)usrdata;
	if (status == SYSUTIL_EXIT_GAME)
		want_exit = true;
}

static void wait_flip(void)
{
	while (gcmGetFlipStatus() != 0)
		usleep(200);
	gcmResetFlipStatus();
}

static void fill_background(u32 *p)
{
	/* paper-dark background with a thin accent bar; drawn once, never per frame */
	u32 i, n = width * height;
	for (i = 0; i < n; i++)
		p[i] = 0x00101418;
	for (i = 0; i < width * 6; i++)
		p[width * 90 + i] = 0x0022b8cf;
}

/* Double-buffered flip with vsync. Both buffers already hold the same constant background, so a
 * flip changes nothing visible; its job is to keep the display pipeline (and the system dialog
 * composited on top of it) running. The previous version redrew the whole buffer on every call and
 * never waited for the flip, writing into a buffer that could still be on screen: the dialog and
 * a half-drawn background alternated, which is the flicker. */
static bool first_flip = true;

static void present(void)
{
	if (first_flip)
		gcmResetFlipStatus();
	else
		wait_flip();
	first_flip = false;
	gcmSetFlip(ctx, cur_fb);
	rsxFlushBuffer(ctx);
	gcmSetWaitFlip(ctx);
	cur_fb ^= 1;
}

static bool video_init(void)
{
	void *host = memalign(1024 * 1024, HOST_SIZE);
	videoState state;
	videoResolution res;
	videoConfiguration vc;
	int i;

	if (!host) return false;
	rsxInit(&ctx, CB_SIZE, HOST_SIZE, host);
	if (!ctx) return false;
	if (videoGetState(0, 0, &state) != 0 || state.state != 0) return false;
	if (videoGetResolution(state.displayMode.resolution, &res) != 0) return false;
	memset(&vc, 0, sizeof(vc));
	vc.resolution = state.displayMode.resolution;
	vc.format = VIDEO_BUFFER_FORMAT_XRGB;
	vc.pitch = res.width * 4;
	vc.aspect = state.displayMode.aspect;
	if (videoConfigure(0, &vc, NULL, 0) != 0) return false;
	width = res.width;
	height = res.height;
	pitch = res.width * 4;
	gcmSetFlipMode(GCM_FLIP_VSYNC);
	for (i = 0; i < 2; i++) {
		u32 off;
		fb[i] = rsxMemalign(64, pitch * height);
		if (!fb[i]) return false;
		rsxAddressToOffset(fb[i], &off);
		gcmSetDisplayBuffer(i, off, pitch, width, height);
	}
	fill_background(fb[0]);
	fill_background(fb[1]);
	gcmResetFlipStatus();
	present();
	return true;
}

static volatile int dlg_done;
static volatile int dlg_btn;

static void dlg_cb(msgButton b, void *u)
{
	(void)u;
	dlg_btn = b;
	dlg_done = 1;
}

/* Modal dialog; returns true for Yes/OK. */
static bool ask(bool yesno, const char *text)
{
	dlg_done = 0;
	dlg_btn = MSG_DIALOG_BTN_NONE;
	msgDialogOpen2((msgType)(MSG_DIALOG_NORMAL | (yesno ? MSG_DIALOG_BTN_TYPE_YESNO : MSG_DIALOG_BTN_TYPE_OK)),
		       text, dlg_cb, NULL, NULL);
	while (!dlg_done && !want_exit) {
		sysUtilCheckCallback();
		present();
	}
	if (!dlg_done) { msgDialogAbort(); msgDialogClose(500.0f); }
	return dlg_btn == MSG_DIALOG_BTN_YES || dlg_btn == MSG_DIALOG_BTN_OK;
}

/* System message dialogs hold about 512 bytes, so long text goes out in pages cut at line ends. */
static void ask_pages(const char *text)
{
	char page[480];
	const char *p = text;
	while (*p && !want_exit) {
		size_t n = 0, last = 0;
		while (p[n] && n < sizeof(page) - 24) {
			if (p[n] == '\n') last = n + 1;
			n++;
		}
		if (p[n] && last) n = last;
		memcpy(page, p, n);
		page[n] = 0;
		p += n;
		if (*p == '\n') p++;
		if (n) ask(false, page);
	}
}

static void build_status(const op_status *s, char *out, size_t n)
{
	bool on = op_is_openpak(s);
	char plug[64], catch[200], live[160], cobp[64];

	if (!s->plugin_listed) snprintf(plug, sizeof(plug), "not installed");
	else if (!s->plugin_ran && !s->plugin_c_ran && !s->plugin_d_ran) snprintf(plug, sizeof(plug), "installed, not loaded at boot");
	else {
		int a = s->pokes_applied;
		int t = s->pokes_total;
		snprintf(plug, sizeof(plug), "loaded %d/%d (%s%s%s)", a, t, s->plugin_ran ? "A" : "", s->plugin_c_ran ? "C" : "", s->plugin_d_ran ? "D" : "");
	}

	if (!s->plugins_known) snprintf(cobp, sizeof(cobp), "no answer from PS3MAPI");
	else if (s->plugin_loaded) snprintf(cobp, sizeof(cobp), "OpenPak loaded (slot %d)", s->plugin_slot);
	else snprintf(cobp, sizeof(cobp), "OpenPak NOT loaded (%d others)", s->plugins_other);

	if (!s->plugin_listed) snprintf(catch, sizeof(catch), "off (not installed)");
	else if (!s->catcher_ran) snprintf(catch, sizeof(catch), "not run yet");
	else if (!strcmp(s->catcher_state, "on"))
		snprintf(catch, sizeof(catch), "on (list %s v%d, %d families), %u caught, %u passed",
			 s->catcher_verified[0] ? s->catcher_verified : "bundled", s->catcher_version,
			 s->catcher_families, s->catcher_caught, s->catcher_passed);
	else if (!strncmp(s->catcher_state, "pass-through", 12))
		snprintf(catch, sizeof(catch), "%s (installed, nothing redirected), %u lookups passed",
			 s->catcher_state, s->catcher_passed);
	else
		snprintf(catch, sizeof(catch), "%s", s->catcher_state);   /* "off - <reason>", "waiting for the XMB" */

	if (!s->vsh_found) snprintf(live, sizeof(live), "vsh not found");
	else snprintf(live, sizeof(live), "pokes %d/%d, CN %s, Qhook %s",
		      s->live_poke_patched, s->live_poke_total,
		      s->live_cn_known ? (s->live_cn_patched ? "yes" : "no") : "?",
		      s->live_qhook ? "installed" : "default");

	snprintf(out, n,
		 "OpenPak PS3 Patcher %s\n\n"
		 "Network: %s\n"
		 "Firmware: %s%s\n"
		 "Cobra: %s, HEN: %s\n"
		 "stage2: %s\n"
		 "Cobra plugins: %s\n"
		 "Plugin: %s\n"
		 "vsh now: %s\n"
		 "DNS catcher: %s\n"
		 "PS3MAPI: %s\n"
		 "OpenPak CA: %s",
		 OPENPAK_VERSION,
		 on ? "OpenPak" : "PSN",
		 s->fw_known ? s->fw : "unknown", s->fw_ok ? " CEX" : "",
		 s->cobra ? s->payload : "appears to be off", s->hen ? "yes" : "no",
		 s->stage2 ? "present" : s->stage2_off ? "renamed (disabled)" : "not found",
		 cobp,
		 plug,
		 live,
		 catch,
		 s->ps3mapi_note[0] ? s->ps3mapi_note : "-",
		 s->ca_openpak ? "installed" : "not installed");
}

/* Every path ends here, in this order: no dialog open, last flip finished, RSX command stream
 * drained (rsxFinish blocks until the GPU reached the reference), callback unregistered, then
 * sysProcessExit. No reboot or other lv2 call is made on a quit path. v0.2.0 instead returned from
 * main with a flip still queued (gcmSetWaitFlip issued, never waited) and the RSX live, which is
 * the most likely cause of the whole-console freeze reported on hardware. */
static void quit_clean(void)
{
	int i;
	for (i = 0; i < 3; i++) {
		sysUtilCheckCallback();
		present();
	}
	wait_flip();
	rsxFinish(ctx, 1);
	sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
	sysProcessExit(0);
}


/* L1 held at launch = safe mode. Polled for about half a second (the first reads can be empty). */
static bool l1_held(void)
{
	padInfo info;
	padData d;
	int i, j;
	bool held = false;
	if (ioPadInit(7) != 0) return false;
	for (i = 0; i < 15 && !held; i++) {
		if (ioPadGetInfo(&info) == 0)
			for (j = 0; j < MAX_PADS && j < 7; j++)
				if (info.status[j] && ioPadGetData(j, &d) == 0 && d.len > 0 && d.BTN_L1) held = true;
		usleep(33000);
	}
	ioPadEnd();
	return held;
}

static const char *plugin_desc(const op_quick *q)
{
	if (!q->plugin_file) return "not installed";
	return q->plugin_matches ? "pass-through test build (this package)" : "another version (NOT this package)";
}

static const char *list_desc(const op_quick *q)
{
	if (q->listed) return "ON (boot_plugins.txt names OpenPak)";
	if (q->boot_list) return "off (boot_plugins.txt has no OpenPak line)";
	return q->boot_list_off ? "off (renamed to boot_plugins.off)" : "off (no boot_plugins.txt)";
}

/* Safe mode: nothing is read from the console; only the two file actions are offered. */
static void safe_mode(bool by_l1)
{
	op_quick q;
	char t[480], err[200];
	op_quick_read(&q);
	snprintf(t, sizeof(t), "OpenPak PS3 Patcher %s - SAFE MODE (%s).\n\nNothing is read from the console in "
		 "safe mode.\nPlugin file: %s\nBoot plugin: %s", OPENPAK_VERSION,
		 by_l1 ? "L1 held" : "openpak_nohook present", plugin_desc(&q), list_desc(&q));
	ask(false, t);
	if (q.boot_list && !want_exit &&
	    ask(true, "Remove the OpenPak boot plugin?\n\nRenames /dev_hdd0/boot_plugins.txt to boot_plugins.off, so "
		      "no boot plugin loads on the next start.")) {
		if (!op_disable_boot_plugin(err, sizeof(err))) ask(false, err);
	}
	if (!want_exit && ask(true, "Install this package's pass-through plugin file into /dev_hdd0/plugins?\n\n"
				    "The boot list is not changed.")) {
		if (!op_install_plugin_file(err, sizeof(err))) ask(false, err);
	}
	op_quick_read(&q);
	snprintf(t, sizeof(t), "Done.\n\nPlugin file: %s\nBoot plugin: %s\n\nRestart the console when you are ready.",
		 plugin_desc(&q), list_desc(&q));
	ask(false, t);
}

/* The normal first screen: file state only, then each step is a separate, explicit choice. Returns
 * true when the user asked for the full status (which reads vsh memory through Cobra). */
static bool steps(void)
{
	op_quick q;
	char t[480], err[200];

	op_quick_read(&q);
	if (!q.bundle_ok) { ask(false, "This package's plugin file is missing. Reinstall the package."); return false; }

	if (!q.plugin_matches && !want_exit) {
		snprintf(t, sizeof(t), "OpenPak PS3 Patcher %s\n\nPlugin file: %s\nBoot plugin: %s\n\nInstall the "
			 "pass-through plugin file now? The boot list is not changed.", OPENPAK_VERSION, plugin_desc(&q),
			 list_desc(&q));
		if (ask(true, t)) {
			if (!op_install_plugin_file(err, sizeof(err))) { ask(false, err); return false; }
			op_quick_read(&q);
		}
	}
	if (q.listed && !q.plugin_matches && !want_exit) {
		if (ask(true, "The boot list names an OpenPak plugin that is not this package's file. Remove the "
			      "OpenPak boot plugin (rename boot_plugins.txt to boot_plugins.off)?")) {
			if (!op_disable_boot_plugin(err, sizeof(err))) ask(false, err);
			op_quick_read(&q);
		}
	}
	if (q.plugin_matches && !want_exit) {
		char res[400], msg[480];
		op_step_result(res, sizeof(res));
		if (q.listed) {
			/* a step is armed for the next boot */
			snprintf(msg, sizeof(msg), "A step is armed for the next boot (boot_plugins.txt names OpenPak; the "
				 "plugin removes that line itself as soon as it starts).\n\n%s\n\nRestart now?", res);
			if (ask(true, msg)) { op_reboot(); sleep(10); return false; }
			if (ask(true, "Disarm it instead (rename boot_plugins.txt to boot_plugins.off)?")) {
				if (!op_disable_boot_plugin(err, sizeof(err))) ask(false, err);
				else ask(false, "Disarmed. Nothing loads at the next boot.");
			}
			return false;
		}
		/* v0.2.18-bt: one boot-time, one-shot step per restart */
		{
			static const struct { char step; const char *text; } st[6] = {
				{ 'a', "Step A at the next boot: the plugin only (no pokes, no thread)?" },
				{ 'b', "Step B at the next boot: plugin + thread (logs \"alive\" every 5 s)?" },
				{ 'c', "Step C at the next boot: thread + the CA trust pokes?" },
				{ 'd', "Step D at the next boot: install a bare hook (returns at once, touches nothing) at 45 s?" },
				{ 'e', "Step E at the next boot: hook that counts each lookup and passes it through?" },
				{ 'f', "Step F at the next boot: hook that redirects PlayStation names and passes the rest?" },
			};
			int k;
			snprintf(msg, sizeof(msg), "Step test (boot-time, one boot only). Each step is armed for the "
				 "next boot and removes itself as it starts, so a freeze clears with a power cycle.\n\n%s",
				 res);
			ask(false, msg);
			for (k = 0; k < 6 && !want_exit; k++) {
				if (!ask(true, st[k].text)) continue;
				if (!op_step_arm(st[k].step, err, sizeof(err))) { ask(false, err); return false; }
				if (ask(true, "Armed. Restart the console now? After it starts, wait 2 minutes on the XMB, then "
					      "open this app again to see the result.")) { op_reboot(); sleep(10); }
				return false;
			}
		}
	}
	return !want_exit && ask(true, "Show full status?\n\nThis reads the console's memory through Cobra (PS3MAPI) "
				       "and offers Switch to OpenPak / back to PSN.");
}

int main(int argc, char **argv)
{
	op_status s;
	char text[1400], why[256];
	bool on;

	(void)argc; (void)argv;
	sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_cb, NULL);
	if (!video_init()) sysProcessExit(1);

	/* v0.2.18: draw first, then only plain file reads until the user picks an action. Safe mode
	 * (L1 held at launch, or openpak_nohook present) never touches Cobra, lv2 peek or vsh memory. */
	{
		bool l1 = l1_held();
		op_quick q;
		op_quick_read(&q);
		if (l1 || q.nohook) { safe_mode(l1); goto out; }
	}
	if (!steps()) goto out;

	op_status_read(&s);
	build_status(&s, text, sizeof(text));
	if (!op_supported(&s, why, sizeof(why))) {
		strncat(text, "\n\n", sizeof(text) - strlen(text) - 1);
		strncat(text, why, sizeof(text) - strlen(text) - 1);
		ask(false, text);
		goto out;
	}

	/* Details page first: the full PS3MAPI line, every slot, boot_plugins.txt, the size check of each
	 * wrapper and (after "Load it now") Cobra's raw answers. Also written to /dev_hdd0/openpak/diag.txt. */
	if (s.installed || s.plugin_file) {
		ask_pages(s.detail);
		if (s.log[0]) ask_pages(s.log);
	}

	/* Installed but older or different: offer the update first (same backup, verify and rename rules
	 * as the first install), then fall through to "Switch back to PSN?" when declined. */
	if (s.update_needed) {
		char q[200], err[200], msg[700];
		snprintf(q, sizeof(q), "\n\nOpenPak update available (installed %s%s, this app v%s). Install it?",
			 s.inst_ver[0] ? "v" : "", s.inst_ver[0] ? s.inst_ver : "older version", OPENPAK_VERSION);
		strncat(text, q, sizeof(text) - strlen(text) - 1);
		if (ask(true, text)) {
			if (!op_switch_openpak(err, sizeof(err))) {
				snprintf(msg, sizeof(msg), "Failed: %s\n\nLog: " OPENPAK_LOG, err);
				ask(false, msg);
				goto out;
			}
			ask(false, "OpenPak setup updated.\n\nSafety switch: an empty file named openpak_nohook in "
				    "the root of a USB stick plugged in at power-on keeps the DNS catcher off. "
				    "The catcher installs only 45 s after the XMB starts.");
			if (ask(true, "Restart to apply?")) {
				op_reboot();
				sleep(10);
				goto out;
			}
			/* not restarting: fall through to "Load it now?" with the new files in place */
		}
		op_status_read(&s);
		build_status(&s, text, sizeof(text));
	}

	/* The plugin is installed and current but Cobra has not loaded it (an outdated install is updated first,
	 * because "Load it now" would only load the old files): offer to load it now, and show the raw
	 * return code of Cobra's load call (0 = loaded). */
	/* v0.2.18-bt: no "Load it now": Cobra's session load hangs this console; steps run at boot only. */

	on = op_is_openpak(&s) || s.installed;
	strncat(text, on ? "\n\nSwitch back to PSN?" : "\n\nSwitch to OpenPak?", sizeof(text) - strlen(text) - 1);
	if (ask(true, text)) {
		char err[200], msg[700];
		bool ok = on ? op_switch_psn(err, sizeof(err)) : op_switch_openpak(err, sizeof(err));
		if (!ok) {
			snprintf(msg, sizeof(msg), "Failed: %s\n\nLog: " OPENPAK_LOG, err);
			ask(false, msg);
		} else if (on) {
			ask(false, "PSN restored.\n\nRestart the console to put the original names back.");
			if (ask(true, "Restart the console now?")) { op_reboot(); sleep(10); }
		} else {
			ask(false, "OpenPak selected.\n\nRestart the console, then sign in under PlayStation "
				    "Network with your OpenPak email and password. Keep the network DNS on Automatic.");
			ask(false, "If the console freezes after a restart: turn it off, put an empty file named "
				    "openpak_nohook in the root of the USB stick, plug the stick in and turn the "
				    "console on. The DNS catcher then stays off (it installs only 45 s after the XMB "
				    "starts, and checks for that file first). Then open this app and choose "
				    "\"Switch back to PSN\" or install a fixed version.");
			if (ask(true, "Restart the console now?")) { op_reboot(); sleep(10); }
		}
	}
out:
	quit_clean();
	return 0;
}
