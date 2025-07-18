/*
 * Copyright (C) Internet Systems Consortium, Inc. ("ISC")
 *
 * SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * See the COPYRIGHT file distributed with this work for additional
 * information regarding copyright ownership.
 */

#pragma once

/*! \file */

#include <inttypes.h>
#include <stdbool.h>

#include <isc/log.h>
#include <isc/magic.h>
#include <isc/quota.h>
#include <isc/signal.h>
#include <isc/sockaddr.h>
#include <isc/tls.h>
#include <isc/types.h>

#include <dns/acl.h>
#include <dns/dnstap.h>
#include <dns/stats.h>
#include <dns/types.h>

#include <ns/interfacemgr.h>
#include <ns/server.h>
#include <ns/stats.h>
#include <ns/types.h>

#include <named/types.h>

/*%
 * Name server state.  Better here than in lots of separate global variables.
 */
struct named_server {
	unsigned int magic;
	isc_mem_t   *mctx;

	ns_server_t *sctx;

	char *statsfile;    /*%< Statistics file name */
	char *dumpfile;	    /*%< Dump file name */
	char *secrootsfile; /*%< Secroots file name */
	char *bindkeysfile; /*%< bind.keys file name */
	char *recfile;	    /*%< Recursive file name */
	bool  version_set;  /*%< User has set version */
	char *version;	    /*%< User-specified version */
	bool  hostname_set; /*%< User has set hostname */
	char *hostname;	    /*%< User-specified hostname */

	/* Server data structures. */
	dns_loadmgr_t	  *loadmgr;
	dns_zonemgr_t	  *zonemgr;
	dns_viewlist_t	   viewlist;
	dns_kasplist_t	   kasplist;
	dns_keystorelist_t keystorelist;
	ns_interfacemgr_t *interfacemgr;
	dns_db_t	  *in_roothints;

	isc_timer_t *interface_timer;
	isc_timer_t *heartbeat_timer;
	isc_timer_t *pps_timer;
	isc_timer_t *tat_timer;

	uint32_t interface_interval;

	atomic_int reload_status;

	bool flushonshutdown;

	named_cachelist_t cachelist; /*%< Possibly shared caches
				      * */
	isc_stats_t *zonestats;	     /*% Zone management stats */
	isc_stats_t *resolverstats;  /*% Resolver stats */
	isc_stats_t *sockstats;	     /*%< Socket stats */

	named_controls_t    *controls; /*%< Control channels */
	unsigned int	     dispatchgen;
	named_dispatchlist_t dispatches;

	named_statschannellist_t statschannels;

	dst_key_t     *sessionkey;
	char	      *session_keyfile;
	dns_name_t    *session_keyname;
	unsigned int   session_keyalg;
	uint16_t       session_keybits;
	bool	       interface_auto;
	unsigned char  secret[32]; /*%< Server Cookie Secret */
	ns_cookiealg_t cookiealg;

	dns_dtenv_t *dtenv; /*%< Dnstap environment */

	isc_tlsctx_cache_t *tlsctx_server_cache;
	isc_tlsctx_cache_t *tlsctx_client_cache;

	isc_signal_t *sighup;
	isc_signal_t *sigusr1;
};

#define NAMED_SERVER_MAGIC    ISC_MAGIC('S', 'V', 'E', 'R')
#define NAMED_SERVER_VALID(s) ISC_MAGIC_VALID(s, NAMED_SERVER_MAGIC)

void
named_server_create(isc_mem_t *mctx, named_server_t **serverp);
/*%<
 * Create a server object with default settings.
 * This function either succeeds or causes the program to exit
 * with a fatal error.
 */

void
named_server_destroy(named_server_t **serverp);
/*%<
 * Destroy a server object, freeing its memory.
 */

void
named_server_reloadwanted(void *arg, int signum);
/*%<
 * Inform a server that a reload is wanted.  This function
 * may be called asynchronously, from outside the server's task.
 * If a reload is already scheduled or in progress, the call
 * is ignored.
 */

void
named_server_scan_interfaces(named_server_t *server);
/*%<
 * Trigger a interface scan.
 * Must only be called when running under server->task.
 */

void
named_server_flushonshutdown(named_server_t *server, bool flush);
/*%<
 * Inform the server that the zones should be flushed to disk on shutdown.
 */

isc_result_t
named_server_reloadcommand(named_server_t *server, isc_lex_t *lex,
			   isc_buffer_t **text);
/*%<
 * Act on a "reload" command from the command channel.
 */

isc_result_t
named_server_resetstatscommand(named_server_t *server, isc_lex_t *lex,
			       isc_buffer_t **text);
/*%<
 * Act on a "reset-stats" command from the command channel.
 */

isc_result_t
named_server_reconfigcommand(named_server_t *server, isc_buffer_t *text);
/*%<
 * Act on a "reconfig" command from the command channel.
 */

isc_result_t
named_server_notifycommand(named_server_t *server, isc_lex_t *lex,
			   isc_buffer_t **text);
/*%<
 * Act on a "notify" command from the command channel.
 */

isc_result_t
named_server_refreshcommand(named_server_t *server, isc_lex_t *lex,
			    isc_buffer_t **text);
/*%<
 * Act on a "refresh" command from the command channel.
 */

isc_result_t
named_server_retransfercommand(named_server_t *server, isc_lex_t *lex,
			       isc_buffer_t **text);
/*%<
 * Act on a "retransfer" command from the command channel.
 */

isc_result_t
named_server_setortoggle(named_server_t *server, const char *optname,
			 unsigned int option, isc_lex_t *lex);
/*%<
 * Enable/disable, or toggle, a server option via the command channel.
 * 'option' is the option value to be changed (for example,
 * NS_SERVER_LOGQUERIES or NS_SERVER_LOGRESPOSNES) and 'optname' is the
 * option's human-readable name for logging purposes ("query logging"
 * or "response logging").
 *
 * If an explicit argument to enable the option was provided
 * (i.e., "on", "enable", "true", or "yes") or an explicit argument
 * to disable it ("off", "disable", "false", or "no"), it will be used.
 *
 * If no argument is provided, the option's current state will be reversed.
 */

/*%
 * Save the current NTAs for all views to files.
 */
isc_result_t
named_server_saventa(named_server_t *server);

/*%
 * Load NTAs for all views from files.
 */
isc_result_t
named_server_loadnta(named_server_t *server);

/*%
 * Dump the current statistics to the statistics file.
 */
isc_result_t
named_server_dumpstats(named_server_t *server);

/*%
 * Dump the current cache to the dump file.
 */
isc_result_t
named_server_dumpdb(named_server_t *server, isc_lex_t *lex,
		    isc_buffer_t **text);

/*%
 * Dump the current security roots to the secroots file.
 */
isc_result_t
named_server_dumpsecroots(named_server_t *server, isc_lex_t *lex,
			  isc_buffer_t **text);

/*%
 * Change or increment the server debug level.
 */
isc_result_t
named_server_setdebuglevel(named_server_t *server, isc_lex_t *lex);

/*%
 * Flush the server's cache(s)
 */
isc_result_t
named_server_flushcache(named_server_t *server, isc_lex_t *lex);

/*%
 * Flush a particular name from the server's cache.  If 'tree' is false,
 * also flush the name from the ADB and badcache.  If 'tree' is true, also
 * flush all the names under the specified name.
 */
isc_result_t
named_server_flushnode(named_server_t *server, isc_lex_t *lex, bool tree);

/*%
 * Report the server's status.
 */
isc_result_t
named_server_status(named_server_t *server, isc_buffer_t **text);

/*%
 * Enable or disable updates for a zone.
 */
isc_result_t
named_server_freeze(named_server_t *server, bool freeze, isc_lex_t *lex,
		    isc_buffer_t **text);

/*%
 * Dump zone updates to disk, optionally removing the journal file
 */
isc_result_t
named_server_sync(named_server_t *server, isc_lex_t *lex, isc_buffer_t **text);

/*%
 * Update a zone's DNSKEY set from the key repository.  If
 * the command that triggered the call to this function was "sign",
 * then force a full signing of the zone.  If it was "loadkeys",
 * then don't sign the zone; any needed changes to signatures can
 * take place incrementally.
 */
isc_result_t
named_server_rekey(named_server_t *server, isc_lex_t *lex, isc_buffer_t **text);

/*%
 * Dump the current recursive queries.
 */
isc_result_t
named_server_dumprecursing(named_server_t *server);

/*%
 * Enable or disable dnssec validation.
 */
isc_result_t
named_server_validation(named_server_t *server, isc_lex_t *lex,
			isc_buffer_t **text);

/*%
 * Add a zone to a running process, or modify an existing zone
 */
isc_result_t
named_server_changezone(named_server_t *server, char *command,
			isc_buffer_t **text);

/*%
 * Deletes a zone from a running process
 */
isc_result_t
named_server_delzone(named_server_t *server, isc_lex_t *lex,
		     isc_buffer_t **text);

/*%
 * Show current configuration for a given zone
 */
isc_result_t
named_server_showzone(named_server_t *server, isc_lex_t *lex,
		      isc_buffer_t **text);

/*%
 * Lists the status of the signing records for a given zone.
 */
isc_result_t
named_server_signing(named_server_t *server, isc_lex_t *lex,
		     isc_buffer_t **text);

/*%
 * Lists the DNSSEC status for a given zone.
 */
isc_result_t
named_server_dnssec(named_server_t *server, isc_lex_t *lex,
		    isc_buffer_t **text);

/*%
 * Lists status information for a given zone (e.g., name, type, files,
 * load time, expiry, etc).
 */
isc_result_t
named_server_zonestatus(named_server_t *server, isc_lex_t *lex,
			isc_buffer_t **text);

/*%
 * Adds/updates a Negative Trust Anchor (NTA) for a specified name and
 * duration, in a particular view if specified, or in all views.
 */
isc_result_t
named_server_nta(named_server_t *server, isc_lex_t *lex, bool readonly,
		 isc_buffer_t **text);

/*%
 * Generates a test sequence that is only for use in system tests. The
 * argument is the size of required output in bytes.
 */
isc_result_t
named_server_testgen(isc_lex_t *lex, isc_buffer_t **text);

/*%
 * Force fefresh or print status for managed keys zones.
 */
isc_result_t
named_server_mkeys(named_server_t *server, isc_lex_t *lex, isc_buffer_t **text);

/*%
 * Close and reopen DNSTAP output file.
 */
isc_result_t
named_server_dnstap(named_server_t *server, isc_lex_t *lex,
		    isc_buffer_t **text);

/*%
 * Display or update tcp-{initial,idle,keepalive,advertised}-timeout options.
 */
isc_result_t
named_server_tcptimeouts(isc_lex_t *lex, isc_buffer_t **text);

/*%
 * Control whether stale answers are served or not when configured in
 * named.conf.
 */
isc_result_t
named_server_servestale(named_server_t *server, isc_lex_t *lex,
			isc_buffer_t **text);

/*%
 * Report fetch-limited ADB server addresses.
 */
isc_result_t
named_server_fetchlimit(named_server_t *server, isc_lex_t *lex,
			isc_buffer_t **text);

/*%
 * Import SKR file for offline KSK signing.
 */
isc_result_t
named_server_skr(named_server_t *server, isc_lex_t *lex, isc_buffer_t **text);

/*%
 * Toggle memory profiling if supported.
 */
isc_result_t
named_server_togglememprof(isc_lex_t *lex);

/*%
 * Get status of memory profiling.
 */
const char *
named_server_getmemprof(void);
