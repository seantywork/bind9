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

#include <stdio.h>
#include <stdlib.h>

#include <isc/buffer.h>
#include <isc/lib.h>
#include <isc/mem.h>
#include <isc/region.h>
#include <isc/stdio.h>
#include <isc/string.h>
#include <isc/util.h>

#include <dns/lib.h>

#define DST_KEY_INTERNAL

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/rsa.h>

#include <isc/log.h>
#include <isc/result.h>

#include <dns/dnssec.h>
#include <dns/fixedname.h>
#include <dns/keyvalues.h>
#include <dns/name.h>
#include <dns/rdataclass.h>
#include <dns/secalg.h>

#include <dst/dst.h>

dst_key_t *key;
dns_fixedname_t fname;
dns_name_t *name;
unsigned int bits = 2048U;
isc_mem_t *mctx;
isc_logconfig_t *logconfig;
int level = ISC_LOG_WARNING;
char filename[255];
isc_result_t result;
isc_buffer_t buf;
RSA *rsa;
BIGNUM *e;
EVP_PKEY *pkey;

#define CHECK(op, msg)                                                        \
	do {                                                                  \
		result = (op);                                                \
		if (result != ISC_R_SUCCESS) {                                \
			fprintf(stderr,                                       \
				"fatal error: %s returns %s at file %s line " \
				"%d\n",                                       \
				msg, isc_result_totext(result), __FILE__,     \
				__LINE__);                                    \
			ERR_clear_error();                                    \
			exit(EXIT_FAILURE);                                   \
		}                                                             \
	} while (0)

int
main(int argc, char **argv) {
	UNUSED(argc);
	UNUSED(argv);

	rsa = RSA_new();
	e = BN_new();
	pkey = EVP_PKEY_new();

	if ((rsa == NULL) || (e == NULL) || (pkey == NULL) ||
	    !EVP_PKEY_set1_RSA(pkey, rsa))
	{
		fprintf(stderr, "fatal error: basic OpenSSL failure\n");
		ERR_clear_error();
		exit(EXIT_FAILURE);
	}

	/* e = 0x1000000000001 */
	BN_set_bit(e, 0);
	BN_set_bit(e, 48);

	if (RSA_generate_key_ex(rsa, bits, e, NULL)) {
		BN_free(e);
		RSA_free(rsa);
	} else {
		fprintf(stderr,
			"fatal error: RSA_generate_key_ex() fails "
			"at file %s line %d\n",
			__FILE__, __LINE__);
		ERR_clear_error();
		exit(EXIT_FAILURE);
	}

	isc_mem_create(argv[0], &mctx);

	logconfig = isc_logconfig_get();
	isc_log_settag(logconfig, "bigkey");

	isc_log_createandusechannel(
		logconfig, "default_stderr", ISC_LOG_TOFILEDESC, level,
		ISC_LOGDESTINATION_STDERR,
		ISC_LOG_PRINTTAG | ISC_LOG_PRINTLEVEL, ISC_LOGCATEGORY_DEFAULT,
		ISC_LOGMODULE_DEFAULT);

	name = dns_fixedname_initname(&fname);
	isc_buffer_constinit(&buf, "example.", strlen("example."));
	isc_buffer_add(&buf, strlen("example."));
	CHECK(dns_name_fromtext(name, &buf, dns_rootname, 0, NULL), "dns_name_"
								    "fromtext("
								    "\"example."
								    "\")");

	CHECK(dst_key_buildinternal(name, DNS_KEYALG_RSASHA256, bits,
				    DNS_KEYOWNER_ZONE, DNS_KEYPROTO_DNSSEC,
				    dns_rdataclass_in, pkey, mctx, &key),
	      "dst_key_buildinternal(...)");

	CHECK(dst_key_tofile(key, DST_TYPE_PRIVATE | DST_TYPE_PUBLIC, NULL),
	      "dst_key_tofile()");
	isc_buffer_init(&buf, filename, sizeof(filename) - 1);
	isc_buffer_clear(&buf);
	CHECK(dst_key_buildfilename(key, 0, NULL, &buf), "dst_key_"
							 "buildfilename()");
	printf("%s\n", filename);
	dst_key_free(&key);

	isc_mem_detach(&mctx);
	return 0;
}

/*! \file */
