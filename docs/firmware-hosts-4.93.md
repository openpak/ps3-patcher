# PS3 firmware 4.93 CEX: network host inventory

Card openpak/prds#125. Generated 2026-11-06. Everything below is read from the firmware images and from
read-only probes of OpenPak DNS (UDP 145.241.199.19:53) and HTTP(S) on 145.241.199.19. Nothing was changed.

## 1. Method

Sources:

- `scratch/pier/decrypted/{libs,np,proc,vshmod,vsh-onboarding}`: 255 decrypted `.prx` modules (libhttp, libssl,
  libnet, libnetctl, all NP libs, every vsh plugin: store/explore, video, music, photo, remote play, PSP/PS2
  activation, Blu-ray, silk browser, DLNA, print, MTP...). `strings -a -n 4` over every module, grepped for
  `playstation.{net,org,com}`, `sonyentertainmentnetwork.com`, `sony.`, `scea|scee|scej`, `ndmdhs`, `dnas`,
  every `http(s)://`, and every dotted host-like token.
- `scratch/pier/analysis/vsh/vsh.elf` (decrypted vsh.self 4.93, 7.3 MB): same strings pass. The PS3 vsh is a
  decrypted ELF, so its data segment was read by virtual address (the addresses in `app/source/vshnames.h`).
- `scratch/pier/ghidra/out/*.c` (decompiled vsh.elf, nas_plugin, npsignin_plugin, np_oauth, regcam_plugin,
  libsysutil_np_commerce2, netconf_plugin ...): used to resolve names built from parts. Examples:
  `"%s.%s.%s"` + `"native-ps3"` + env + `"ac.playstation.net"`; `"%s.%s.%s.communication.playstation.net"`;
  `"%s.%s.stun.playstation.net"` (default prefix `us`, default env `np`, read from vsh data at 0x684610/0x684628);
  `"a0.ww." + env + ".dl.playstation.net"`; `"trophy01." + env + ".community.playstation.net"`;
  `"http://npsns." + env + ".dl.playstation.net/npsns/"`; the 12 update-list URLs `f<cc>01.ps3.update.playstation.net`
  (au br cn eu jp kr mx ru sa tw uk us).
- Environment values found in the firmware (the `%s` after the service label): `np` (production), `prod-qa`,
  `sp-int`, `rc`, `e1-np`, `mgmt`. Production consoles use `np`. Region/country labels seen at runtime: `ww`,
  `us`, `pt`, `ie`, and `np` again (`getprof.np.np...`).
- Runtime evidence: every `DnsHook: DNS query for <name>` in `scratch/pier/logs/*.log` (Pier). Rows marked
  "seen in Pier" appeared there (query counts in parentheses).
- Prior notes cross-checked: `research/ps3/psn-name-redirect-evidence.md`, `names/psn-names.txt`, `vshnames.h`.
- Status probes: DNS with a raw UDP client; HTTP with `curl -sk --resolve <name>:<port>:145.241.199.19`, and the
  np-account/np-community README route tables. "404" from the box means the request reached np-account (the
  family routes there) but no route matched the probe path.

Not available: **no decrypted lv2_kernel / LV1 image exists** under `scratch/pier` or `scratch/ps3*`
(`scratch/ps3-research/lv2.ld` and `mamba/lv2` are Mamba source, not firmware). `vsh.self` and the
`.sprx` files under `~/.config/pier/dev_flash` are encrypted; only the decrypted copies above were read. Kernel
strings could therefore not be inspected. The webMAN patterns in `psn-name-redirect-evidence.md` (LV1 `s://auth`,
LV2 `.ps3.upd`, `ena.net.`, `p.stun.p`...) are copies of vsh data seen through physical memory, not kernel hosts.

`<env>` = np | prod-qa | sp-int | rc | e1-np | mgmt (np in production). `<cc>` = country label.

## 2. Table

Status: `serves` = an OpenPak service answers real requests; `stub` = answers but with canned/empty data;
`gap` = DNS resolves to OpenPak but no route/service answers; `not needed` = not Sony/PSN or not game-relevant,
must not be redirected. Family = the wildcard that covers it.

### 2.1 Sign-in, account, store (`*.ac.playstation.net`)

| Name / pattern | Modules | For | Status | Family |
| --- | --- | --- | --- | --- |
| `native-ps3.<env>.ac.playstation.net` /native/{reg,cam,store,upgrade,pwd}/*.action (seen in Pier, 39) | vsh, npsignin, nas, regcam, explore, np_commerce2, campaign, autodownload, newstore ... (11) | native sign-in, account binding, Account Information | serves (save actions answer 503) | ac.playstation.net |
| `auth.<env>.ac.playstation.net/nav/auth` | vsh | legacy ticket issue | serves | ac |
| `<TITLEID>.auth.<env>.ac.playstation.net/nav/auth` via `%s.auth.%s.ac...` (seen in Pier: NPXS01001, NPEB01270) | vsh | per-title ticket request | serves | ac |
| `auth.api.<env>.ac.playstation.net` /2.0/oauth/* | np_oauth | OAuth tokens | serves | ac |
| `account.<env>.ac.playstation.net` /register/endSession, /manage/endSession | nas_plugin, vsh | account web session end | gap | ac |
| `account.<env>.ac.playstation.net/ecomm/ingame/{start,finish,set}DownloadDRM, {start,finish,rebuffer}StreamingDRM` | nas, download, autodownload, esehttp, newstore, podl, videoplayer, videoeditor ... | purchased-content DRM licence | gap | ac |
| `commerce.<env>.ac.playstation.net` /ingame/endSession.action, /cap.m, /cdp.m, /kdp.m | nas, mtpinitiator, vsh | commerce session, content keys | gap | ac |
| `store.<env>.ac.playstation.net/endSession.action` | nas_plugin | store session end | gap | ac |
| `mds.<env>.ac.playstation.net/<path>` | nas, msmw2, mtpinitiator | media/licence delivery (Marlin) | gap | ac |
| `<x>.<env>.ac.playstation.net/sony/mmr` | x3_mdimp11 | media metadata | gap | ac |
| `native.q-np.ac.playstation.net/images/examples/*.png` | regcam (card images, static text) | not a connection target | not needed | ac |

### 2.2 Community, profile, trophies, presence (`*.community`, `*.communication`, `trophy`, `stun`)

| Name / pattern | Modules | For | Status | Family |
| --- | --- | --- | --- | --- |
| `csla.<env>.community.playstation.net` /csla/*/console/lwConsoleToken | np_oauth | console token | serves | community.playstation.net |
| `getprof.<cc>.<env>.community...` (np.np 258, pt 50, ie 33, us 9) /basic_view/* | autodownload, vsh, profile, explore | profile read | serves | community |
| `updptl.<cc>.<env>.community...` (pt, ie, us) /basic_update/sec/update_ptl | profile plugins | profile update | serves | community |
| `profile.<...>.community...` | autodownload | profile write | serves | community |
| `trophy01.<env>.community.../trophy/` | np_trophy, profile, friendtrophy, explore, autodownload | trophy list/sync | serves (np-trophy) | community |
| `trophy.ww.np.community.playstation.net` (Pier, 46) | libsysutil_np_trophy, vsh | trophy registration | serves (np-trophy; verify path) | community |
| `avatar.np.community...` /a/{cap} (Pier, 109) | vsh, explore | avatar images | serves | community |
| `static-resource.<env>.community...` /np/resource/title/* (Pier, 114) | regcam, newstore, vsh | title rating/resources | serves | community |
| `matching.np.community...` /np/resource/title/{id}/matching/* (Pier, 18) | libsysutil_np2 | Matching2 discovery | serves (np-community, maintenance unless agent set) | community |
| `ranking-record.np.community...` (Pier, 2) | libsysutil_np | score upload | serves when NP_SCORE_URL set (np-score) | community |
| `sclock.ww.np.community.playstation.net/scs/get` | mtpinitiator_plugin | server clock | gap | community |
| `<svc>.<cc>.<env>.communication.playstation.net` (Pier: `pt.pt.np`, `ie.ie.np`, `us.us.np`) XMPP | vsh | presence, friends, messages | serves (np-community XMPP; TCP 5223 open, 5222 closed) | communication.playstation.net (under playstation.net) |
| `<x>.<env>.stun.playstation.net` (Pier `us.np.stun`, 145) | vsh | NAT type / STUN | gap (UDP 3478 not probed; TCP closed; see connectivity-stun notes) | playstation.net |
| `trophy.playstation.net` /entry (https) | np_trophy, profile, explore ... (13) | Facebook-style trophy share endpoint | gap | playstation.net |
| `%s@%s.%s.%s.playstation.net`, `d0.mail.playstation.net` | newstore, eula_net, regcam | JID / mail address format | not needed (identifier, not dialled) | playstation.net |

### 2.3 Updates, network test, downloads (`*.update`, `*.net`, `*.dl`)

| Name / pattern | Modules | For | Status | Family |
| --- | --- | --- | --- | --- |
| `f{au,br,cn,eu,jp,kr,mx,ru,sa,tw,uk,us}01.ps3.update.playstation.net/update/ps3/list/<cc>/ps3-updatelist.txt` (Pier `feu01`, 132) | vsh | firmware update list | serves (200) | playstation.net |
| `fj00.ps3.update.playstation.org/networktest/trial.txt` | netconf_plugin | connection-test | serves (200) | playstation.org |
| `get.net.playstation.net/networktest/get_2m`, `post.net.playstation.net/networktest/post{,_128}` | vsh, libsysutil_np_util | bandwidth test | serves | playstation.net |
| `ena.net.playstation.net` (Pier, 109) | vsh (libupdown) | firmware/system download | gap (404) | playstation.net |
| `a0.ww.<env>.dl.playstation.net` /tpl/*-ver.xml, /tss/, /user_storage/ (Pier, 47) | vsh | title patch list, TSS, user storage | serves via np-update/np-tus when configured (404 on unknown path) | dl.playstation.net |
| `npsns.<env>.dl.playstation.net/npsns/` | vsh | SNS feed | gap | dl |
| `wboard.ww.dl.playstation.net/download/wboard` | vsh | whiteboard config | gap | dl |
| `nsx.sec.<env>.dl.playstation.net/nsx/sec/...` (Pier, 42) | explore, regcam, newstore, eula_net | XMB What's New / billboard XML | serves (200) | dl |
| `nsx-e.<env>.dl.playstation.net/nsx/log`, `nsx-e.sec.<env>.dl...` (Pier, 54) | 30+ plugins | telemetry log pings | gap (harmless 404) | dl |
| `xmb-e.dl.playstation.net/xmb/l` | explore, categories | XMB layout | gap | dl |
| `tmdb.<env>.dl.playstation.net/tmdb/*.xml` (Pier, 22) | explore_plugin | title metadata | gap | dl |
| `video.dl.playstation.net/cdn/video/` (Pier, 24); `comic.dl` (Pier, 2); `<x>.dl.../cdn/<id>/g` | explore, newstore | store media | gap | dl |
| `legaldoc.dl.playstation.net/ps3-eula/psn/` | regcam, eula_net, newstore | EULA / ToS text | gap | dl |
| `hakoniwa.dl...`, `videoeditor.dl...`, `apollo.dl...`, `netflix.dl...`, `hfs-pac.e1-np.dl...` | hknw, x3_mdimp7, videoeditor, newstore, explore, download_plugin | VSH module pkgs, service conf, Netflix pkg | gap (not needed to play) | dl |
| `<x>.dl.playstation.net/download/...`, `.dl.playstation.net` generic (172 strings, 29 modules) | all explore_category_* | content URLs built from server data | gap | dl |

### 2.4 Other Sony hosts

| Name / pattern | Modules | For | Status | Family |
| --- | --- | --- | --- | --- |
| `select.ww.home.playstation.net/home/{ww,xx}/country-select.xml` | explore/home | PlayStation Home | not needed | playstation.net |
| `manuals.playstation.net/document/<lang>/ps3/current/...` | explore, hknw, videoeditor | online manual | not needed | playstation.net |
| `pspgate1.dnas.playstation.org/activation/{atrac,wma,dtcpip}` | sysconf, dtcpip_util | DNAS media activation | not needed | playstation.org |
| `secure.eu.playstation.com`, `webtrends.eu.playstation.com`, `www.playstation.com` (Pier, 1) | nas_plugin (allow-list of browser hosts) | in-app web pages | not needed | playstation.com |
| `sf.{prod,mgmt,e1-np,<env>}.sonyentertainmentnetwork.com`, `d1-np.swordfish.store.sonyentertainmentnetwork.com` | explore, autodownload, remotedownload | store front / remote download (PS Store web) | gap (DNS only, not routed to a store) | sonyentertainmentnetwork.com |
| `www.sonyentertainmentnetwork.com/video-unlimited` | newstore | link text | not needed | sonyentertainmentnetwork.com |
| `<cc>.<env>.adproxy.ndmdhs.com/adproxy/getmanifest` (Pier `pt.np`, 78) | explore + 12 categories | XMB ad manifest (third-party domain; Sony ad broker) | not needed (NXDOMAIN on OpenPak DNS; failure is tolerated, add `*.ndmdhs.com` only if XMB ad errors matter) | ndmdhs.com |
| `http://playstation.net/np/rt/...` | vsh | XML namespace URIs, not hosts | not needed | none |

### 2.5 Third parties (do NOT redirect)

| Name | Modules | For | Status |
| --- | --- | --- | --- |
| `www.facebook.com`, `api.facebook.com`, `api-video.facebook.com` | profile, trophy, eula_net, photoupload, videoeditor | Facebook sharing | not needed |
| `gdata.youtube.com`, `uploads.gdata.youtube.com`, `www.youtube.com`, `www.google.com`, `picasaweb.google.com`, `mail.google.com` | upload_util, videoeditor, x3_mdimp7, silk_webkit | YouTube/Picasa upload | not needed |
| `dmr.allmusic.com`, `dmrdev.allmusic.com` | x3_amgsdk | music metadata (AMG; no Gracenote host in this firmware) | not needed |
| `boxreg.trendmicro.com`, `webclassify.trendmicro.com`, `ps301.url.trendmicro.com`, `webprotection.trendmicro.com`, `*.trendsecure.com`, `sv/sec.ifilter.jp` | silk_wk, silk_nas | browser URL filtering | not needed |
| `*.macromedia.com`, `settingsmanager.adobe.com`, `fpdownload*.macromedia.com` | faust_widget, silk_npflashplayer | Flash player | not needed |
| `crl.marlin-tmo.com`, `nemo.intertrust.com`, `www.octopus-drm.com` | msmw2 | Marlin DRM | not needed |
| `www.cyberedy.bitwallet.co.jp` | edy_plugin | Edy payments | not needed |
| `www.yahoo.co.jp`, `xsushi.ddo.jp:11080` (developer test URLs) | avc_plugin, newstore | test strings | not needed |
| `*.dlna.org`, `*.dtcp.com`, `www.canon.com/ns/*`, `http://%s:%d` | mintx, print_* | DLNA/print protocol header names and LAN peers | not needed |

Distinct names/patterns in the tables: 74 rows, covering about 120 concrete host spellings (region/env
variants collapsed).

## 3. Wildcard families the ps3 profile needs

Only Sony/PSN. Evidence = number of firmware strings / modules (vsh.elf + 255 modules), plus Pier runtime hits.

| Family | Evidence | Notes |
| --- | --- | --- |
| `*.playstation.net` | 379 strings in 58 modules, 76 in vsh.elf; Pier: all runtime lookups | Needed. Contains the sub-families below, so one wildcard covers ac/community/communication/dl/update/net/stun/home/mail. |
| `*.ac.playstation.net` | 134 strings / 35 modules | sign-in, account, store, commerce |
| `*.community.playstation.net` | 23 / 20 | profile, trophy, avatar, csla, matching, clock |
| `*.communication.playstation.net` | 2 (vsh) | XMPP presence |
| `*.dl.playstation.net` | 172 / 29 | nsx, tpl, tss, tmdb, store media |
| `*.update.playstation.net`, `*.net.playstation.net` | 12 (vsh) / 3 | update lists, network test, ena |
| `stun.playstation.net` | 2 (vsh); Pier 145 | NAT |
| `*.playstation.org` | 9 / 5 modules | `fj00.ps3.update`, `pspgate1.dnas` (DNAS); only the update/test host is useful |
| `*.playstation.com` | 6 / 3 modules | browser allow-list pages only; optional |
| `*.sonyentertainmentnetwork.com` | 21 / 5 modules | store/remote-download web fronts only; optional, not needed to sign in or play |
| `*.ndmdhs.com` | 13 / 13 modules; Pier 78 | XMB ads. Third-party domain; not redirected today, optional |

`*.sony.com` / `*.sony.net` / `scea|scee|scej` never appear as hosts (`scea.xml` etc. are file names;
`sony.net:1.0` is a DRM identifier). No game-server families exist in the firmware itself (game hosts belong to
the titles, not the OS).

OpenPak DNS already answers 145.241.199.19 for every playstation.net/.org/.com and sonyentertainmentnetwork.com
name probed (about 50), but returns NXDOMAIN for `*.ndmdhs.com` and `dmr.allmusic.com`, and forwards
facebook/google/trendmicro/macromedia unchanged (correct).

## 4. Gaps

DNS resolves to OpenPak, but no OpenPak route/service answers (404 or no listener):

1. Commerce/store: `account|commerce|store|mds.<env>.ac.playstation.net` (endSession, ecomm/ingame DRM,
   cap.m/cdp.m/kdp.m), `sf.*.sonyentertainmentnetwork.com`, `d1-np.swordfish.store.*`.
2. `sclock.ww.np.community.playstation.net/scs/get` (only mtpinitiator uses it).
3. `trophy.playstation.net/entry` (https share endpoint).
4. `ena.net.playstation.net` (Pier 109 hits: firmware download, 404 today).
5. `stun.playstation.net` (`us.np.stun...`, 145 Pier hits): TCP closed, UDP 3478 not verified here.
6. dl content: `legaldoc`, `xmb-e`, `tmdb.<env>`, `video`, `comic`, `nsx-e` logs, `npsns.<env>`, `wboard.ww`,
   `hakoniwa`, `videoeditor`, `apollo`, `netflix`, `hfs-pac.e1-np`; `a0.ww.<env>.dl` serves only if np-update /
   np-tus are configured (probe returned 404).
7. `select.ww.home...` (Home), `manuals.playstation.net`, `pspgate1.dnas.playstation.org` (not needed for play).
8. DNS does not answer at all: `*.ndmdhs.com` (NXDOMAIN, 78 Pier hits `pt.np.adproxy`).
9. `ranking-record.np.community...`, `matching.np.community...`: only work if `NP_SCORE_URL` / `NP_MATCHING_AGENT`
   are set in prod (not checked, prod box not touched).

Unresolved: kernel (lv2/LV1) strings; whether the `getprof.np.np` form means env `np` plus country `np`
(vsh builds it from NP settings); the real role of `ena.net` beyond the `Dest=`/`ImageVer` strings; the UDP
behavior of stun; whether `ndmdhs.com` is Sony-owned (it is only the XMB ad broker; outcome is cosmetic).
