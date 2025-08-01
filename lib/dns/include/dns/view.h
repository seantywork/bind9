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

/*****
***** Module Info
*****/

/*! \file dns/view.h
 * \brief
 * DNS View
 *
 * A "view" is a DNS namespace, together with an optional resolver and a
 * forwarding policy.  A "DNS namespace" is a (possibly empty) set of
 * authoritative zones together with an optional cache and optional
 * "hints" information.
 *
 * Views start out "unfrozen".  In this state, core attributes like
 * the cache, set of zones, and forwarding policy may be set.  While
 * "unfrozen", the caller (e.g. nameserver configuration loading
 * code), must ensure exclusive access to the view.  When the view is
 * "frozen", the core attributes become immutable, and the view module
 * will ensure synchronization.  Freezing allows the view's core attributes
 * to be accessed without locking.
 *
 * MP:
 *\li	Before the view is frozen, the caller must ensure synchronization.
 *
 *\li	After the view is frozen, the module guarantees appropriate
 *	synchronization of any data structures it creates and manipulates.
 *
 * Reliability:
 *\li	No anticipated impact.
 *
 * Resources:
 *\li	TBS
 *
 * Security:
 *\li	No anticipated impact.
 *
 * Standards:
 *\li	None.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include <isc/magic.h>
#include <isc/mutex.h>
#include <isc/net.h>
#include <isc/refcount.h>
#include <isc/rwlock.h>
#include <isc/stdtime.h>

#include <dns/acl.h>
#include <dns/catz.h>
#include <dns/clientinfo.h>
#include <dns/dnstap.h>
#include <dns/fixedname.h>
#include <dns/nta.h>
#include <dns/rdatastruct.h>
#include <dns/rpz.h>
#include <dns/rrl.h>
#include <dns/transport.h>
#include <dns/types.h>
#include <dns/zt.h>

struct dns_view {
	/* Unlocked. */
	unsigned int	   magic;
	isc_mem_t	  *mctx;
	dns_rdataclass_t   rdclass;
	char		  *name;
	dns_zt_t	  *zonetable;
	dns_resolver_t	  *resolver;
	dns_adb_t	  *adb;
	dns_requestmgr_t  *requestmgr;
	dns_dispatchmgr_t *dispatchmgr;
	dns_cache_t	  *cache;
	dns_db_t	  *cachedb;
	dns_db_t	  *hints;

	/*
	 * security roots and negative trust anchors.
	 * internal use only; access via * dns_view_getsecroots()
	 */
	dns_keytable_t *secroots_priv;
	dns_ntatable_t *ntatable_priv;

	isc_mutex_t lock;
	bool	    frozen;
	bool	    cacheshared;

	/* Configurable data. */
	dns_transport_list_t *transports;
	dns_tsigkeyring_t    *statickeys;
	dns_tsigkeyring_t    *dynamickeys;
	dns_peerlist_t	     *peers;
	dns_order_t	     *order;
	dns_fwdtable_t	     *fwdtable;
	bool		      recursion;
	bool		      qminimization;
	bool		      qmin_strict;
	bool		      auth_nxdomain;
	bool		      minimal_any;
	dns_minimaltype_t     minimalresponses;
	bool		      enablevalidation;
	bool		      acceptexpired;
	bool		      requireservercookie;
	bool		      synthfromdnssec;
	bool		      trust_anchor_telemetry;
	bool		      root_key_sentinel;
	dns_transfer_format_t transfer_format;
	dns_acl_t	     *cacheacl;
	dns_acl_t	     *cacheonacl;
	dns_acl_t	     *queryacl;
	dns_acl_t	     *queryonacl;
	dns_acl_t	     *recursionacl;
	dns_acl_t	     *recursiononacl;
	dns_acl_t	     *notifyacl;
	dns_acl_t	     *transferacl;
	dns_acl_t	     *updateacl;
	dns_acl_t	     *upfwdacl;
	dns_acl_t	     *denyansweracl;
	dns_acl_t	     *nocasecompress;
	dns_acl_t	     *proxyacl;
	dns_acl_t	     *proxyonacl;
	bool		      msgcompression;
	dns_nametree_t	     *answeracl_exclude;
	dns_nametree_t	     *denyanswernames;
	dns_nametree_t	     *answernames_exclude;
	dns_nametree_t	     *sfd;
	dns_rrl_t	     *rrl;
	bool		      provideixfr;
	bool		      requestnsid;
	bool		      requestzoneversion;
	bool		      sendcookie;
	dns_ttl_t	      maxcachettl;
	dns_ttl_t	      maxncachettl;
	dns_ttl_t	      mincachettl;
	dns_ttl_t	      minncachettl;
	uint32_t	      nta_lifetime;
	uint32_t	      nta_recheck;
	char		     *nta_file;
	dns_ttl_t	      prefetch_trigger;
	dns_ttl_t	      prefetch_eligible;
	in_port_t	      dstport;
	dns_aclenv_t	     *aclenv;
	dns_rdatatype_t	      preferred_glue;
	bool		      flush;
	bool		      checknames;
	uint16_t	      maxudp;
	dns_ttl_t	      staleanswerttl;
	dns_stale_answer_t    staleanswersok;	  /* rndc setting */
	bool		      staleanswersenable; /* named.conf setting */
	uint32_t	      staleanswerclienttimeout;
	uint16_t	      nocookieudp;
	uint16_t	      padding;
	dns_acl_t	     *pad_acl;
	dns_dns64list_t	      dns64;
	unsigned int	      dns64cnt;
	bool		      usedns64;
	dns_rpz_zones_t	     *rpzs;
	dns_catz_zones_t     *catzs;
	dns_dlzdblist_t	      dlz_searched;
	dns_dlzdblist_t	      dlz_unsearched;
	uint32_t	      fail_ttl;
	dns_badcache_t	     *failcache;
	dns_unreachcache_t   *unreachcache;
	unsigned int	      udpsize;
	uint32_t	      sig0key_checks_limit;
	uint32_t	      sig0message_checks_limit;
	uint32_t	      maxrrperset;
	uint32_t	      maxtypepername;
	uint16_t	      max_queries;
	uint8_t		      max_restarts;

	/*
	 * Configurable data for server use only,
	 * locked by server configuration lock.
	 */
	dns_acl_t *matchclients;
	dns_acl_t *matchdestinations;
	bool	   matchrecursiveonly;

	/* Locked by themselves. */
	isc_refcount_t references;
	isc_refcount_t weakrefs;

	/* Under owner's locking control. */
	ISC_LINK(struct dns_view) link;
	dns_viewlist_t *viewlist;

	dns_zone_t *managed_keys;
	dns_zone_t *redirect;
	dns_name_t *redirectzone; /* points to
				   * redirectfixed
				   * when valid */
	dns_fixedname_t redirectfixed;

	/*
	 * File and configuration data for zones added at runtime
	 * (only used in BIND9).
	 *
	 * XXX: This should be a pointer to an opaque type that
	 * named implements.
	 */
	char	*new_zone_dir;
	char	*new_zone_file;
	char	*new_zone_db;
	void	*new_zone_dbenv;
	uint64_t new_zone_mapsize;
	void	*new_zone_config;
	void (*cfg_destroy)(void **);
	isc_mutex_t new_zone_lock;

	unsigned char secret[32]; /* Client secret */
	unsigned int  v6bias;

	dns_dtenv_t    *dtenv;	 /* Dnstap environment */
	dns_dtmsgtype_t dttypes; /* Dnstap message types
				  * to log */

	/* Registered module instances */
	void *plugins;
	void (*plugins_free)(isc_mem_t *, void **);

	/* Hook table */
	void *hooktable; /* ns_hooktable */
	void (*hooktable_free)(isc_mem_t *, void **);
};

#define DNS_VIEW_MAGIC	     ISC_MAGIC('V', 'i', 'e', 'w')
#define DNS_VIEW_VALID(view) ISC_MAGIC_VALID(view, DNS_VIEW_MAGIC)

#define DNS_VIEWATTR_RESSHUTDOWN 0x01
#define DNS_VIEWATTR_ADBSHUTDOWN 0x02
#define DNS_VIEWATTR_REQSHUTDOWN 0x04

#ifdef HAVE_LMDB
#define DNS_LMDB_COMMON_FLAGS (MDB_CREATE | MDB_NOSUBDIR | MDB_NOLOCK)
#ifndef __OpenBSD__
#define DNS_LMDB_FLAGS (DNS_LMDB_COMMON_FLAGS)
#else /* __OpenBSD__ */
/*
 * OpenBSD does not have a unified buffer cache, which requires both reads and
 * writes to be performed using mmap().
 */
#define DNS_LMDB_FLAGS (DNS_LMDB_COMMON_FLAGS | MDB_WRITEMAP)
#endif /* __OpenBSD__ */
#endif /* HAVE_LMDB */

void
dns_view_create(isc_mem_t *mctx, dns_dispatchmgr_t *dispmgr,
		dns_rdataclass_t rdclass, const char *name, dns_view_t **viewp);
/*%<
 * Create a view.
 *
 * Notes:
 *
 *\li	The newly created view has no cache, no resolver, and an empty
 *	zone table.  The view is not frozen.
 *
 * Requires:
 *
 *\li	'mctx' is a valid memory context.
 *
 *\li	'rdclass' is a valid class.
 *
 *\li	'name' is a valid C string.
 *
 *\li	viewp != NULL && *viewp == NULL
 */

void
dns_view_attach(dns_view_t *source, dns_view_t **targetp);
/*%<
 * Attach '*targetp' to 'source', incrementing the view's reference
 * counter.
 *
 * Requires:
 *
 *\li	'source' is a valid, frozen view.
 *
 *\li	'targetp' points to a NULL dns_view_t *.
 *
 * Ensures:
 *
 *\li	*targetp is attached to source.
 *
 *\li	While *targetp is attached, the view will not shut down.
 */

void
dns_view_detach(dns_view_t **viewp);
/*%<
 * Detach '*viewp' and decrement the view's reference counter.  If this was
 * the last reference, then the associated resolver, requestmgr, ADB and
 * zones will be shut down; if dns_view_flushonshutdown() has been called
 * with 'true', uncommitted changed in zones will also be flushed to disk.
 * The view will not be fully destroyed, however, until the weak references
 * (see below) reach zero as well.
 *
 * Requires:
 *
 *\li	'viewp' points to a valid dns_view_t *
 *
 * Ensures:
 *
 *\li	*viewp is NULL.
 */

void
dns_view_weakattach(dns_view_t *source, dns_view_t **targetp);
/*%<
 * Attach '*targetp' to 'source', incrementing the view's weak reference
 * counter.
 *
 * Weak references are used by objects such as the resolver, requestmgr,
 * ADB, and zones, which are subsidiary to the view; they need the view
 * object to remain in existence as long as they persist, but they do
 * not need to prevent it from being shut down.
 *
 * Requires:
 *
 *\li	'source' is a valid, frozen view.
 *
 *\li	'targetp' points to a NULL dns_view_t *.
 *
 * Ensures:
 *
 *\li	*targetp is attached to source.
 *
 * \li	While *targetp is attached, the view will not be freed.
 */

void
dns_view_weakdetach(dns_view_t **targetp);
/*%<
 * Detach '*viewp' from its view. If this is the last weak reference,
 * the view will be destroyed.
 *
 * Requires:
 *
 *\li	'viewp' points to a valid dns_view_t *.
 *
 * Ensures:
 *
 *\li	*viewp is NULL.
 */

isc_result_t
dns_view_createresolver(dns_view_t *view, unsigned int options,
			isc_tlsctx_cache_t *tlsctx_cache,
			dns_dispatch_t *dispatchv4, dns_dispatch_t *dispatchv6);
/*%<
 * Create a resolver and address database for the view.
 *
 * Requires:
 *
 *\li	'view' is a valid, unfrozen view.
 *
 *\li	'view' does not have a resolver already.
 *
 *\li	The requirements of dns_resolver_create() apply to 'ndisp',
 *	'netmgr', 'options', 'tlsctx_cache', 'dispatchv4', and 'dispatchv6'.
 *
 * Returns:
 *
 *\li   	#ISC_R_SUCCESS
 *
 *\li	Any error that dns_resolver_create() can return.
 */

void
dns_view_setcache(dns_view_t *view, dns_cache_t *cache, bool shared);
/*%<
 * Set the view's cache database.  If 'shared' is true, this means the cache
 * is created by another view and is shared with that view.  dns_view_setcache()
 * is a backward compatible version equivalent to setcache2(..., false).
 *
 * Requires:
 *
 *\li	'view' is a valid, unfrozen view.
 *
 *\li	'cache' is a valid cache.
 *
 * Ensures:
 *
 * \li    	The cache of 'view' is 'cached.
 *
 *\li	If this is not the first call to dns_view_setcache() for this
 *	view, then previously set cache is detached.
 */

void
dns_view_sethints(dns_view_t *view, dns_db_t *hints);
/*%<
 * Set the view's hints database.
 *
 * Requires:
 *
 *\li	'view' is a valid, unfrozen view, whose hints database has not been
 *	set.
 *
 *\li	'hints' is a valid zone database.
 *
 * Ensures:
 *
 * \li    	The hints database of 'view' is 'hints'.
 */

void
dns_view_settransports(dns_view_t *view, dns_transport_list_t *list);

void
dns_view_setkeyring(dns_view_t *view, dns_tsigkeyring_t *ring);
void
dns_view_setdynamickeyring(dns_view_t *view, dns_tsigkeyring_t *ring);
/*%<
 * Set the view's static TSIG keys
 *
 * Requires:
 *
 *   \li   'view' is a valid, unfrozen view, whose static TSIG keyring has not
 *	been set.
 *
 *\li      'ring' is a valid TSIG keyring
 *
 * Ensures:
 *
 *\li      The static TSIG keyring of 'view' is 'ring'.
 */

void
dns_view_getdynamickeyring(dns_view_t *view, dns_tsigkeyring_t **ringp);
/*%<
 * Return the views dynamic keys.
 *
 *   \li  'view' is a valid, unfrozen view.
 *   \li  'ringp' != NULL && ringp == NULL.
 */

void
dns_view_setdstport(dns_view_t *view, in_port_t dstport);
/*%<
 * Set the view's destination port.  This is the port to
 * which outgoing queries are sent.  The default is 53,
 * the standard DNS port.
 *
 * Requires:
 *
 *\li      'view' is a valid view.
 *
 *\li      'dstport' is a valid TCP/UDP port number.
 *
 * Ensures:
 *\li	External name servers will be assumed to be listening
 *	on 'dstport'.  For servers whose address has already
 *	obtained obtained at the time of the call, the view may
 *	continue to use the previously set port until the address
 *	times out from the view's address database.
 */

isc_result_t
dns_view_addzone(dns_view_t *view, dns_zone_t *zone);
/*%<
 * Add zone 'zone' to 'view'.
 *
 * Requires:
 *
 *\li	'view' is a valid, unfrozen view.
 *
 *\li	'zone' is a valid zone.
 */

isc_result_t
dns_view_delzone(dns_view_t *view, dns_zone_t *zone);
/*%<
 * Removes zone 'zone' from 'view'.
 *
 * Requires:
 *
 *\li	'zone' is a valid zone.
 */

void
dns_view_freeze(dns_view_t *view);
/*%<
 * Freeze view.  No changes can be made to view configuration while frozen.
 *
 * Requires:
 *
 *\li	'view' is a valid, unfrozen view.
 *
 * Ensures:
 *
 *\li	'view' is frozen.
 */

void
dns_view_thaw(dns_view_t *view);
/*%<
 * Thaw view.  This allows zones to be added or removed at runtime.  This is
 * NOT thread-safe; the caller MUST have paused the loopmgr prior to thawing
 * the view.
 *
 * Requires:
 *
 *\li	'view' is a valid, frozen view.
 *
 * Ensures:
 *
 *\li	'view' is no longer frozen.
 */

isc_result_t
dns_view_find(dns_view_t *view, const dns_name_t *name, dns_rdatatype_t type,
	      isc_stdtime_t now, unsigned int options, bool use_hints,
	      bool use_static_stub, dns_db_t **dbp, dns_dbnode_t **nodep,
	      dns_name_t *foundname, dns_rdataset_t *rdataset,
	      dns_rdataset_t *sigrdataset);
/*%<
 * Find an rdataset whose owner name is 'name', and whose type is
 * 'type'.
 * In general, this function first searches view's zone and cache DBs for the
 * best match data against 'name'.  If nothing found there, and if 'use_hints'
 * is true, the view's hint DB (if configured) is searched.
 * If the view is configured with a static-stub zone which gives the longest
 * match for 'name' among the zones, however, the cache DB is not consulted
 * unless 'use_static_stub' is false (see below about this argument).
 *
 * Notes:
 *
 *\li	See the description of dns_db_find() for information about 'options'.

 *\li	If the caller sets #DNS_DBFIND_GLUEOK, it must ensure that 'name'
 *	and 'type' are appropriate for glue retrieval. Glue found in a
 *	zone database will be returned without checking the cache for a
 *	better answer.
 *
 *\li	If 'now' is zero, then the current time will be used.
 *
 *\li	If 'use_hints' is true, and the view has a hints database, then
 *	it will be searched last.  If the answer is found in the hints
 *	database, the result code will be DNS_R_HINT.  If the name is found
 *	in the hints database but not the type, the result code will be
 *	#DNS_R_HINTNXRRSET.
 *
 *\li	If 'use_static_stub' is false and the longest match zone for 'name'
 *	is a static-stub zone, it's ignored and the cache and/or hints will be
 *	searched.  In the majority of the cases this argument should be
 *	false.  The only known usage of this argument being true is
 *	if this search is for a "bailiwick" glue A or AAAA RRset that may
 *	best match a static-stub zone.  Consider the following example:
 *	this view is configured with a static-stub zone "example.com",
 *	and an attempt of recursive resolution needs to send a query for the
 *	zone.  In this case it's quite likely that the resolver is trying to
 *	find A/AAAA RRs for the apex name "example.com".  And, to honor the
 *	static-stub configuration it needs to return the glue RRs in the
 *	static-stub zone even if that exact RRs coming from the authoritative
 *	zone has been cached.
 *	In other general cases, the requested data is better to be
 *	authoritative, either locally configured or retrieved from an external
 *	server, and the data in the static-stub zone should better be ignored.
 *
 *\li	'foundname' must meet the requirements of dns_db_find().
 *
 *\li	If 'sigrdataset' is not NULL, and there is a SIG rdataset which
 *	covers 'type', then 'sigrdataset' will be bound to it.
 *
 * Requires:
 *
 *\li	'view' is a valid, frozen view.
 *
 *\li	'name' is valid name.
 *
 *\li	'type' is a valid dns_rdatatype_t, and is not a meta query type
 *	except dns_rdatatype_any.
 *
 *\li	dbp == NULL || *dbp == NULL
 *
 *\li	nodep == NULL || *nodep == NULL.  If nodep != NULL, dbp != NULL.
 *
 *\li	'foundname' is a valid name with a dedicated buffer or NULL.
 *
 *\li	'rdataset' is a valid, disassociated rdataset.
 *
 *\li	'sigrdataset' is NULL, or is a valid, disassociated rdataset.
 *
 * Ensures:
 *
 *\li	In successful cases, 'rdataset', and possibly 'sigrdataset', are
 *	bound to the found data.
 *
 *\li	If dbp != NULL, it points to the database containing the data.
 *
 *\li	If nodep != NULL, it points to the database node containing the data.
 *
 *\li	If foundname != NULL, it contains the full name of the found data.
 *
 * Returns:
 *
 *\li	Any result that dns_db_find() can return, with the exception of
 *	#DNS_R_DELEGATION.
 */

isc_result_t
dns_view_simplefind(dns_view_t *view, const dns_name_t *name,
		    dns_rdatatype_t type, isc_stdtime_t now,
		    unsigned int options, bool use_hints,
		    dns_rdataset_t *rdataset, dns_rdataset_t *sigrdataset);
/*%<
 * Find an rdataset whose owner name is 'name', and whose type is
 * 'type'.
 *
 * Notes:
 *
 *\li	This routine is appropriate for simple, exact-match queries of the
 *	view.  'name' must be a canonical name; there is no DNAME or CNAME
 *	processing.
 *
 *\li	See the description of dns_db_find() for information about 'options'.
 *	If the caller sets DNS_DBFIND_GLUEOK, it must ensure that 'name'
 *	and 'type' are appropriate for glue retrieval.
 *
 *\li	If 'now' is zero, then the current time will be used.
 *
 *\li	If 'use_hints' is true, and the view has a hints database, then
 *	it will be searched last.  If the answer is found in the hints
 *	database, the result code will be DNS_R_HINT.  If the name is found
 *	in the hints database but not the type, the result code will be
 *	DNS_R_HINTNXRRSET.
 *
 *\li	If 'sigrdataset' is not NULL, and there is a SIG rdataset which
 *	covers 'type', then 'sigrdataset' will be bound to it.
 *
 * Requires:
 *
 *\li	'view' is a valid, frozen view.
 *
 *\li	'name' is valid name.
 *
 *\li	'type' is a valid dns_rdatatype_t, and is not a meta query type
 *	(e.g. dns_rdatatype_any), or dns_rdatatype_rrsig.
 *
 *\li	'rdataset' is a valid, disassociated rdataset.
 *
 *\li	'sigrdataset' is NULL, or is a valid, disassociated rdataset.
 *
 * Ensures:
 *
 *\li	In successful cases, 'rdataset', and possibly 'sigrdataset', are
 *	bound to the found data.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS			Success; result is desired type.
 *\li	DNS_R_GLUE			Success; result is glue.
 *\li	DNS_R_HINT			Success; result is a hint.
 *\li	DNS_R_NCACHENXDOMAIN		Success; result is a ncache entry.
 *\li	DNS_R_NCACHENXRRSET		Success; result is a ncache entry.
 *\li	DNS_R_NXDOMAIN			The name does not exist.
 *\li	DNS_R_NXRRSET			The rrset does not exist.
 *\li	#ISC_R_NOTFOUND			No matching data found,
 *					or an error occurred.
 */

isc_result_t
dns_view_findzonecut(dns_view_t *view, const dns_name_t *name,
		     dns_name_t *fname, dns_name_t *dcname, isc_stdtime_t now,
		     unsigned int options, bool use_hints, bool use_cache,
		     dns_rdataset_t *rdataset, dns_rdataset_t *sigrdataset);
/*%<
 * Find the best known zonecut containing 'name'.
 *
 * This uses local authority, cache, and optionally hints data.
 * No external queries are performed.
 *
 * Notes:
 *
 *\li	If 'now' is zero, then the current time will be used.
 *
 *\li	If 'use_hints' is true, and the view has a hints database, then
 *	it will be searched last.
 *
 *\li	If 'use_cache' is true, and the view has a cache, then it will be
 *	searched.
 *
 *\li	If 'sigrdataset' is not NULL, and there is a SIG rdataset which
 *	covers 'type', then 'sigrdataset' will be bound to it.
 *
 *\li	If the DNS_DBFIND_NOEXACT option is set, then the zonecut returned
 *	(if any) will be the deepest known ancestor of 'name'.
 *
 *\li	If dcname is not NULL the deepest cached name is copied to it.
 *
 * Requires:
 *
 *\li	'view' is a valid, frozen view.
 *
 *\li	'name' is valid name.
 *
 *\li	'rdataset' is a valid, disassociated rdataset.
 *
 *\li	'sigrdataset' is NULL, or is a valid, disassociated rdataset.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS				Success.
 *
 *\li	Many other results are possible.
 */

isc_result_t
dns_viewlist_find(dns_viewlist_t *list, const char *name,
		  dns_rdataclass_t rdclass, dns_view_t **viewp);
/*%<
 * Search for a view with name 'name' and class 'rdclass' in 'list'.
 * If found, '*viewp' is (strongly) attached to it.
 *
 * Requires:
 *
 *\li	'viewp' points to a NULL dns_view_t *.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS		A matching view was found.
 *\li	#ISC_R_NOTFOUND		No matching view was found.
 */

isc_result_t
dns_viewlist_findzone(dns_viewlist_t *list, const dns_name_t *name,
		      bool allclasses, dns_rdataclass_t rdclass,
		      dns_zone_t **zonep);

/*%<
 * Search zone with 'name' in view with 'rdclass' in viewlist 'list'
 * If found, zone is returned in *zonep. If allclasses is set rdclass is ignored
 *
 * Returns:
 *\li	#ISC_R_SUCCESS          A matching zone was found.
 *\li	#ISC_R_NOTFOUND         No matching zone was found.
 *\li	#ISC_R_MULTIPLE         Multiple zones with the same name were found.
 */

isc_result_t
dns_view_findzone(dns_view_t *view, const dns_name_t *name,
		  unsigned int options, dns_zone_t **zonep);
/*%<
 * Search for the zone 'name' in the zone table of 'view'.
 * If found, 'zonep' is (strongly) attached to it.
 *
 * Requires:
 *
 *\li	'zonep' points to a NULL dns_zone_t *.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS		A matching zone was found.
 *\li	#ISC_R_NOTFOUND		No matching zone was found.
 */

isc_result_t
dns_view_load(dns_view_t *view, bool stop, bool newonly);

isc_result_t
dns_view_asyncload(dns_view_t *view, bool newonly, dns_zt_callback_t *callback,
		   void *arg);
/*%<
 * Load zones attached to this view.  dns_view_load() loads
 * all zones whose master file has changed since the last
 * load
 *
 * dns_view_asyncload() loads zones asynchronously.  When all zones
 * in the view have finished loading, 'callback' is called with argument
 * 'arg' to inform the caller.
 *
 * If 'stop' is true, stop on the first error and return it.
 * If 'stop' is false (or we are loading asynchronously), ignore errors.
 *
 * If 'newonly' is true load only zones that were never loaded.
 *
 * Requires:
 *
 *\li	'view' is valid.
 */

isc_result_t
dns_view_gettransport(dns_view_t *view, const dns_transport_type_t type,
		      const dns_name_t *name, dns_transport_t **transportp);

isc_result_t
dns_view_gettsig(dns_view_t *view, const dns_name_t *keyname,
		 dns_tsigkey_t **keyp);
/*%<
 * Find the TSIG key configured in 'view' with name 'keyname',
 * if any.
 *
 * Requires:
 *\li	keyp points to a NULL dns_tsigkey_t *.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS	A key was found and '*keyp' now points to it.
 *\li	#ISC_R_NOTFOUND	No key was found.
 *\li	others		An error occurred.
 */

isc_result_t
dns_view_getpeertsig(dns_view_t *view, const isc_netaddr_t *peeraddr,
		     dns_tsigkey_t **keyp);
/*%<
 * Find the TSIG key configured in 'view' for the server whose
 * address is 'peeraddr', if any.
 *
 * Requires:
 *	keyp points to a NULL dns_tsigkey_t *.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS	A key was found and '*keyp' now points to it.
 *\li	#ISC_R_NOTFOUND	No key was found.
 *\li	others		An error occurred.
 */

isc_result_t
dns_view_checksig(dns_view_t *view, isc_buffer_t *source, dns_message_t *msg);
/*%<
 * Verifies the signature of a message.
 *
 * Requires:
 *
 *\li	'view' is a valid view.
 *\li	'source' is a valid buffer containing the message
 *\li	'msg' is a valid message
 *
 * Returns:
 *\li	see dns_tsig_verify()
 */

isc_result_t
dns_view_flushcache(dns_view_t *view, bool fixuponly);
/*%<
 * Flush the view's cache (and ADB).  If 'fixuponly' is true, it only updates
 * the internal reference to the cache DB with omitting actual flush operation.
 * 'fixuponly' is intended to be used for a view that shares a cache with
 * a different view.  dns_view_flushcache() is a backward compatible version
 * that always sets fixuponly to false.
 *
 * Requires:
 * 	'view' is valid.
 *
 * 	No other tasks are executing.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS
 *	other returns are failures.
 */

isc_result_t
dns_view_flushnode(dns_view_t *view, const dns_name_t *name, bool tree);
/*%<
 * Flush the given name from the view's cache (and optionally ADB/badcache).
 *
 * Flush the given name from the cache, ADB, and bad cache.  If 'tree'
 * is true, also flush all subdomains of 'name'.
 *
 * Requires:
 *\li	'view' is valid.
 *\li	'name' is valid.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS
 *	other returns are failures.
 */

isc_result_t
dns_view_flushname(dns_view_t *view, const dns_name_t *name);
/*%<
 * Flush the given name from the view's cache, ADB and badcache.
 * Equivalent to dns_view_flushnode(view, name, false).
 *
 *
 * Requires:
 *\li	'view' is valid.
 *\li	'name' is valid.
 *
 * Returns:
 *\li	#ISC_R_SUCCESS
 *	other returns are failures.
 */

isc_result_t
dns_view_freezezones(dns_view_t *view, bool freeze);
/*%<
 * Freeze/thaw updates to primary zones.
 *
 * Requires:
 * \li	'view' is valid.
 */

bool
dns_view_iscacheshared(dns_view_t *view);
/*%<
 * Check if the view shares the cache created by another view.
 *
 * Requires:
 * \li	'view' is valid.
 *
 * Returns:
 *\li	#true if the cache is shared.
 *\li	#false otherwise.
 */

void
dns_view_initntatable(dns_view_t *view);
/*%<
 * Initialize the negative trust anchor table for the view.
 *
 * Requires:
 * \li	'view' is valid.
 */

isc_result_t
dns_view_getntatable(dns_view_t *view, dns_ntatable_t **ntp);
/*%<
 * Get the negative trust anchor table for this view.  Returns
 * ISC_R_NOTFOUND if the table not been initialized for the view.
 *
 * '*ntp' is attached on success; the caller is responsible for
 * detaching it with dns_ntatable_detach().
 *
 * Requires:
 * \li	'view' is valid.
 * \li	'nta' is not NULL and '*nta' is NULL.
 *
 * Returns:
 *\li	ISC_R_SUCCESS
 *\li	ISC_R_NOTFOUND
 */

void
dns_view_initsecroots(dns_view_t *view);
/*%<
 * Initialize security roots for the view, detaching any previously
 * existing security roots first.  (Note that secroots_priv is
 * NULL until this function is called, so any function using
 * security roots must check that they have been initialized first.
 * One way to do this is use dns_view_getsecroots() and check its
 * return value.)
 *
 * Requires:
 * \li	'view' is valid.
 *
 * Returns:
 *\li	ISC_R_SUCCESS
 *\li	Any other result indicates failure
 */

isc_result_t
dns_view_getsecroots(dns_view_t *view, dns_keytable_t **ktp);
/*%<
 * Get the security roots for this view.  Returns ISC_R_NOTFOUND if
 * the security roots keytable has not been initialized for the view.
 *
 * '*ktp' is attached on success; the caller is responsible for
 * detaching it with dns_keytable_detach().
 *
 * Requires:
 * \li	'view' is valid.
 * \li	'ktp' is not NULL and '*ktp' is NULL.
 *
 * Returns:
 *\li	ISC_R_SUCCESS
 *\li	ISC_R_NOTFOUND
 */

isc_result_t
dns_view_issecuredomain(dns_view_t *view, const dns_name_t *name,
			isc_stdtime_t now, bool checknta, bool *ntap,
			bool *secure_domain);
/*%<
 * Is 'name' at or beneath a trusted key, and not covered by a valid
 * negative trust anchor?  Put answer in '*secure_domain'.
 *
 * If 'checknta' is false, ignore the NTA table in determining
 * whether this is a secure domain. If 'checknta' is not false, and if
 * 'ntap' is non-NULL, then '*ntap' will be updated with true if the
 * name is covered by an NTA.
 *
 * Requires:
 * \li	'view' is valid.
 *
 * Returns:
 *\li	ISC_R_SUCCESS
 *\li	Any other value indicates failure
 */

bool
dns_view_ntacovers(dns_view_t *view, isc_stdtime_t now, const dns_name_t *name,
		   const dns_name_t *anchor);
/*%<
 * Is there a current negative trust anchor above 'name' and below 'anchor'?
 *
 * Requires:
 * \li	'view' is valid.
 *
 * Returns:
 *\li	ISC_R_TRUE
 *\li	ISC_R_FALSE
 */

void
dns_view_untrust(dns_view_t *view, const dns_name_t *keyname,
		 const dns_rdata_dnskey_t *dnskey);
/*%<
 * Remove keys that match 'keyname' and 'dnskey' from the views trust
 * anchors.
 *
 * (NOTE: If the configuration specifies that there should be a
 * trust anchor at 'keyname', but no keys are left after this
 * operation, that is an error.  We fail closed, inserting a NULL
 * key so as to prevent validation until a legimitate key has been
 * provided.)
 *
 * Requires:
 * \li	'view' is valid.
 * \li	'keyname' is valid.
 * \li	'dnskey' is valid.
 */

bool
dns_view_istrusted(dns_view_t *view, const dns_name_t *keyname,
		   const dns_rdata_dnskey_t *dnskey);
/*%<
 * Determine if the key defined by 'keyname' and 'dnskey' is
 * trusted by 'view'.
 *
 * Requires:
 * \li	'view' is valid.
 * \li	'keyname' is valid.
 * \li	'dnskey' is valid.
 */

isc_result_t
dns_view_setnewzones(dns_view_t *view, bool allow, void *cfgctx,
		     void (*cfg_destroy)(void **), uint64_t mapsize);
/*%<
 * Set whether or not to allow zones to be created or deleted at runtime.
 *
 * If 'allow' is true, determines the filename into which new zone
 * configuration will be written.  Preserves the configuration context
 * (a pointer to which is passed in 'cfgctx') for use when parsing new
 * zone configuration.  'cfg_destroy' points to a callback routine to
 * destroy the configuration context when the view is destroyed.  (This
 * roundabout method is used in order to avoid libdns having a dependency
 * on libisccfg and libbind9.)
 *
 * If 'allow' is false, removes any existing references to
 * configuration context and frees any memory.
 *
 * Requires:
 * \li 'view' is valid.
 *
 * Returns:
 * \li ISC_R_SUCCESS
 * \li ISC_R_NOSPACE
 */

void
dns_view_setnewzonedir(dns_view_t *view, const char *dir);
const char *
dns_view_getnewzonedir(dns_view_t *view);
/*%<
 * Set/get the path to the directory in which NZF or NZD files should
 * be stored. If the path was previously set to a non-NULL value,
 * the previous value is freed.
 *
 * Requires:
 * \li 'view' is valid.
 */

void
dns_view_restorekeyring(dns_view_t *view);

isc_result_t
dns_view_searchdlz(dns_view_t *view, const dns_name_t *name,
		   unsigned int minlabels, dns_clientinfomethods_t *methods,
		   dns_clientinfo_t *clientinfo, dns_db_t **dbp);

/*%<
 * Search through the DLZ database(s) in view->dlz_searched to find
 * one that can answer a query for 'name', using the DLZ driver's
 * findzone method.  If successful, '*dbp' is set to point to the
 * DLZ database.
 *
 * Returns:
 * \li ISC_R_SUCCESS
 * \li ISC_R_NOTFOUND
 *
 * Requires:
 * \li 'view' is valid.
 * \li 'name' is not NULL.
 * \li 'dbp' is not NULL and *dbp is NULL.
 */

uint32_t
dns_view_getfailttl(dns_view_t *view);
/*%<
 * Get the view's servfail-ttl.  zero => no servfail caching.
 *
 * Requires:
 *\li	'view' to be valid.
 */

void
dns_view_setfailttl(dns_view_t *view, uint32_t failttl);
/*%<
 * Set the view's servfail-ttl.  zero => no servfail caching.
 *
 * Requires:
 *\li	'view' to be valid.
 */

isc_result_t
dns_view_saventa(dns_view_t *view);
/*%<
 * Save NTA for names in this view to a file.
 *
 * Requires:
 *\li	'view' to be valid.
 */

isc_result_t
dns_view_loadnta(dns_view_t *view);
/*%<
 * Loads NTA for names in this view from a file.
 *
 * Requires:
 *\li	'view' to be valid.
 */

void
dns_view_setviewcommit(dns_view_t *view);
/*%<
 * Commit dns_zone_setview() calls previously made for all zones in this
 * view.
 *
 * Requires:
 *\li	'view' to be valid.
 */

void
dns_view_setviewrevert(dns_view_t *view);
/*%<
 * Revert dns_zone_setview() calls previously made for all zones in this
 * view.
 *
 * Requires:
 *\li	'view' to be valid.
 */

bool
dns_view_staleanswerenabled(dns_view_t *view);
/*%<
 * Check if stale answers are enabled for this view.
 *
 * Requires:
 *\li	'view' to be valid.
 */

void
dns_view_flushonshutdown(dns_view_t *view, bool flush);
/*%<
 * Inform the view that the zones should (or should not) be flushed to
 * disk on shutdown.
 *
 * Requires:
 *\li	'view' to be valid.
 */

void
dns_view_sfd_add(dns_view_t *view, const dns_name_t *name);
/*%<
 * Add 'name' to the synth-from-dnssec namespace tree for the
 * view.  If the tree does not already exist create it.
 *
 * Requires:
 *\li	'view' to be valid.
 *\li	'name' to be valid.
 */

void
dns_view_sfd_del(dns_view_t *view, const dns_name_t *name);
/*%<
 * Delete 'name' to the synth-from-dnssec namespace tree for
 * the view when the count of previous adds and deletes becomes
 * zero.
 *
 * Requires:
 *\li	'view' to be valid.
 *\li	'name' to be valid.
 */

void
dns_view_sfd_find(dns_view_t *view, const dns_name_t *name,
		  dns_name_t *foundname);
/*%<
 * Find the enclosing name to the synth-from-dnssec namespace tree for 'name'
 * in the specified view.
 *
 * Requires:
 *\li	'view' to be valid.
 *\li	'name' to be valid.
 *\li	'foundname' to be valid with a buffer sufficient to hold the name.
 */

isc_result_t
dns_view_getresolver(dns_view_t *view, dns_resolver_t **resolverp);
/*%<
 * Return the resolver associated with the view.
 */

void
dns_view_setmaxrrperset(dns_view_t *view, uint32_t value);
/*%<
 * Set the maximum resource records per RRSet that can be cached.
 */

void
dns_view_setmaxtypepername(dns_view_t *view, uint32_t value);
/*%<
 * Set the maximum resource record types per owner name that can be cached.
 */

void
dns_view_setudpsize(dns_view_t *view, uint16_t udpsize);
/*%<
 * Set the EDNS UDP buffer size advertised by the server.
 */

uint16_t
dns_view_getudpsize(dns_view_t *view);
/*%<
 * Get the current EDNS UDP buffer size.
 */

dns_dispatchmgr_t *
dns_view_getdispatchmgr(dns_view_t *view);
/*%<
 * Get the attached dispatch manager for the view; this will be used
 * by the resolver and request managers to send and receive DNS
 * messages.
 */

isc_result_t
dns_view_addtrustedkey(dns_view_t *view, dns_rdatatype_t rdtype,
		       const dns_name_t *keyname, isc_buffer_t *databuf);
/*%<
 * Add a DNSSEC trusted key to a view of class 'IN'.  'rdtype' is the type
 * of the RR data for the key, either DNSKEY or DS.  'keyname' is the DNS
 * name of the key, and 'databuf' stores the RR data.
 *
 * Requires:
 *
 *\li	'view' is a valid view.
 *
 *\li	'view' is class 'IN'.
 *
 *\li	'keyname' is a valid name.
 *
 *\li	'keydatabuf' is a valid buffer.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS				On success.
 *
 *\li	Anything else				Failure.
 */

isc_result_t
dns_view_apply(dns_view_t *view, bool stop, isc_result_t *sub,
	       isc_result_t (*action)(dns_zone_t *, void *), void *uap);
/*%<
 * Call dns_zt_apply on the view's zonetable.
 *
 * Returns:
 * \li  ISC_R_SUCCESS if action was applied to all nodes.  If 'stop' is
 *	false and 'sub' is non NULL then the first error (if any)
 *	reported by 'action' is returned in '*sub'. If 'stop' is true,
 *	the first error code from 'action' is returned.
 *
 * \li ISC_R_SHUTTINGDOWN if the view is in the process of shutting down.
 */

void
dns_view_getadb(dns_view_t *view, dns_adb_t **adbp);
/*%<
 * Get the view's adb if it exist.
 *
 * Requires:
 *
 *\li	'view' is a valid view.
 *\li	'adbp' is non-NULL and '*adbp' is NULL.
 */

void
dns_view_setmaxrestarts(dns_view_t *view, uint8_t max_restarts);
/*%<
 * Set the number of permissible chained queries before we give up,
 * to prevent CNAME loops. This defaults to 11.
 *
 * Requires:
 *
 *\li	'view' is valid;
 *\li	'max_restarts' is greater than 0.
 */

void
dns_view_setmaxqueries(dns_view_t *view, uint16_t max_queries);
/*%
 * Set the number of permissible outgoing queries before we give up.
 * This defaults to 200.
 *
 * Requires:
 *
 *\li	'view' is valid;
 *\li	'max_queries' is greater than 0.
 */
