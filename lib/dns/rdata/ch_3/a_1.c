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

/* by Bjorn.Victor@it.uu.se, 2005-05-07 */
/* Based on generic/soa_6.c and generic/mx_15.c */

#ifndef RDATA_CH_3_A_1_C
#define RDATA_CH_3_A_1_C

#include <isc/net.h>

#define RRTYPE_A_ATTRIBUTES (0)

static isc_result_t
fromtext_ch_a(ARGS_FROMTEXT) {
	isc_token_t token;
	dns_fixedname_t fn;
	dns_name_t *name = dns_fixedname_initname(&fn);
	isc_buffer_t buffer;

	REQUIRE(type == dns_rdatatype_a);
	REQUIRE(rdclass == dns_rdataclass_ch); /* 3 */

	UNUSED(type);
	UNUSED(callbacks);

	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_string,
				      false));

	/* get domain name */
	buffer_fromregion(&buffer, &token.value.as_region);
	if (origin == NULL) {
		origin = dns_rootname;
	}
	RETTOK(dns_name_fromtext(name, &buffer, origin, options));
	RETTOK(dns_name_towire(name, NULL, target));
	if ((options & DNS_RDATA_CHECKNAMES) != 0 &&
	    (options & DNS_RDATA_CHECKREVERSE) != 0)
	{
		bool ok;
		ok = dns_name_ishostname(name, false);
		if (!ok && (options & DNS_RDATA_CHECKNAMESFAIL) != 0) {
			RETTOK(DNS_R_BADNAME);
		}
		if (!ok && callbacks != NULL) {
			warn_badname(name, lexer, callbacks);
		}
	}

	/* 16-bit octal address */
	RETERR(isc_lex_getoctaltoken(lexer, &token, false));
	if (token.value.as_ulong > 0xffffU) {
		RETTOK(ISC_R_RANGE);
	}
	return uint16_tobuffer(token.value.as_ulong, target);
}

static isc_result_t
totext_ch_a(ARGS_TOTEXT) {
	isc_region_t region;
	dns_name_t name;
	dns_name_t prefix;
	unsigned int opts;
	char buf[sizeof("0177777")];
	uint16_t addr;

	REQUIRE(rdata->type == dns_rdatatype_a);
	REQUIRE(rdata->rdclass == dns_rdataclass_ch); /* 3 */
	REQUIRE(rdata->length != 0);

	dns_name_init(&name);
	dns_name_init(&prefix);

	dns_rdata_toregion(rdata, &region);
	dns_name_fromregion(&name, &region);
	isc_region_consume(&region, name_length(&name));
	addr = uint16_fromregion(&region);

	opts = name_prefix(&name, tctx->origin, &prefix) ? DNS_NAME_OMITFINALDOT
							 : 0;
	RETERR(dns_name_totext(&prefix, opts, target));

	snprintf(buf, sizeof(buf), "%o", addr); /* note octal */
	RETERR(str_totext(" ", target));
	return str_totext(buf, target);
}

static isc_result_t
fromwire_ch_a(ARGS_FROMWIRE) {
	isc_region_t sregion;
	isc_region_t tregion;
	dns_name_t name;

	REQUIRE(type == dns_rdatatype_a);
	REQUIRE(rdclass == dns_rdataclass_ch);

	UNUSED(type);
	UNUSED(rdclass);

	dctx = dns_decompress_setpermitted(dctx, true);

	dns_name_init(&name);

	RETERR(dns_name_fromwire(&name, source, dctx, target));

	isc_buffer_activeregion(source, &sregion);
	isc_buffer_availableregion(target, &tregion);
	if (sregion.length < 2) {
		return ISC_R_UNEXPECTEDEND;
	}
	if (tregion.length < 2) {
		return ISC_R_NOSPACE;
	}

	memmove(tregion.base, sregion.base, 2);
	isc_buffer_forward(source, 2);
	isc_buffer_add(target, 2);

	return ISC_R_SUCCESS;
}

static isc_result_t
towire_ch_a(ARGS_TOWIRE) {
	dns_name_t name;
	isc_region_t sregion;
	isc_region_t tregion;

	REQUIRE(rdata->type == dns_rdatatype_a);
	REQUIRE(rdata->rdclass == dns_rdataclass_ch);
	REQUIRE(rdata->length != 0);

	dns_compress_setpermitted(cctx, true);

	dns_name_init(&name);

	dns_rdata_toregion(rdata, &sregion);

	dns_name_fromregion(&name, &sregion);
	isc_region_consume(&sregion, name_length(&name));
	RETERR(dns_name_towire(&name, cctx, target));

	isc_buffer_availableregion(target, &tregion);
	if (tregion.length < 2) {
		return ISC_R_NOSPACE;
	}

	memmove(tregion.base, sregion.base, 2);
	isc_buffer_add(target, 2);
	return ISC_R_SUCCESS;
}

static int
compare_ch_a(ARGS_COMPARE) {
	dns_name_t name1;
	dns_name_t name2;
	isc_region_t region1;
	isc_region_t region2;
	int order;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_a);
	REQUIRE(rdata1->rdclass == dns_rdataclass_ch);
	REQUIRE(rdata1->length != 0);
	REQUIRE(rdata2->length != 0);

	dns_name_init(&name1);
	dns_name_init(&name2);

	dns_rdata_toregion(rdata1, &region1);
	dns_rdata_toregion(rdata2, &region2);

	dns_name_fromregion(&name1, &region1);
	dns_name_fromregion(&name2, &region2);
	isc_region_consume(&region1, name_length(&name1));
	isc_region_consume(&region2, name_length(&name2));

	order = dns_name_rdatacompare(&name1, &name2);
	if (order != 0) {
		return order;
	}

	order = memcmp(region1.base, region2.base, 2);
	if (order != 0) {
		order = (order < 0) ? -1 : 1;
	}
	return order;
}

static isc_result_t
fromstruct_ch_a(ARGS_FROMSTRUCT) {
	dns_rdata_ch_a_t *a = source;
	isc_region_t region;

	REQUIRE(type == dns_rdatatype_a);
	REQUIRE(a != NULL);
	REQUIRE(a->common.rdtype == type);
	REQUIRE(a->common.rdclass == rdclass);

	UNUSED(type);
	UNUSED(rdclass);

	dns_name_toregion(&a->ch_addr_dom, &region);
	RETERR(isc_buffer_copyregion(target, &region));

	return uint16_tobuffer(ntohs(a->ch_addr), target);
}

static isc_result_t
tostruct_ch_a(ARGS_TOSTRUCT) {
	dns_rdata_ch_a_t *a = target;
	isc_region_t region;
	dns_name_t name;

	REQUIRE(rdata->type == dns_rdatatype_a);
	REQUIRE(rdata->rdclass == dns_rdataclass_ch);
	REQUIRE(rdata->length != 0);

	a->common.rdclass = rdata->rdclass;
	a->common.rdtype = rdata->type;

	dns_rdata_toregion(rdata, &region);

	dns_name_init(&name);
	dns_name_fromregion(&name, &region);
	isc_region_consume(&region, name_length(&name));

	dns_name_init(&a->ch_addr_dom);
	name_duporclone(&name, mctx, &a->ch_addr_dom);
	a->ch_addr = htons(uint16_fromregion(&region));
	a->mctx = mctx;
	return ISC_R_SUCCESS;
}

static void
freestruct_ch_a(ARGS_FREESTRUCT) {
	dns_rdata_ch_a_t *a = source;

	REQUIRE(a != NULL);
	REQUIRE(a->common.rdtype == dns_rdatatype_a);

	if (a->mctx == NULL) {
		return;
	}

	dns_name_free(&a->ch_addr_dom, a->mctx);
	a->mctx = NULL;
}

static isc_result_t
additionaldata_ch_a(ARGS_ADDLDATA) {
	REQUIRE(rdata->type == dns_rdatatype_a);
	REQUIRE(rdata->rdclass == dns_rdataclass_ch);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(add);
	UNUSED(arg);

	return ISC_R_SUCCESS;
}

static isc_result_t
digest_ch_a(ARGS_DIGEST) {
	isc_region_t r;
	dns_name_t name;

	REQUIRE(rdata->type == dns_rdatatype_a);
	REQUIRE(rdata->rdclass == dns_rdataclass_ch);

	dns_rdata_toregion(rdata, &r);
	dns_name_init(&name);
	dns_name_fromregion(&name, &r);
	isc_region_consume(&r, name_length(&name));
	RETERR(dns_name_digest(&name, digest, arg));
	return (digest)(arg, &r);
}

static bool
checkowner_ch_a(ARGS_CHECKOWNER) {
	REQUIRE(type == dns_rdatatype_a);
	REQUIRE(rdclass == dns_rdataclass_ch);

	UNUSED(type);

	return dns_name_ishostname(name, wildcard);
}

static bool
checknames_ch_a(ARGS_CHECKNAMES) {
	isc_region_t region;
	dns_name_t name;

	REQUIRE(rdata->type == dns_rdatatype_a);
	REQUIRE(rdata->rdclass == dns_rdataclass_ch);

	UNUSED(owner);

	dns_rdata_toregion(rdata, &region);
	dns_name_init(&name);
	dns_name_fromregion(&name, &region);
	if (!dns_name_ishostname(&name, false)) {
		if (bad != NULL) {
			dns_name_clone(&name, bad);
		}
		return false;
	}

	return true;
}

static int
casecompare_ch_a(ARGS_COMPARE) {
	return compare_ch_a(rdata1, rdata2);
}
#endif /* RDATA_CH_3_A_1_C */
