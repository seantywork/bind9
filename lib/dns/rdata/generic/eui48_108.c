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

#ifndef RDATA_GENERIC_EUI48_108_C
#define RDATA_GENERIC_EUI48_108_C

#include <string.h>

#define RRTYPE_EUI48_ATTRIBUTES (0)

static isc_result_t
fromtext_eui48(ARGS_FROMTEXT) {
	isc_token_t token;
	unsigned char eui48[6];
	unsigned int l0, l1, l2, l3, l4, l5;
	int n;

	REQUIRE(type == dns_rdatatype_eui48);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(origin);
	UNUSED(options);
	UNUSED(callbacks);

	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_string,
				      false));
	n = sscanf(DNS_AS_STR(token), "%2x-%2x-%2x-%2x-%2x-%2x", &l0, &l1, &l2,
		   &l3, &l4, &l5);
	if (n != 6 || l0 > 255U || l1 > 255U || l2 > 255U || l3 > 255U ||
	    l4 > 255U || l5 > 255U)
	{
		return DNS_R_BADEUI;
	}

	eui48[0] = l0;
	eui48[1] = l1;
	eui48[2] = l2;
	eui48[3] = l3;
	eui48[4] = l4;
	eui48[5] = l5;
	return mem_tobuffer(target, eui48, sizeof(eui48));
}

static isc_result_t
totext_eui48(ARGS_TOTEXT) {
	char buf[sizeof("xx-xx-xx-xx-xx-xx")];

	REQUIRE(rdata->type == dns_rdatatype_eui48);
	REQUIRE(rdata->length == 6);

	UNUSED(tctx);

	(void)snprintf(buf, sizeof(buf), "%02x-%02x-%02x-%02x-%02x-%02x",
		       rdata->data[0], rdata->data[1], rdata->data[2],
		       rdata->data[3], rdata->data[4], rdata->data[5]);
	return str_totext(buf, target);
}

static isc_result_t
fromwire_eui48(ARGS_FROMWIRE) {
	isc_region_t sregion;

	REQUIRE(type == dns_rdatatype_eui48);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(dctx);

	isc_buffer_activeregion(source, &sregion);
	if (sregion.length != 6) {
		return DNS_R_FORMERR;
	}
	isc_buffer_forward(source, sregion.length);
	return mem_tobuffer(target, sregion.base, sregion.length);
}

static isc_result_t
towire_eui48(ARGS_TOWIRE) {
	REQUIRE(rdata->type == dns_rdatatype_eui48);
	REQUIRE(rdata->length == 6);

	UNUSED(cctx);

	return mem_tobuffer(target, rdata->data, rdata->length);
}

static int
compare_eui48(ARGS_COMPARE) {
	isc_region_t region1;
	isc_region_t region2;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_eui48);
	REQUIRE(rdata1->length == 6);
	REQUIRE(rdata2->length == 6);

	dns_rdata_toregion(rdata1, &region1);
	dns_rdata_toregion(rdata2, &region2);
	return isc_region_compare(&region1, &region2);
}

static isc_result_t
fromstruct_eui48(ARGS_FROMSTRUCT) {
	dns_rdata_eui48_t *eui48 = source;

	REQUIRE(type == dns_rdatatype_eui48);
	REQUIRE(eui48 != NULL);
	REQUIRE(eui48->common.rdtype == type);
	REQUIRE(eui48->common.rdclass == rdclass);

	UNUSED(type);
	UNUSED(rdclass);

	return mem_tobuffer(target, eui48->eui48, sizeof(eui48->eui48));
}

static isc_result_t
tostruct_eui48(ARGS_TOSTRUCT) {
	dns_rdata_eui48_t *eui48 = target;

	REQUIRE(rdata->type == dns_rdatatype_eui48);
	REQUIRE(eui48 != NULL);
	REQUIRE(rdata->length == 6);

	UNUSED(mctx);

	eui48->common.rdclass = rdata->rdclass;
	eui48->common.rdtype = rdata->type;

	memmove(eui48->eui48, rdata->data, rdata->length);
	return ISC_R_SUCCESS;
}

static void
freestruct_eui48(ARGS_FREESTRUCT) {
	dns_rdata_eui48_t *eui48 = source;

	REQUIRE(eui48 != NULL);
	REQUIRE(eui48->common.rdtype == dns_rdatatype_eui48);

	return;
}

static isc_result_t
additionaldata_eui48(ARGS_ADDLDATA) {
	REQUIRE(rdata->type == dns_rdatatype_eui48);
	REQUIRE(rdata->length == 6);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(add);
	UNUSED(arg);

	return ISC_R_SUCCESS;
}

static isc_result_t
digest_eui48(ARGS_DIGEST) {
	isc_region_t r;

	REQUIRE(rdata->type == dns_rdatatype_eui48);
	REQUIRE(rdata->length == 6);

	dns_rdata_toregion(rdata, &r);

	return (digest)(arg, &r);
}

static bool
checkowner_eui48(ARGS_CHECKOWNER) {
	REQUIRE(type == dns_rdatatype_eui48);

	UNUSED(name);
	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(wildcard);

	return true;
}

static bool
checknames_eui48(ARGS_CHECKNAMES) {
	REQUIRE(rdata->type == dns_rdatatype_eui48);
	REQUIRE(rdata->length == 6);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(bad);

	return true;
}

static int
casecompare_eui48(ARGS_COMPARE) {
	return compare_eui48(rdata1, rdata2);
}

#endif /* RDATA_GENERIC_EUI48_108_C */
