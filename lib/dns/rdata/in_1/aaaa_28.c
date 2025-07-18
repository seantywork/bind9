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

/* RFC1886 */

#ifndef RDATA_IN_1_AAAA_28_C
#define RDATA_IN_1_AAAA_28_C

#include <isc/net.h>

#define RRTYPE_AAAA_ATTRIBUTES (0)

static isc_result_t
fromtext_in_aaaa(ARGS_FROMTEXT) {
	isc_token_t token;
	unsigned char addr[16];
	isc_region_t region;

	REQUIRE(type == dns_rdatatype_aaaa);
	REQUIRE(rdclass == dns_rdataclass_in);

	UNUSED(type);
	UNUSED(origin);
	UNUSED(options);
	UNUSED(rdclass);
	UNUSED(callbacks);

	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_string,
				      false));

	if (inet_pton(AF_INET6, DNS_AS_STR(token), addr) != 1) {
		RETTOK(DNS_R_BADAAAA);
	}
	isc_buffer_availableregion(target, &region);
	if (region.length < 16) {
		return ISC_R_NOSPACE;
	}
	memmove(region.base, addr, 16);
	isc_buffer_add(target, 16);
	return ISC_R_SUCCESS;
}

static isc_result_t
totext_in_aaaa(ARGS_TOTEXT) {
	isc_region_t region;

	REQUIRE(rdata->type == dns_rdatatype_aaaa);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);
	REQUIRE(rdata->length == 16);

	if ((tctx->flags & DNS_STYLEFLAG_EXPANDAAAA) != 0) {
		char buf[5 * 8];
		const char *sep = "";
		int i, n;
		unsigned int len = 0;

		for (i = 0; i < 16; i += 2) {
			INSIST(len < sizeof(buf));
			n = snprintf(buf + len, sizeof(buf) - len, "%s%02x%02x",
				     sep, rdata->data[i], rdata->data[i + 1]);
			if (n < 0) {
				return ISC_R_FAILURE;
			}
			len += n;
			sep = ":";
		}
		return str_totext(buf, target);
	}
	dns_rdata_toregion(rdata, &region);
	return inet_totext(AF_INET6, tctx->flags, &region, target);
}

static isc_result_t
fromwire_in_aaaa(ARGS_FROMWIRE) {
	isc_region_t sregion;
	isc_region_t tregion;

	REQUIRE(type == dns_rdatatype_aaaa);
	REQUIRE(rdclass == dns_rdataclass_in);

	UNUSED(type);
	UNUSED(dctx);
	UNUSED(rdclass);

	isc_buffer_activeregion(source, &sregion);
	isc_buffer_availableregion(target, &tregion);
	if (sregion.length < 16) {
		return ISC_R_UNEXPECTEDEND;
	}
	if (tregion.length < 16) {
		return ISC_R_NOSPACE;
	}

	memmove(tregion.base, sregion.base, 16);
	isc_buffer_forward(source, 16);
	isc_buffer_add(target, 16);
	return ISC_R_SUCCESS;
}

static isc_result_t
towire_in_aaaa(ARGS_TOWIRE) {
	isc_region_t region;

	UNUSED(cctx);

	REQUIRE(rdata->type == dns_rdatatype_aaaa);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);
	REQUIRE(rdata->length == 16);

	isc_buffer_availableregion(target, &region);
	if (region.length < rdata->length) {
		return ISC_R_NOSPACE;
	}
	memmove(region.base, rdata->data, rdata->length);
	isc_buffer_add(target, 16);
	return ISC_R_SUCCESS;
}

static int
compare_in_aaaa(ARGS_COMPARE) {
	isc_region_t r1;
	isc_region_t r2;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_aaaa);
	REQUIRE(rdata1->rdclass == dns_rdataclass_in);
	REQUIRE(rdata1->length == 16);
	REQUIRE(rdata2->length == 16);

	dns_rdata_toregion(rdata1, &r1);
	dns_rdata_toregion(rdata2, &r2);
	return isc_region_compare(&r1, &r2);
}

static isc_result_t
fromstruct_in_aaaa(ARGS_FROMSTRUCT) {
	dns_rdata_in_aaaa_t *aaaa = source;

	REQUIRE(type == dns_rdatatype_aaaa);
	REQUIRE(rdclass == dns_rdataclass_in);
	REQUIRE(aaaa != NULL);
	REQUIRE(aaaa->common.rdtype == type);
	REQUIRE(aaaa->common.rdclass == rdclass);

	UNUSED(type);
	UNUSED(rdclass);

	return mem_tobuffer(target, aaaa->in6_addr.s6_addr, 16);
}

static isc_result_t
tostruct_in_aaaa(ARGS_TOSTRUCT) {
	dns_rdata_in_aaaa_t *aaaa = target;
	isc_region_t r;

	REQUIRE(rdata->type == dns_rdatatype_aaaa);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);
	REQUIRE(aaaa != NULL);
	REQUIRE(rdata->length == 16);

	UNUSED(mctx);

	aaaa->common.rdclass = rdata->rdclass;
	aaaa->common.rdtype = rdata->type;

	dns_rdata_toregion(rdata, &r);
	INSIST(r.length == 16);
	memmove(aaaa->in6_addr.s6_addr, r.base, 16);

	return ISC_R_SUCCESS;
}

static void
freestruct_in_aaaa(ARGS_FREESTRUCT) {
	dns_rdata_in_aaaa_t *aaaa = source;

	REQUIRE(aaaa != NULL);
	REQUIRE(aaaa->common.rdclass == dns_rdataclass_in);
	REQUIRE(aaaa->common.rdtype == dns_rdatatype_aaaa);

	UNUSED(aaaa);
}

static isc_result_t
additionaldata_in_aaaa(ARGS_ADDLDATA) {
	REQUIRE(rdata->type == dns_rdatatype_aaaa);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(add);
	UNUSED(arg);

	return ISC_R_SUCCESS;
}

static isc_result_t
digest_in_aaaa(ARGS_DIGEST) {
	isc_region_t r;

	REQUIRE(rdata->type == dns_rdatatype_aaaa);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);

	dns_rdata_toregion(rdata, &r);

	return (digest)(arg, &r);
}

static bool
checkowner_in_aaaa(ARGS_CHECKOWNER) {
	dns_name_t prefix, suffix;

	REQUIRE(type == dns_rdatatype_aaaa);
	REQUIRE(rdclass == dns_rdataclass_in);

	UNUSED(type);
	UNUSED(rdclass);

	/*
	 * Handle Active Directory gc._msdcs.<forest> name.
	 */
	if (dns_name_countlabels(name) > 2U) {
		dns_name_init(&prefix);
		dns_name_init(&suffix);
		dns_name_split(name, dns_name_countlabels(name) - 2, &prefix,
			       &suffix);
		if (dns_name_equal(&gc_msdcs, &prefix) &&
		    dns_name_ishostname(&suffix, false))
		{
			return true;
		}
	}

	return dns_name_ishostname(name, wildcard);
}

static bool
checknames_in_aaaa(ARGS_CHECKNAMES) {
	REQUIRE(rdata->type == dns_rdatatype_aaaa);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(bad);

	return true;
}

static int
casecompare_in_aaaa(ARGS_COMPARE) {
	return compare_in_aaaa(rdata1, rdata2);
}
#endif /* RDATA_IN_1_AAAA_28_C */
