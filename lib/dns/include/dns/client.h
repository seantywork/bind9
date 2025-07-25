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

/*! \file
 *
 * \brief
 * The DNS client module provides convenient programming interfaces to various
 * DNS services, such as name resolution with or without DNSSEC validation or
 * dynamic DNS update.  This module is primarily expected to be used by other
 * applications than BIND9-related ones that need such advanced DNS features.
 *
 * MP:
 *\li	In the typical usage of this module, application threads will not share
 *	the same data structures created and manipulated in this module.
 *	However, the module still ensures appropriate synchronization of such
 *	data structures.
 *
 * Resources:
 *\li	TBS
 *
 * Security:
 *\li	This module does not handle any low-level data directly, and so no
 *	security issue specific to this module is anticipated.
 */

#include <isc/loop.h>
#include <isc/sockaddr.h>
#include <isc/tls.h>

#include <dns/tsig.h>
#include <dns/types.h>

#include <dst/dst.h>

/***
 *** Types
 ***/

/*%
 * Optional flags for dns_client_resolve.
 */
/*%< Do not return DNSSEC data (e.g. RRSIGS) with response. */
#define DNS_CLIENTRESOPT_NODNSSEC 0x01
/*%< Allow running external context. */
#define DNS_CLIENTRESOPT_RESERVED 0x02
/*%< Don't validate responses. */
#define DNS_CLIENTRESOPT_NOVALIDATE 0x04
/*%< Don't set the CD flag on upstream queries. */
#define DNS_CLIENTRESOPT_NOCDFLAG 0x08
/*%< Use TCP transport. */
#define DNS_CLIENTRESOPT_TCP 0x10

/*%
 * View name used in dns_client.
 */
#define DNS_CLIENTVIEW_NAME "_dnsclient"

/*%
 * A dns_clientresume_t holds state for resolution performed by a client,
 * and is sent to the callback when the resolution completes.
 * 'result' stores the result code of the entire resolution
 * procedure.  'vresult' specifically stores the result code of DNSSEC
 * validation if it is performed.  When name resolution successfully completes,
 * 'answerlist' is typically non empty, containing answer names along with
 * RRsets. 'cb' is the callback function and 'arg' is the callback argument
 * that was specified by the caller.
 *
 * It is the receiver's responsibility to free 'answerlist' by
 * calling dns_client_freeresanswer(), and to free the dns_clientresume
 * structure itself.
 */
typedef struct dns_clientresume {
	isc_mem_t     *mctx;
	isc_result_t   result;
	isc_result_t   vresult;
	dns_namelist_t answerlist;
	isc_job_cb     cb;
	void	      *arg;
} dns_clientresume_t; /* too long? */

isc_result_t
dns_client_create(isc_mem_t *mctx, unsigned int options,
		  isc_tlsctx_cache_t *tlsctx_client_cache,
		  dns_client_t **clientp, const isc_sockaddr_t *localaddr4,
		  const isc_sockaddr_t *localaddr6);
/*%<
 * Create a DNS client object with minimal internal resources, such as
 * a default view for the IN class and IPv4/IPv6 dispatches for the view.
 *
 * dns_client_create() takes 'manager' arguments so that the caller can
 * control the behavior of the client through the underlying event framework.
 * 'localaddr4' and 'localaddr6' specify the local addresses to use for
 * each address family; if both are set to NULL, then wildcard addresses
 * will be used for both families. If only one is NULL, then the other
 * address will be used as the local address, and the NULL protocol family
 * will not be used.
 *
 * Requires:
 *
 *\li	'mctx' is a valid memory context.
 *
 *\li	'loopmgr' is a valid loop manager.

 *\li	'nm' is a valid network manager.
 *
 *\li	'tlsctx_client_cache' is a valid TLS context cache.
 *
 *\li	clientp != NULL && *clientp == NULL.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS				On success.
 *
 *\li	Anything else				Failure.
 */

void
dns_client_detach(dns_client_t **clientp);
/*%<
 * Detach 'client' and destroy it if there are no more references.
 *
 * Requires:
 *
 *\li	'*clientp' is a valid client.
 *
 * Ensures:
 *
 *\li	*clientp == NULL.
 */

isc_result_t
dns_client_setservers(dns_client_t *client, dns_rdataclass_t rdclass,
		      const dns_name_t *name_space, isc_sockaddrlist_t *addrs);
/*%<
 * Specify a list of addresses of recursive name servers that the client will
 * use for name resolution.  A view for the 'rdclass' class must be created
 * beforehand.  If 'name_space' is non NULL, the specified server will be used
 * if and only if the query name is a subdomain of 'name_space'.  When servers
 * for multiple 'name_space's are provided, and a query name is covered by
 * more than one 'name_space', the servers for the best (longest) matching
 * name_space will be used.  If 'name_space' is NULL, it works as if
 * dns_rootname (.) were specified.
 *
 * Requires:
 *
 *\li	'client' is a valid client.
 *
 *\li	'name_space' is NULL or a valid name.
 *
 *\li	'addrs' != NULL.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS				On success.
 *
 *\li	Anything else				Failure.
 */

void
dns_client_setmaxrestarts(dns_client_t *client, uint8_t max_restarts);
/*%<
 * Set the number of permissible chained queries before we give up,
 * to prevent CNAME loops. This defaults to 11.
 *
 * Requires:
 *
 *\li	'client' is a valid client.

 *\li	'max_restarts' is greater than 0.
 */

void
dns_client_setmaxqueries(dns_client_t *client, uint8_t max_queries);
/*%<
 * Set the number of permissible outgoing queries before we give up,
 * This defaults to 200.
 *
 * Requires:
 *
 *\li	'client' is a valid client.

 *\li	'max_queries' is greater than 0.
 */

typedef void (*dns_client_resolve_cb)(dns_client_t     *client,
				      const dns_name_t *name,
				      dns_namelist_t   *namelist,
				      isc_result_t	result);

isc_result_t
dns_client_resolve(dns_client_t *client, const dns_name_t *name,
		   dns_rdataclass_t rdclass, dns_rdatatype_t type,
		   unsigned int options, dns_namelist_t *namelist,
		   dns_client_resolve_cb resolve_cb);

/*%<
 * Perform name resolution for 'name', 'rdclass', and 'type'.
 *
 * If any trusted keys are configured and the query name is considered to
 * belong to a secure zone, these functions also validate the responses
 * using DNSSEC by default.  If the DNS_CLIENTRESOPT_NOVALIDATE flag is set
 * in 'options', DNSSEC validation is disabled regardless of the configured
 * trusted keys or the query name. With DNS_CLIENTRESOPT_NODNSSEC
 * DNSSEC data is not returned with response. DNS_CLIENTRESOPT_NOCDFLAG
 * disables the CD flag on queries, DNS_CLIENTRESOPT_TCP switches to
 * the TCP (vs. UDP) transport.
 *
 * dns_client_resolve() provides a synchronous service.  This function starts
 * name resolution internally and blocks until it completes.  On success,
 * 'namelist' will contain a list of answer names, each of which has
 * corresponding RRsets.  The caller must provide a valid empty list, and
 * is responsible for freeing the list content via dns_client_freeresanswer().
 * If the name resolution fails due to an error in DNSSEC validation,
 * dns_client_resolve() returns the result code indicating the validation
 * error. Otherwise, it returns the result code of the entire resolution
 * process, either success or failure.
 *
 * It is expected that the client object passed to dns_client_resolve() was
 * created via dns_client_create() and has external managers and contexts.
 *
 * Requires:
 *
 *\li	'client' is a valid client.
 *
 *\li	'addrs' != NULL.
 *
 *\li	'name' is a valid name.
 *
 *\li	'namelist' != NULL and is not empty.
 *
 *\li	'transp' != NULL && *transp == NULL;
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS				On success.
 *
 *\li	Anything else				Failure.
 */

void
dns_client_freeresanswer(dns_client_t *client, dns_namelist_t *namelist);
/*%<
 * Free resources allocated for the content of 'namelist'.
 *
 * Requires:
 *
 *\li	'client' is a valid client.
 *
 *\li	'namelist' != NULL.
 */

isc_result_t
dns_client_addtrustedkey(dns_client_t *client, dns_rdataclass_t rdclass,
			 dns_rdatatype_t rdtype, const dns_name_t *keyname,
			 isc_buffer_t *keydatabuf);
/*%<
 * Add a DNSSEC trusted key for the 'rdclass' class (only class 'IN' is
 * currently supported).  A view for the 'rdclass' class must be created
 * beforehand.  'rdtype' is the type of the RR data for the key, either
 * DNSKEY or DS.  'keyname' is the DNS name of the key, and 'keydatabuf'
 * stores the RR data.
 *
 * Requires:
 *
 *\li	'client' is a valid client.
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
