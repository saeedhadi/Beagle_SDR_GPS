/*
--------------------------------------------------------------------------------
This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.
This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.
You should have received a copy of the GNU Library General Public
License along with this library; if not, write to the
Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
Boston, MA  02110-1301, USA.
--------------------------------------------------------------------------------
*/

// Copyright (c) 2014-2016 John Seamons, ZL/KF6VO

#include "types.h"
#include "config.h"
#include "kiwi.h"
#include "misc.h"
#include "str.h"
#include "printf.h"
#include "timer.h"
#include "web.h"
#include "peri.h"
#include "spi.h"
#include "gps.h"
#include "cfg.h"
#include "dx.h"
#include "coroutines.h"
#include "data_pump.h"
#include "ext_int.h"
#include "net.h"

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sched.h>
#include <math.h>
#include <signal.h>
#include <fftw3.h>

// copy admin-related configuration from kiwi.json to new admin.json file
void cfg_adm_transition()
{
	int i;
	bool b;
	const char *s;

	s = cfg_string("user_password", NULL, CFG_REQUIRED);
	admcfg_set_string("user_password", s);
	cfg_string_free(s);
	b = cfg_bool("user_auto_login", NULL, CFG_REQUIRED);
	admcfg_set_bool("user_auto_login", b);
	s = cfg_string("admin_password", NULL, CFG_REQUIRED);
	admcfg_set_string("admin_password", s);
	cfg_string_free(s);
	b = cfg_bool("admin_auto_login", NULL, CFG_REQUIRED);
	admcfg_set_bool("admin_auto_login", b);
	
	i = cfg_int("port", NULL, CFG_REQUIRED);
	admcfg_set_int("port", i);
	
	b = cfg_bool("enable_gps", NULL, CFG_REQUIRED);
	admcfg_set_bool("enable_gps", b);

	b = cfg_bool("update_check", NULL, CFG_REQUIRED);
	admcfg_set_bool("update_check", b);
	b = cfg_bool("update_install", NULL, CFG_REQUIRED);
	admcfg_set_bool("update_install", b);
	
	b = cfg_bool("sdr_hu_register", NULL, CFG_REQUIRED);
	admcfg_set_bool("sdr_hu_register", b);
	s = cfg_string("api_key", NULL, CFG_REQUIRED);
	admcfg_set_string("api_key", s);
	cfg_string_free(s);


	// remove from kiwi.json file
	cfg_rem_string("user_password");
	cfg_rem_bool("user_auto_login");
	cfg_rem_string("admin_password");
	cfg_rem_bool("admin_auto_login");

	cfg_rem_int("port");

	cfg_rem_bool("enable_gps");

	cfg_rem_bool("update_check");
	cfg_rem_bool("update_install");

	cfg_rem_bool("sdr_hu_register");
	cfg_rem_string("api_key");


	// won't be present first time after upgrading from v1.2
	// first admin page connection will create
	if ((s = cfg_object("ip_address", NULL, CFG_OPTIONAL)) != NULL) {
		admcfg_set_object("ip_address", s);
		cfg_object_free(s);
		cfg_rem_object("ip_address");
	}


	// update JSON files
	admcfg_save_json(cfg_adm.json);
	cfg_save_json(cfg_cfg.json);
}

static char *cpu_stats_buf;
static volatile float audio_kbps, waterfall_kbps, waterfall_fps[RX_CHANS+1], http_kbps;
volatile int audio_bytes, waterfall_bytes, waterfall_frames[RX_CHANS+1], http_bytes;
char *current_authkey;
int debug_v;

//#define FORCE_ADMIN_PWD_CHECK

bool rx_common_cmd(const char *name, conn_t *conn, char *cmd)
{
	int i, j, n;
	struct mg_connection *mc = conn->mc;
	char *sb, *sb2;
	
	if (mc == NULL) return false;
	
	// SECURITY: auth command here is the only one allowed before auth check below
	if (strncmp(cmd, "SET auth", 8) == 0) {
		const char *pwd_s = NULL;
		int cfg_auto_login;
		char *type_m = NULL, *pwd_m = NULL;
		
		n = sscanf(cmd, "SET auth t=%ms p=%ms", &type_m, &pwd_m);
		//cprintf(conn, "n=%d typem=%s pwd=%s\n", n, type_m, pwd_m);
		if ((n != 1 && n != 2) || type_m == NULL) {
			send_msg(conn, false, "MSG badp=1");
			return true;
		}
		
		if (pwd_m != NULL) {
			str_decode_inplace(pwd_m);
			//printf("PWD %s pwd %d \"%s\" from %s\n", type_m, slen, pwd_m, mc->remote_ip);
		}
		
		bool allow = false;
		bool is_kiwi = (type_m != NULL && strcmp(type_m, "kiwi") == 0);
		bool is_admin = (type_m != NULL && strcmp(type_m, "admin") == 0);
		
		bool bad_type = (conn->type != STREAM_SOUND && conn->type != STREAM_WATERFALL && conn->type != STREAM_EXT &&
			conn->type != STREAM_ADMIN && conn->type != STREAM_MFG);
		
		if ((!is_kiwi && !is_admin) || bad_type) {
			clprintf(conn, "PWD BAD REQ type_m=\"%s\" conn_type=%d from %s\n", type_m, conn->type, mc->remote_ip);
			send_msg(conn, false, "MSG badp=1");
			return true;
		}
		
		bool log_auth_attempt = (conn->type == STREAM_ADMIN || conn->type == STREAM_MFG || (conn->type == STREAM_EXT && is_admin));
		bool is_local = isLocal_IP(conn, log_auth_attempt);

		#ifdef FORCE_ADMIN_PWD_CHECK
			is_local = false;
		#endif
		
		//cprintf(conn, "PWD %s log_auth_attempt %d conn_type %d [%s] is_local %d from %s\n",
		//	type_m, log_auth_attempt, conn->type, streams[conn->type].uri, is_local, mc->remote_ip);
		
		int chan_no_pwd = cfg_int("chan_no_pwd", NULL, CFG_REQUIRED);
		int chan_need_pwd = RX_CHANS - chan_no_pwd;

		if (is_kiwi) {
			pwd_s = admcfg_string("user_password", NULL, CFG_REQUIRED);
			cfg_auto_login = admcfg_bool("user_auto_login", NULL, CFG_REQUIRED);

			// if no user password set allow unrestricted connection
			if ((pwd_s == NULL || *pwd_s == '\0')) {
				cprintf(conn, "PWD kiwi: no config pwd set, allow any\n");
				allow = true;
			} else
			
			// config pwd set, but auto_login for local subnet is true
			if (cfg_auto_login && is_local) {
				cprintf(conn, "PWD kiwi: config pwd set, but is_local and auto-login set\n");
				allow = true;
			} else {
			
				int rx_free = rx_chan_free(NULL);
				
				// allow with no password if minimum number of channels needing password remains
				// if no password has been set at all we've already allowed access above
				if (rx_free >= chan_need_pwd) {
					allow = true;
					//cprintf(conn, "PWD rx_free=%d >= chan_need_pwd=%d %s\n", rx_free, chan_need_pwd, allow? "TRUE":"FALSE");
				}
			}
			
		} else
		if (is_admin) {
			pwd_s = admcfg_string("admin_password", NULL, CFG_REQUIRED);
			cfg_auto_login = admcfg_bool("admin_auto_login", NULL, CFG_REQUIRED);
			clprintf(conn, "PWD %s: config pwd set %s, auto-login %s\n", type_m,
				(pwd_s == NULL || *pwd_s == '\0')? "FALSE":"TRUE", cfg_auto_login? "TRUE":"FALSE");

			// no config pwd set (e.g. initial setup) -- allow if connection is from local network
			if ((pwd_s == NULL || *pwd_s == '\0') && is_local) {
				clprintf(conn, "PWD %s: no config pwd set, but is_local\n", type_m);
				allow = true;
			} else
			
			// config pwd set, but auto_login for local subnet is true
			if (cfg_auto_login && is_local) {
				clprintf(conn, "PWD %s: config pwd set, but is_local and auto-login set\n", type_m);
				allow = true;
			}
		} else {
			cprintf(conn, "PWD bad type=%s\n", type_m);
			pwd_s = NULL;
		}
		
		int badp = 1;
		
		// FIXME: remove at some point
		#ifndef FORCE_ADMIN_PWD_CHECK
			if (!allow && (strcmp(mc->remote_ip, "103.26.16.225") == 0 || strcmp(mc->remote_ip, "::ffff:103.26.16.225") == 0)) {
				allow = true;
			}
		#endif
		
		if (allow) {
			if (log_auth_attempt)
				clprintf(conn, "PWD %s allow override: sent from %s\n", type_m, mc->remote_ip);
			badp = 0;
		} else
		if ((pwd_s == NULL || *pwd_s == '\0')) {
			clprintf(conn, "PWD %s rejected: no config pwd set, sent from %s\n", type_m, mc->remote_ip);
			badp = 1;
		} else {
			if (pwd_m == NULL || pwd_s == NULL)
				badp = 1;
			else {
				//cprintf(conn, "PWD CMP %s pwd_s \"%s\" pwd_m \"%s\" from %s\n", type_m, pwd_s, pwd_m, mc->remote_ip);
				badp = strcasecmp(pwd_m, pwd_s);
			}
			//clprintf(conn, "PWD %s %s: sent from %s\n", type_m, badp? "rejected":"accepted", mc->remote_ip);
		}
		
		send_msg(conn, false, "MSG rx_chans=%d", RX_CHANS);
		send_msg(conn, false, "MSG chan_no_pwd=%d", chan_no_pwd);
		send_msg(conn, false, "MSG badp=%d", badp? 1:0);

		if (type_m) free(type_m);
		if (pwd_m) free(pwd_m);
		cfg_string_free(pwd_s);
		
		// only when the auth validates do we setup the handler
		if (badp == 0) {
			if (is_kiwi) conn->auth_kiwi = true;
			if (is_admin) conn->auth_admin = true;

			if (conn->auth == false) {
				conn->auth = true;
				conn->isLocal = is_local;
				
				// send cfg once to javascript
				if (conn->type == STREAM_SOUND || conn->type == STREAM_ADMIN || conn->type == STREAM_MFG)
					rx_server_send_config(conn);
				
				// setup stream task first time it's authenticated
				stream_t *st = &streams[conn->type];
				if (st->setup) (st->setup)((void *) conn);
			}
		}
		
		return true;
	}

	// SECURITY: we accept no incoming command besides auth above until auth is successful
	if (conn->auth == false) {
		clprintf(conn, "### SECURITY: NO AUTH YET: %s %d %s <%s>\n", name, conn->type, mc->remote_ip, cmd);
		return true;	// fake that we accepted command so it won't be further processed
	}

	if (strcmp(cmd, "SET get_authkey") == 0) {
		if (current_authkey)
			free(current_authkey);
		current_authkey = kiwi_authkey();
		send_msg(conn, false, "MSG authkey_cb=%s", current_authkey);
		return true;
	}

	if (strcmp(cmd, "SET keepalive") == 0) {
		conn->keepalive_count++;
		return true;
	}

	n = strncmp(cmd, "SET save_cfg=", 13);
	if (n == 0) {
		if (conn->type != STREAM_ADMIN) {
			lprintf("** attempt to save kiwi config from non-STREAM_ADMIN! IP %s\n", mc->remote_ip);
			return true;	// fake that we accepted command so it won't be further processed
		}
	
		char *json = cfg_realloc_json(strlen(cmd), CFG_NONE);	// a little bigger than necessary
		n = sscanf(cmd, "SET save_cfg=%s", json);
		assert(n == 1);
		//printf("SET save_cfg=...\n");
		str_decode_inplace(json);
		cfg_save_json(json);
		update_vars_from_config();		
		return true;
	}

	n = strncmp(cmd, "SET save_adm=", 13);
	if (n == 0) {
		if (conn->type != STREAM_ADMIN) {
			lprintf("** attempt to save admin config from non-STREAM_ADMIN!\n");
			return true;	// fake that we accepted command so it won't be further processed
		}
	
		char *json = admcfg_realloc_json(strlen(cmd), CFG_NONE);	// a little bigger than necessary
		n = sscanf(cmd, "SET save_adm=%s", json);
		assert(n == 1);
		//printf("SET save_adm=...\n");
		str_decode_inplace(json);
		admcfg_save_json(json);
		
		return true;
	}

	if (strcmp(cmd, "SET GET_USERS") == 0) {
		rx_chan_t *rx;
		bool need_comma = false;
		sb = kstr_cat(NULL, "[");
		bool isAdmin = (conn->type == STREAM_ADMIN);
		
		for (rx = rx_chan, i=0; rx < &rx_chan[RX_CHANS]; rx++, i++) {
			n = 0;
			if (rx->busy) {
				conn_t *c = rx->conn;
				if (c && c->valid && c->arrived && c->user != NULL) {
					assert(c->type == STREAM_SOUND);
					u4_t now = timer_sec();
					u4_t t = now - c->arrival;
					u4_t sec = t % 60; t /= 60;
					u4_t min = t % 60; t /= 60;
					u4_t hr = t;
					char *user = c->isUserIP? NULL : str_encode(c->user);
					char *geo = c->geo? str_encode(c->geo) : NULL;
					char *ext = ext_users[i].ext? str_encode((char *) ext_users[i].ext->name) : NULL;
					const char *ip = isAdmin? c->remote_ip : "";
					asprintf(&sb2, "%s{\"i\":%d,\"n\":\"%s\",\"g\":\"%s\",\"f\":%d,\"m\":\"%s\",\"z\":%d,\"t\":\"%d:%02d:%02d\",\"e\":\"%s\",\"a\":\"%s\"}",
						need_comma? ",":"", i, user? user:"", geo? geo:"", c->freqHz,
						enum2str(c->mode, mode_s, ARRAY_LEN(mode_s)), c->zoom, hr, min, sec, ext? ext:"", ip);
					if (user) free(user);
					if (geo) free(geo);
					if (ext) free(ext);
					n = 1;
				}
			}
			if (n == 0) {
				asprintf(&sb2, "%s{\"i\":%d}", need_comma? ",":"", i);
			}
			sb = kstr_cat(sb, kstr_wrap(sb2));
			need_comma = true;
		}

		sb = kstr_cat(sb, "]");
		send_msg(conn, false, "MSG user_cb=%s", kstr_sp(sb));
		kstr_free(sb);
		return true;
	}

#define DX_SPACING_ZOOM_THRESHOLD	5
#define DX_SPACING_THRESHOLD_PX		10
		dx_t *dp, *ldp, *upd;

	// SECURITY: should be okay: checks for conn->auth_admin first
	if (strncmp(cmd, "SET DX_UPD", 10) == 0) {
		if (conn->auth_admin == false) {
			cprintf(conn, "DX_UPD NO AUTH %s\n", conn->mc->remote_ip);
			return true;
		}
		
		if (dx.len == 0) {
			return true;
		}
		
		float freq;
		int gid, mkr_off, flags, new_len;
		flags = 0;
		char *text_m, *notes_m;
		text_m = notes_m = NULL;
		n = sscanf(cmd, "SET DX_UPD g=%d f=%f o=%d m=%d i=%ms n=%ms", &gid, &freq, &mkr_off, &flags, &text_m, &notes_m);
		//printf("DX_UPD #%d %8.2f 0x%x text=<%s> notes=<%s>\n", gid, freq, flags, text_m, notes_m);

		if (n != 2 && n != 6) {
			printf("DX_UPD n=%d\n", n);
			return true;
		}
		
		dx_t *dxp;
		if (gid >= -1 && gid < dx.len) {
			if (gid != -1 && freq == -1) {
				// delete entry by forcing to top of list, then decreasing size by one before save
				cprintf(conn, "DX_UPD %s delete entry #%d\n", conn->mc->remote_ip, gid);
				dxp = &dx.list[gid];
				dxp->freq = 999999;
				new_len = dx.len - 1;
			} else {
				if (gid == -1) {
					// new entry: add to end of list (in hidden slot), then sort will insert it properly
					cprintf(conn, "DX_UPD %s adding new entry\n", conn->mc->remote_ip);
					assert(dx.hidden_used == false);		// FIXME need better serialization
					dxp = &dx.list[dx.len];
					dx.hidden_used = true;
					dx.len++;
					new_len = dx.len;
				} else {
					// modify entry
					cprintf(conn, "DX_UPD %s modify entry #%d\n", conn->mc->remote_ip, gid);
					dxp = &dx.list[gid];
					new_len = dx.len;
				}
				dxp->freq = freq;
				dxp->offset = mkr_off;
				dxp->flags = flags;
				
				// remove trailing 'x' transmitted with text and notes fields
				text_m[strlen(text_m)-1] = '\0';
				notes_m[strlen(notes_m)-1] = '\0';
				
				// can't use kiwi_strdup because free() must be used later on
				dxp->ident = strdup(text_m);
				free(text_m);
				dxp->notes = strdup(notes_m);
				free(notes_m);
			}
		} else {
			printf("DX_UPD: gid %d >= dx.len %d ?\n", gid, dx.len);
		}
		
		qsort(dx.list, dx.len, sizeof(dx_t), qsort_floatcomp);
		//printf("DX_UPD after qsort dx.len %d new_len %d top elem f=%.2f\n",
		//	dx.len, new_len, dx.list[dx.len-1].freq);
		dx.len = new_len;
		dx_save_as_json();		// FIXME need better serialization
		dx_reload();
		send_msg(conn, false, "MSG request_dx_update");	// get client to request updated dx list
		return true;
	}

	if (strncmp(cmd, "SET MKR", 7) == 0) {
		float min, max;
		int zoom, width;
		n = sscanf(cmd, "SET MKR min=%f max=%f zoom=%d width=%d", &min, &max, &zoom, &width);
		if (n != 4) return true;
		float bw;
		bw = max - min;
		static bool first = true;
		static int dx_lastx;
		dx_lastx = 0;
		time_t t; time(&t);
		
		if (dx.len == 0) {
			return true;
		}
		
		asprintf(&sb, "[{\"t\":%ld}", t);		// reset appending
		sb = kstr_wrap(sb);

		for (dp = dx.list, i=j=0; i < dx.len; dp++, i++) {
			float freq = dp->freq + (dp->offset / 1000.0);		// carrier plus offset

			// when zoomed far-in need to look at wider window since we don't know PB center here
			#define DX_SEARCH_WINDOW 10.0
			if (freq < min - DX_SEARCH_WINDOW) continue;
			if (freq > max + DX_SEARCH_WINDOW) break;
			
			// reduce dx label clutter
			if (zoom <= DX_SPACING_ZOOM_THRESHOLD) {
				int x = ((dp->freq - min) / bw) * width;
				int diff = x - dx_lastx;
				//printf("DX spacing %d %d %d %s\n", dx_lastx, x, diff, dp->ident);
				if (!first && diff < DX_SPACING_THRESHOLD_PX) continue;
				dx_lastx = x;
				first = false;
			}
			
			// NB: ident and notes are already stored URL encoded
			float f = dp->freq + (dp->offset / 1000.0);
			asprintf(&sb2, ",{\"g\":%d,\"f\":%.3f,\"o\":%.0f,\"b\":%d,\"i\":\"%s\"%s%s%s}",
				i, freq, dp->offset, dp->flags, dp->ident,
				dp->notes? ",\"n\":\"":"", dp->notes? dp->notes:"", dp->notes? "\"":"");
			//printf("dx(%d,%.3f,%.0f,%d,\'%s\'%s%s%s)\n", i, f, dp->offset, dp->flags, dp->ident,
			//	dp->notes? ",\'":"", dp->notes? dp->notes:"", dp->notes? "\'":"");
			sb = kstr_cat(sb, kstr_wrap(sb2));
		}
		
		sb = kstr_cat(sb, "]");
		send_msg(conn, false, "MSG mkr=%s", kstr_sp(sb));
		kstr_free(sb);
		return true;
	}

	if (strcmp(cmd, "SET GET_CONFIG") == 0) {
		asprintf(&sb, "{\"r\":%d,\"g\":%d,\"s\":%d,\"pu\":\"%s\",\"po\":%d,\"pv\":\"%s\",\"n\":%d,\"m\":\"%s\",\"v1\":%d,\"v2\":%d}",
			RX_CHANS, GPS_CHANS, ddns.serno, ddns.ip_pub, ddns.port, ddns.ip_pvt, ddns.nm_bits, ddns.mac, VERSION_MAJ, VERSION_MIN);
		send_msg(conn, false, "MSG config_cb=%s", sb);
		free(sb);
		return true;
	}
	
	if (strncmp(cmd, "SET STATS_UPD", 13) == 0) {
		int ch;
		n = sscanf(cmd, "SET STATS_UPD ch=%d", &ch);
		if (n != 1 || ch < 0 || ch > RX_CHANS) return true;

		rx_chan_t *rx;
		int underruns = 0, seq_errors = 0;
		n = 0;
		//n = snprintf(oc, rem, "{\"a\":["); oc += n; rem -= n;
		
		for (rx = rx_chan, i=0; rx < &rx_chan[RX_CHANS]; rx++, i++) {
			if (rx->busy) {
				conn_t *c = rx->conn;
				if (c && c->valid && c->arrived && c->user != NULL) {
					underruns += c->audio_underrun;
					seq_errors += c->sequence_errors;
				}
			}
		}
		
		if (cpu_stats_buf != NULL) {
			asprintf(&sb, "{%s", cpu_stats_buf);
		} else {
			asprintf(&sb, "");
		}
		sb = kstr_wrap(sb);

		float sum_kbps = audio_kbps + waterfall_kbps + http_kbps;
		asprintf(&sb2, ",\"aa\":%.0f,\"aw\":%.0f,\"af\":%.0f,\"at\":%.0f,\"ah\":%.0f,\"as\":%.0f",
			audio_kbps, waterfall_kbps, waterfall_fps[ch], waterfall_fps[RX_CHANS], http_kbps, sum_kbps);
		sb = kstr_cat(sb, kstr_wrap(sb2));

		asprintf(&sb2, ",\"ga\":%d,\"gt\":%d,\"gg\":%d,\"gf\":%d,\"gc\":%.6f,\"go\":%d",
			gps.acquiring, gps.tracking, gps.good, gps.fixes, adc_clock/1000000, gps.adc_clk_corr);
		sb = kstr_cat(sb, kstr_wrap(sb2));

		extern int audio_dropped;
		asprintf(&sb2, ",\"ad\":%d,\"au\":%d,\"ae\":%d",
			audio_dropped, underruns, seq_errors);
		sb = kstr_cat(sb, kstr_wrap(sb2));

		char *s, utc_s[32], local_s[32];
		time_t utc; time(&utc);
		s = asctime(gmtime(&utc));
		strncpy(utc_s, &s[11], 5);
		utc_s[5] = '\0';
		if (utc_offset != -1 && dst_offset != -1) {
			time_t local = utc + utc_offset + dst_offset;
			s = asctime(gmtime(&local));
			strncpy(local_s, &s[11], 5);
			local_s[5] = '\0';
		} else {
			strcpy(local_s, "");
		}
		asprintf(&sb2, ",\"tu\":\"%s\",\"tl\":\"%s\",\"ti\":\"%s\",\"tn\":\"%s\"",
			utc_s, local_s, tzone_id, tzone_name);
		sb = kstr_cat(sb, kstr_wrap(sb2));

		asprintf(&sb2, "}");
		sb = kstr_cat(sb, kstr_wrap(sb2));

		send_msg(conn, false, "MSG stats_cb=%s", kstr_sp(sb));
		kstr_free(sb);
		return true;
	}

	n = strcmp(cmd, "SET gps_update");
	if (n == 0) {
		gps_stats_t::gps_chan_t *c;
		
		asprintf(&sb, "{\"FFTch\":%d,\"ch\":[", gps.FFTch);
		sb = kstr_wrap(sb);
		
		for (i=0; i < gps_chans; i++) {
			c = &gps.ch[i];
			int un = c->ca_unlocked;
			asprintf(&sb2, "%s{ \"ch\":%d,\"prn\":%d,\"snr\":%d,\"rssi\":%d,\"gain\":%d,\"hold\":%d,\"wdog\":%d"
				",\"unlock\":%d,\"parity\":%d,\"sub\":%d,\"sub_renew\":%d,\"novfl\":%d}",
				i? ", ":"", i, c->prn, c->snr, c->rssi, c->gain, c->hold, c->wdog,
				un, c->parity, c->sub, c->sub_renew, c->novfl);
			sb = kstr_cat(sb, kstr_wrap(sb2));
			c->parity = 0;
			for (j = 0; j < SUBFRAMES; j++) {
				if (c->sub_renew & (1<<j)) {
					c->sub |= 1<<j;
					c->sub_renew &= ~(1<<j);
				}
			}
		}

		UMS hms(gps.StatSec/60/60);
		
		unsigned r = (timer_ms() - gps.start)/1000;
		if (r >= 3600) {
			asprintf(&sb2, "],\"run\":\"%d:%02d:%02d\"", r / 3600, (r / 60) % 60, r % 60);
		} else {
			asprintf(&sb2, "],\"run\":\"%d:%02d\"", (r / 60) % 60, r % 60);
		}
		sb = kstr_cat(sb, kstr_wrap(sb2));

		if (gps.ttff) {
			asprintf(&sb2, ",\"ttff\":\"%d:%02d\"", gps.ttff / 60, gps.ttff % 60);
		} else {
			asprintf(&sb2, ",\"ttff\":null");
		}
		sb = kstr_cat(sb, kstr_wrap(sb2));

		if (gps.StatDay != -1) {
			asprintf(&sb2, ",\"gpstime\":\"%s %02d:%02d:%02.0f\"", Week[gps.StatDay], hms.u, hms.m, hms.s);
		} else {
			asprintf(&sb2, ",\"gpstime\":null");
		}
		sb = kstr_cat(sb, kstr_wrap(sb2));

		if (gps.tLS_valid) {
			asprintf(&sb2, ",\"utc_offset\":\"%+d sec\"", gps.delta_tLS);
		} else {
			asprintf(&sb2, ",\"utc_offset\":null");
		}
		sb = kstr_cat(sb, kstr_wrap(sb2));

		if (gps.StatLat) {
			asprintf(&sb2, ",\"lat\":\"%8.6f %c\"", gps.StatLat, gps.StatNS);
			sb = kstr_cat(sb, kstr_wrap(sb2));
			asprintf(&sb2, ",\"lon\":\"%8.6f %c\"", gps.StatLon, gps.StatEW);
			sb = kstr_cat(sb, kstr_wrap(sb2));
			asprintf(&sb2, ",\"alt\":\"%1.0f m\"", gps.StatAlt);
			sb = kstr_cat(sb, kstr_wrap(sb2));
			asprintf(&sb2, ",\"map\":\"<a href='http://wikimapia.org/#lang=en&lat=%8.6f&lon=%8.6f&z=18&m=b' target='_blank'>wikimapia.org</a>\"",
				gps.sgnLat, gps.sgnLon);
		} else {
			asprintf(&sb2, ",\"lat\":null");
		}
		sb = kstr_cat(sb, kstr_wrap(sb2));
			
		asprintf(&sb2, ",\"acq\":%d,\"track\":%d,\"good\":%d,\"fixes\":%d,\"adc_clk\":%.6f,\"adc_corr\":%d,\"srate\":%.6f}",
			gps.acquiring? 1:0, gps.tracking, gps.good, gps.fixes, (adc_clock - adc_clock_offset)/1e6, gps.adc_clk_corr, gps.srate);
		sb = kstr_cat(sb, kstr_wrap(sb2));

		send_msg_encoded_mc(conn->mc, "MSG", "gps_update_cb", "%s", kstr_sp(sb));
		kstr_free(sb);
		return true;
	}

	// SECURITY: FIXME: get rid of this?
	int wf_comp;
	n = sscanf(cmd, "SET wf_comp=%d", &wf_comp);
	if (n == 1) {
		c2s_waterfall_compression(conn->rx_channel, wf_comp? true:false);
		printf("### SET wf_comp=%d\n", wf_comp);
		return true;
	}

	// SECURITY: should be okay: checks for conn->auth_admin first
	double dc_off_I, dc_off_Q;
	n = sscanf(cmd, "SET DC_offset I=%lf Q=%lf", &dc_off_I, &dc_off_Q);
	if (n == 2) {
	#if 0
		if (conn->auth_admin == false) {
			lprintf("SET DC_offset: NO AUTH\n");
			return true;
		}
		
		DC_offset_I += dc_off_I;
		DC_offset_Q += dc_off_Q;
		printf("DC_offset: I %.4lg/%.4lg Q %.4lg/%.4lg\n", dc_off_I, DC_offset_I, dc_off_Q, DC_offset_Q);

		cfg_set_float("DC_offset_I", DC_offset_I);
		cfg_set_float("DC_offset_Q", DC_offset_Q);
		cfg_save_json(cfg_cfg.json);
		return true;
	#else
		// FIXME: Too many people are screwing themselves by pushing the button without understanding what it does
		// and then complaining that there is carrier leak in AM mode. So disable this for now.
		return true;
	#endif
	}
	
	// SECURITY: only used during debugging
	n = sscanf(cmd, "SET debug_v=%d", &i);
	if (n == 1) {
		debug_v = i;
		printf("SET debug_v=%d\n", debug_v);
		return true;
	}

	if (strncmp(cmd, "SERVER DE CLIENT", 16) == 0) return true;
	
	// we see these sometimes; not part of our protocol
	if (strcmp(cmd, "PING") == 0) return true;

	// we see these at the close of a connection; not part of our protocol
	if (strcmp(cmd, "?") == 0) return true;

	return false;
}

int inactivity_timeout_mins;
int S_meter_cal;
double ui_srate;

#define DC_OFFSET_DEFAULT 0.05
double DC_offset_I, DC_offset_Q;

#define WATERFALL_CALIBRATION_DEFAULT -13
#define SMETER_CALIBRATION_DEFAULT -13

void update_vars_from_config()
{
	bool error, update_cfg = false;

	// C copies of vars that must be updated when configuration loaded or saved from file
	// or configuration parameters that must exist for client connections
	// (i.e. have default values assigned).

	inactivity_timeout_mins = cfg_int("inactivity_timeout_mins", &error, CFG_OPTIONAL);
	if (error) {
		cfg_set_int("inactivity_timeout_mins", 0);
		inactivity_timeout_mins = 0;
		update_cfg = true;
	}

	ui_srate = cfg_int("max_freq", &error, CFG_OPTIONAL)? 32*MHz : 30*MHz;
	if (error) {
		cfg_set_int("max_freq", 0);
		ui_srate = 30*MHz;
		update_cfg = true;
	}

	DC_offset_I = cfg_float("DC_offset_I", &error, CFG_OPTIONAL);
	if (error) {
		DC_offset_I = DC_OFFSET_DEFAULT;
		cfg_set_float("DC_offset_I", DC_offset_I);
		update_cfg = true;
	}

	DC_offset_Q = cfg_float("DC_offset_Q", &error, CFG_OPTIONAL);
	if (error) {
		DC_offset_Q = DC_OFFSET_DEFAULT;
		cfg_set_float("DC_offset_Q", DC_offset_Q);
		update_cfg = true;
	}
	printf("INIT DC_offset: I %.4lg Q %.4lg\n", DC_offset_I, DC_offset_Q);

	S_meter_cal = cfg_int("S_meter_cal", &error, CFG_OPTIONAL);
	if (error) {
		cfg_set_int("S_meter_cal", SMETER_CALIBRATION_DEFAULT);
		update_cfg = true;
	}

	cfg_int("waterfall_cal", &error, CFG_OPTIONAL);
	if (error) {
		cfg_set_int("waterfall_cal", WATERFALL_CALIBRATION_DEFAULT);
		update_cfg = true;
	}

	cfg_bool("contact_admin", &error, CFG_OPTIONAL);
	if (error) {
		cfg_set_bool("contact_admin", true);
		update_cfg = true;
	}

	cfg_int("chan_no_pwd", &error, CFG_OPTIONAL);
	if (error) {
		cfg_set_int("chan_no_pwd", 0);
		update_cfg = true;
	}

	const char *s = cfg_string("owner_info", &error, CFG_OPTIONAL);
	if (error) {
		cfg_set_string("owner_info", "");
		update_cfg = true;
	} else {
		cfg_string_free(s);
	}

	if (update_cfg)
		cfg_save_json(cfg_cfg.json);


	// same, but for admin config
	
	bool update_admcfg = false;
	
	admcfg_bool("server_enabled", &error, CFG_OPTIONAL);
	if (error) {
		admcfg_set_bool("server_enabled", true);
		update_admcfg = true;
	}

	admcfg_bool("auto_add_nat", &error, CFG_OPTIONAL);
	if (error) {
		admcfg_set_bool("auto_add_nat", false);
		update_admcfg = true;
	}

	if (update_admcfg)
		admcfg_save_json(cfg_adm.json);
}

int current_nusers;
static int last_hour = -1, last_min = -1;

// called periodically
void webserver_collect_print_stats(int print)
{
	int i, nusers=0;
	conn_t *c;
	
	// print / log connections
	for (c=conns; c < &conns[N_CONNS]; c++) {
		if (!(c->valid && c->type == STREAM_SOUND && c->arrived)) continue;
		
		u4_t now = timer_sec();
		if (c->freqHz != c->last_freqHz || c->mode != c->last_mode || c->zoom != c->last_zoom) {
			if (print) loguser(c, LOG_UPDATE);
			c->last_tune_time = now;
		} else {
			u4_t diff = now - c->last_log_time;
			if (diff > MINUTES_TO_SEC(5)) {
				if (print) loguser(c, LOG_UPDATE_NC);
			}
			
			if (!c->inactivity_timeout_override && (inactivity_timeout_mins != 0) && !c->isLocal) {
				diff = now - c->last_tune_time;
				if (diff > MINUTES_TO_SEC(inactivity_timeout_mins) && !c->inactivity_msg_sent) {
					send_msg(c, SM_NO_DEBUG, "MSG inactivity_timeout_msg=%d", inactivity_timeout_mins);
					c->inactivity_msg_sent = true;
				}
				if (diff > (MINUTES_TO_SEC(inactivity_timeout_mins) + INACTIVITY_WARNING_SECS)) {
					c->inactivity_timeout = true;
				}
			}
		}
		nusers++;
	}
	current_nusers = nusers;

	// construct cpu stats response
	int n, user, sys, idle;
	static int last_user, last_sys, last_idle;
	user = sys = 0;
	u4_t now = timer_ms();
	static u4_t last_now;
	float secs = (float)(now - last_now) / 1000;
	last_now = now;
	
	float del_user = 0;
	float del_sys = 0;
	float del_idle = 0;
	
	char buf[256];
	n = non_blocking_cmd("cat /proc/stat", buf, sizeof(buf), NULL);
	if (n > 0) {
		n = sscanf(buf, "cpu %d %*d %d %d", &user, &sys, &idle);
		//long clk_tick = sysconf(_SC_CLK_TCK);
		del_user = (float)(user - last_user) / secs;
		del_sys = (float)(sys - last_sys) / secs;
		del_idle = (float)(idle - last_idle) / secs;
		//printf("CPU %.1fs u=%.1f%% s=%.1f%% i=%.1f%%\n", secs, del_user, del_sys, del_idle);
		
		// ecpu_use() below can thread block, so cpu_stats_buf must be properly set NULL for reading thread
		if (cpu_stats_buf) {
			char *s = cpu_stats_buf;
			cpu_stats_buf = NULL;
			free(s);
		}
		asprintf(&cpu_stats_buf, "\"ct\":%d,\"cu\":%.0f,\"cs\":%.0f,\"ci\":%.0f,\"ce\":%.0f",
			timer_sec(), del_user, del_sys, del_idle, ecpu_use());
		last_user = user;
		last_sys = sys;
		last_idle = idle;
	}

	// collect network i/o stats
	static const float k = 1.0/1000.0/10.0;		// kbytes/sec every 10 secs
	audio_kbps = audio_bytes*k;
	waterfall_kbps = waterfall_bytes*k;
	
	for (i=0; i <= RX_CHANS; i++) {
		waterfall_fps[i] = waterfall_frames[i]/10.0;
		waterfall_frames[i] = 0;
	}
	http_kbps = http_bytes*k;
	audio_bytes = waterfall_bytes = http_bytes = 0;

	// on the hour: report number of connected users & schedule updates
	time_t t;
	time(&t);
	struct tm tm;
	localtime_r(&t, &tm);
	
	if (tm.tm_hour != last_hour) {
		if (print) lprintf("(%d %s)\n", nusers, (nusers==1)? "user":"users");
		last_hour = tm.tm_hour;
	}

	if (tm.tm_min != last_min) {
		schedule_update(tm.tm_hour, tm.tm_min);
		last_min = tm.tm_min;
	}
}
