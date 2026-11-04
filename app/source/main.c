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

static void build_status(const op_status *s, char *out, size_t n)
{
	bool on = op_is_openpak(s);
	char plug[64];

	if (!s->plugin_listed) snprintf(plug, sizeof(plug), "not installed");
	else if (!s->plugin_ran) snprintf(plug, sizeof(plug), "installed, not run yet (restart)");
	else if (s->pokes_applied == s->pokes_total) snprintf(plug, sizeof(plug), "loaded, %d/%d patches", s->pokes_applied, s->pokes_total);
	else snprintf(plug, sizeof(plug), "loaded, only %d/%d patches", s->pokes_applied, s->pokes_total);

	snprintf(out, n,
		 "OpenPak PS3 Patcher %s\n\n"
		 "Network: %s\n"
		 "Firmware: %s%s\n"
		 "lv2 peek: %s\n"
		 "Cobra: %s\n"
		 "stage2: %s\n"
		 "HEN: %s\n"
		 "Plugin: %s\n"
		 "OpenPak CA: %s",
		 OPENPAK_VERSION,
		 on ? "OpenPak" : "PSN",
		 s->fw_known ? s->fw : "unknown", s->fw_ok ? " CEX" : "",
		 s->peek ? "yes" : "no",
		 s->cobra ? s->payload : "appears to be off",
		 s->stage2 ? "present" : s->stage2_off ? "renamed (disabled)" : "not found",
		 s->hen ? "yes" : "no",
		 plug,
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

int main(int argc, char **argv)
{
	op_status s;
	char text[1024], why[256];
	bool on;

	(void)argc; (void)argv;
	sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_cb, NULL);
	if (!video_init()) sysProcessExit(1);

	op_status_read(&s);
	build_status(&s, text, sizeof(text));
	if (!op_supported(&s, why, sizeof(why))) {
		strncat(text, "\n\n", sizeof(text) - strlen(text) - 1);
		strncat(text, why, sizeof(text) - strlen(text) - 1);
		ask(false, text);
		goto out;
	}

	on = op_is_openpak(&s);
	strncat(text, on ? "\n\nSwitch back to PSN?" : "\n\nSwitch to OpenPak?", sizeof(text) - strlen(text) - 1);
	if (ask(true, text)) {
		char err[200], msg[700];
		bool ok = on ? op_switch_psn(err, sizeof(err)) : op_switch_openpak(err, sizeof(err));
		if (!ok) {
			snprintf(msg, sizeof(msg), "Failed: %s\n\nLog: " OPENPAK_LOG, err);
			ask(false, msg);
		} else if (on) {
			ask(false, "PSN restored.\n\nSet DNS back to Automatic in Settings > Network Settings > "
				    "Internet Connection Settings > Custom, then restart.");
			if (ask(true, "Restart the console now?")) { op_reboot(); sleep(10); }
		} else {
			ask(false, "OpenPak selected.\n\nTemporary step (automatic redirection is coming): set the console DNS: Settings > Network Settings > "
				    "Internet Connection Settings > Custom. At DNS choose Manual, Primary DNS "
				    OPENPAK_DNS ", Secondary the same.\n\nThen restart, and sign in under "
				    "PlayStation Network with your OpenPak email and password.");
			if (ask(true, "Restart the console now?")) { op_reboot(); sleep(10); }
		}
	}
out:
	quit_clean();
	return 0;
}
