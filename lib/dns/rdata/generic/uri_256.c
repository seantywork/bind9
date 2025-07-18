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

#ifndef GENERIC_URI_256_C
#define GENERIC_URI_256_C 1

#define RRTYPE_URI_ATTRIBUTES (0)

static isc_result_t
fromtext_uri(ARGS_FROMTEXT) {
	isc_token_t token;

	REQUIRE(type == dns_rdatatype_uri);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(origin);
	UNUSED(options);
	UNUSED(callbacks);

	/*
	 * Priority
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	if (token.value.as_ulong > 0xffffU) {
		RETTOK(ISC_R_RANGE);
	}
	RETERR(uint16_tobuffer(token.value.as_ulong, target));

	/*
	 * Weight
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	if (token.value.as_ulong > 0xffffU) {
		RETTOK(ISC_R_RANGE);
	}
	RETERR(uint16_tobuffer(token.value.as_ulong, target));

	/*
	 * Target URI
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_qstring,
				      false));
	if (token.type != isc_tokentype_qstring) {
		RETTOK(DNS_R_SYNTAX);
	}
	RETTOK(multitxt_fromtext(&token.value.as_textregion, target));
	return ISC_R_SUCCESS;
}

static isc_result_t
totext_uri(ARGS_TOTEXT) {
	isc_region_t region;
	unsigned short priority, weight;
	char buf[sizeof("65000 ")];

	UNUSED(tctx);

	REQUIRE(rdata->type == dns_rdatatype_uri);
	REQUIRE(rdata->length != 0);

	dns_rdata_toregion(rdata, &region);

	/*
	 * Priority
	 */
	priority = uint16_fromregion(&region);
	isc_region_consume(&region, 2);
	snprintf(buf, sizeof(buf), "%u ", priority);
	RETERR(str_totext(buf, target));

	/*
	 * Weight
	 */
	weight = uint16_fromregion(&region);
	isc_region_consume(&region, 2);
	snprintf(buf, sizeof(buf), "%u ", weight);
	RETERR(str_totext(buf, target));

	/*
	 * Target URI
	 */
	RETERR(multitxt_totext(&region, target));
	return ISC_R_SUCCESS;
}

static isc_result_t
fromwire_uri(ARGS_FROMWIRE) {
	isc_region_t region;

	REQUIRE(type == dns_rdatatype_uri);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(dctx);

	/*
	 * Priority, weight
	 */
	isc_buffer_activeregion(source, &region);
	if (region.length < 4) {
		return ISC_R_UNEXPECTEDEND;
	}

	/*
	 * Priority, weight and target URI
	 */
	isc_buffer_forward(source, region.length);
	return mem_tobuffer(target, region.base, region.length);
}

static isc_result_t
towire_uri(ARGS_TOWIRE) {
	isc_region_t region;

	REQUIRE(rdata->type == dns_rdatatype_uri);
	REQUIRE(rdata->length != 0);

	UNUSED(cctx);

	dns_rdata_toregion(rdata, &region);
	return mem_tobuffer(target, region.base, region.length);
}

static int
compare_uri(ARGS_COMPARE) {
	isc_region_t r1;
	isc_region_t r2;
	int order;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_uri);
	REQUIRE(rdata1->length != 0);
	REQUIRE(rdata2->length != 0);

	dns_rdata_toregion(rdata1, &r1);
	dns_rdata_toregion(rdata2, &r2);

	/*
	 * Priority
	 */
	order = memcmp(r1.base, r2.base, 2);
	if (order != 0) {
		return order < 0 ? -1 : 1;
	}
	isc_region_consume(&r1, 2);
	isc_region_consume(&r2, 2);

	/*
	 * Weight
	 */
	order = memcmp(r1.base, r2.base, 2);
	if (order != 0) {
		return order < 0 ? -1 : 1;
	}
	isc_region_consume(&r1, 2);
	isc_region_consume(&r2, 2);

	return isc_region_compare(&r1, &r2);
}

static isc_result_t
fromstruct_uri(ARGS_FROMSTRUCT) {
	dns_rdata_uri_t *uri = source;

	REQUIRE(type == dns_rdatatype_uri);
	REQUIRE(uri != NULL);
	REQUIRE(uri->common.rdtype == type);
	REQUIRE(uri->common.rdclass == rdclass);
	REQUIRE(uri->target != NULL && uri->tgt_len != 0);

	UNUSED(type);
	UNUSED(rdclass);

	/*
	 * Priority
	 */
	RETERR(uint16_tobuffer(uri->priority, target));

	/*
	 * Weight
	 */
	RETERR(uint16_tobuffer(uri->weight, target));

	/*
	 * Target URI
	 */
	return mem_tobuffer(target, uri->target, uri->tgt_len);
}

static isc_result_t
tostruct_uri(ARGS_TOSTRUCT) {
	dns_rdata_uri_t *uri = target;
	isc_region_t sr;

	REQUIRE(rdata->type == dns_rdatatype_uri);
	REQUIRE(uri != NULL);
	REQUIRE(rdata->length >= 4);

	uri->common.rdclass = rdata->rdclass;
	uri->common.rdtype = rdata->type;

	dns_rdata_toregion(rdata, &sr);

	/*
	 * Priority
	 */
	uri->priority = uint16_fromregion(&sr);
	isc_region_consume(&sr, 2);

	/*
	 * Weight
	 */
	uri->weight = uint16_fromregion(&sr);
	isc_region_consume(&sr, 2);

	/*
	 * Target URI
	 */
	uri->tgt_len = sr.length;
	uri->target = mem_maybedup(mctx, sr.base, sr.length);
	uri->mctx = mctx;
	return ISC_R_SUCCESS;
}

static void
freestruct_uri(ARGS_FREESTRUCT) {
	dns_rdata_uri_t *uri = (dns_rdata_uri_t *)source;

	REQUIRE(uri != NULL);
	REQUIRE(uri->common.rdtype == dns_rdatatype_uri);

	if (uri->mctx == NULL) {
		return;
	}

	if (uri->target != NULL) {
		isc_mem_free(uri->mctx, uri->target);
	}
	uri->mctx = NULL;
}

static isc_result_t
additionaldata_uri(ARGS_ADDLDATA) {
	REQUIRE(rdata->type == dns_rdatatype_uri);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(add);
	UNUSED(arg);

	return ISC_R_SUCCESS;
}

static isc_result_t
digest_uri(ARGS_DIGEST) {
	isc_region_t r;

	REQUIRE(rdata->type == dns_rdatatype_uri);

	dns_rdata_toregion(rdata, &r);

	return (digest)(arg, &r);
}

static bool
checkowner_uri(ARGS_CHECKOWNER) {
	REQUIRE(type == dns_rdatatype_uri);

	UNUSED(name);
	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(wildcard);

	return true;
}

static bool
checknames_uri(ARGS_CHECKNAMES) {
	REQUIRE(rdata->type == dns_rdatatype_uri);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(bad);

	return true;
}

static int
casecompare_uri(ARGS_COMPARE) {
	return compare_uri(rdata1, rdata2);
}

#endif /* GENERIC_URI_256_C */
