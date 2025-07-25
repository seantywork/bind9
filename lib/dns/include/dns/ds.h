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

#include <dns/rdatastruct.h>
#include <dns/types.h>

#define DNS_DSDIGEST_SHA1     (1)
#define DNS_DSDIGEST_SHA256   (2)
#define DNS_DSDIGEST_GOST     (3)
#define DNS_DSDIGEST_SHA384   (4)
#define DNS_DSDIGEST_GOST2012 (5)
#define DNS_DSDIGEST_SM3      (6)

#if TEST_PRIVATE_DSDIGEST
/*
 * Possible future digest types that encode the PRIVATEDNS and
 * PRIVATEOID identifiers before the cryptographic digest value.
 */
#define DNS_DSDIGEST_SHA256PRIVATE (7)
#define DNS_DSDIGEST_SHA384PRIVATE (8)
#define DNS_DSDIGEST_SM3PRIVATE	   (9)
#endif

#define DNS_DSDIGEST_MAX (255)

/*
 * Assuming SHA-384 digest type + maximal PRIVATEDNS name.
 */
#define DNS_DS_BUFFERSIZE (52 + 255)

isc_result_t
dns_ds_fromkeyrdata(const dns_name_t *owner, dns_rdata_t *key,
		    dns_dsdigest_t digest_type, unsigned char *digest,
		    size_t len, dns_rdata_ds_t *dsrdata);
/*%<
 * Build a DS rdata structure from a key.
 *
 * Requires:
 *\li	key	Points to a valid DNSKEY or CDNSKEY record.
 *\li	buffer	Points to a buffer of at least
 * 		#ISC_MAX_MD_SIZE bytes.
 */

isc_result_t
dns_ds_buildrdata(dns_name_t *owner, dns_rdata_t *key,
		  dns_dsdigest_t digest_type, unsigned char *buffer, size_t len,
		  dns_rdata_t *rdata);
/*%<
 * Similar to dns_ds_fromkeyrdata(), but copies the DS into a
 * dns_rdata object.
 *
 * Requires:
 *\li	key	Points to a valid DNSKEY or CDNSKEY record.
 *\li	buffer	Points to a buffer of at least
 * 		#DNS_DS_BUFFERSIZE bytes.
 *\li	rdata	Points to an initialized dns_rdata_t.
 *
 * Ensures:
 *  \li    *rdata	Contains a valid DS rdata.  The 'data' member refers
 *		to 'buffer'.
 */
