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

#ifndef GENERIC_CAA_257_C
#define GENERIC_CAA_257_C 1

#define RRTYPE_CAA_ATTRIBUTES (0)

static unsigned char const alphanumeric[256] = {
	/* 0x00-0x0f */ 0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	/* 0x10-0x1f */ 0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	/* 0x20-0x2f */ 0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	/* 0x30-0x3f */ 1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	0,
	0,
	0,
	0,
	0,
	0,
	/* 0x40-0x4f */ 0,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	/* 0x50-0x5f */ 1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	0,
	0,
	0,
	0,
	0,
	/* 0x60-0x6f */ 0,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	/* 0x70-0x7f */ 1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	1,
	0,
	0,
	0,
	0,
	0,
	/* 0x80-0x8f */ 0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	/* 0x90-0x9f */ 0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	/* 0xa0-0xaf */ 0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	/* 0xb0-0xbf */ 0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	/* 0xc0-0xcf */ 0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	/* 0xd0-0xdf */ 0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	/* 0xe0-0xef */ 0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	/* 0xf0-0xff */ 0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
};

static isc_result_t
fromtext_caa(ARGS_FROMTEXT) {
	isc_token_t token;
	isc_textregion_t tr;
	uint8_t flags;
	unsigned int i;

	REQUIRE(type == dns_rdatatype_caa);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(origin);
	UNUSED(options);
	UNUSED(callbacks);

	/* Flags. */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	if (token.value.as_ulong > 255U) {
		RETTOK(ISC_R_RANGE);
	}
	flags = (uint8_t)(token.value.as_ulong & 255U);
	RETERR(uint8_tobuffer(flags, target));

	/*
	 * Tag
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_string,
				      false));
	tr = token.value.as_textregion;
	for (i = 0; i < tr.length; i++) {
		if (!alphanumeric[(unsigned char)tr.base[i]]) {
			RETTOK(DNS_R_SYNTAX);
		}
	}
	RETERR(uint8_tobuffer(tr.length, target));
	RETERR(mem_tobuffer(target, tr.base, tr.length));

	/*
	 * Value
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_qstring,
				      false));
	if (token.type != isc_tokentype_qstring &&
	    token.type != isc_tokentype_string)
	{
		RETERR(DNS_R_SYNTAX);
	}
	RETERR(multitxt_fromtext(&token.value.as_textregion, target));
	return ISC_R_SUCCESS;
}

static isc_result_t
totext_caa(ARGS_TOTEXT) {
	isc_region_t region;
	uint8_t flags;
	char buf[256];

	UNUSED(tctx);

	REQUIRE(rdata->type == dns_rdatatype_caa);
	REQUIRE(rdata->length >= 3U);
	REQUIRE(rdata->data != NULL);

	dns_rdata_toregion(rdata, &region);

	/*
	 * Flags
	 */
	flags = uint8_consume_fromregion(&region);
	snprintf(buf, sizeof(buf), "%u ", flags);
	RETERR(str_totext(buf, target));

	/*
	 * Tag
	 */
	RETERR(txt_totext(&region, false, target));
	RETERR(str_totext(" ", target));

	/*
	 * Value
	 */
	RETERR(multitxt_totext(&region, target));
	return ISC_R_SUCCESS;
}

static isc_result_t
fromwire_caa(ARGS_FROMWIRE) {
	isc_region_t sr;
	unsigned int len, i;

	REQUIRE(type == dns_rdatatype_caa);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(dctx);

	/*
	 * Flags
	 */
	isc_buffer_activeregion(source, &sr);
	if (sr.length < 2) {
		return ISC_R_UNEXPECTEDEND;
	}

	/*
	 * Flags, tag length
	 */
	RETERR(mem_tobuffer(target, sr.base, 2));
	len = sr.base[1];
	isc_region_consume(&sr, 2);
	isc_buffer_forward(source, 2);

	/*
	 * Zero length tag fields are illegal.
	 */
	if (sr.length < len || len == 0) {
		RETERR(DNS_R_FORMERR);
	}

	/* Check the Tag's value */
	for (i = 0; i < len; i++) {
		if (!alphanumeric[sr.base[i]]) {
			RETERR(DNS_R_FORMERR);
			/*
			 * Tag + Value
			 */
		}
	}
	/*
	 * Tag + Value
	 */
	isc_buffer_forward(source, sr.length);
	return mem_tobuffer(target, sr.base, sr.length);
}

static isc_result_t
towire_caa(ARGS_TOWIRE) {
	isc_region_t region;

	REQUIRE(rdata->type == dns_rdatatype_caa);
	REQUIRE(rdata->length >= 3U);
	REQUIRE(rdata->data != NULL);

	UNUSED(cctx);

	dns_rdata_toregion(rdata, &region);
	return mem_tobuffer(target, region.base, region.length);
}

static int
compare_caa(ARGS_COMPARE) {
	isc_region_t r1, r2;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_caa);
	REQUIRE(rdata1->length >= 3U);
	REQUIRE(rdata2->length >= 3U);
	REQUIRE(rdata1->data != NULL);
	REQUIRE(rdata2->data != NULL);

	dns_rdata_toregion(rdata1, &r1);
	dns_rdata_toregion(rdata2, &r2);
	return isc_region_compare(&r1, &r2);
}

static isc_result_t
fromstruct_caa(ARGS_FROMSTRUCT) {
	dns_rdata_caa_t *caa = source;
	isc_region_t region;
	unsigned int i;

	REQUIRE(type == dns_rdatatype_caa);
	REQUIRE(caa != NULL);
	REQUIRE(caa->common.rdtype == type);
	REQUIRE(caa->common.rdclass == rdclass);
	REQUIRE(caa->tag != NULL && caa->tag_len != 0);
	REQUIRE(caa->value != NULL);

	UNUSED(type);
	UNUSED(rdclass);

	/*
	 * Flags
	 */
	RETERR(uint8_tobuffer(caa->flags, target));

	/*
	 * Tag length
	 */
	RETERR(uint8_tobuffer(caa->tag_len, target));

	/*
	 * Tag
	 */
	region.base = caa->tag;
	region.length = caa->tag_len;
	for (i = 0; i < region.length; i++) {
		if (!alphanumeric[region.base[i]]) {
			RETERR(DNS_R_SYNTAX);
		}
	}
	RETERR(isc_buffer_copyregion(target, &region));

	/*
	 * Value
	 */
	region.base = caa->value;
	region.length = caa->value_len;
	return isc_buffer_copyregion(target, &region);
}

static isc_result_t
tostruct_caa(ARGS_TOSTRUCT) {
	dns_rdata_caa_t *caa = target;
	isc_region_t sr;

	REQUIRE(rdata->type == dns_rdatatype_caa);
	REQUIRE(caa != NULL);
	REQUIRE(rdata->length >= 3U);
	REQUIRE(rdata->data != NULL);

	caa->common.rdclass = rdata->rdclass;
	caa->common.rdtype = rdata->type;

	dns_rdata_toregion(rdata, &sr);

	/*
	 * Flags
	 */
	caa->flags = uint8_fromregion(&sr);
	isc_region_consume(&sr, 1);

	/*
	 * Tag length
	 */
	caa->tag_len = uint8_fromregion(&sr);
	isc_region_consume(&sr, 1);

	/*
	 * Tag
	 */
	INSIST(sr.length >= caa->tag_len);
	caa->tag = mem_maybedup(mctx, sr.base, caa->tag_len);
	isc_region_consume(&sr, caa->tag_len);

	/*
	 * Value
	 */
	caa->value_len = sr.length;
	caa->value = mem_maybedup(mctx, sr.base, sr.length);

	caa->mctx = mctx;
	return ISC_R_SUCCESS;
}

static void
freestruct_caa(ARGS_FREESTRUCT) {
	dns_rdata_caa_t *caa = (dns_rdata_caa_t *)source;

	REQUIRE(caa != NULL);
	REQUIRE(caa->common.rdtype == dns_rdatatype_caa);

	if (caa->mctx == NULL) {
		return;
	}

	if (caa->tag != NULL) {
		isc_mem_free(caa->mctx, caa->tag);
	}
	if (caa->value != NULL) {
		isc_mem_free(caa->mctx, caa->value);
	}
	caa->mctx = NULL;
}

static isc_result_t
additionaldata_caa(ARGS_ADDLDATA) {
	REQUIRE(rdata->type == dns_rdatatype_caa);
	REQUIRE(rdata->data != NULL);
	REQUIRE(rdata->length >= 3U);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(add);
	UNUSED(arg);

	return ISC_R_SUCCESS;
}

static isc_result_t
digest_caa(ARGS_DIGEST) {
	isc_region_t r;

	REQUIRE(rdata->type == dns_rdatatype_caa);
	REQUIRE(rdata->data != NULL);
	REQUIRE(rdata->length >= 3U);

	dns_rdata_toregion(rdata, &r);

	return (digest)(arg, &r);
}

static bool
checkowner_caa(ARGS_CHECKOWNER) {
	REQUIRE(type == dns_rdatatype_caa);

	UNUSED(name);
	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(wildcard);

	return true;
}

static bool
checknames_caa(ARGS_CHECKNAMES) {
	REQUIRE(rdata->type == dns_rdatatype_caa);
	REQUIRE(rdata->data != NULL);
	REQUIRE(rdata->length >= 3U);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(bad);

	return true;
}

static int
casecompare_caa(ARGS_COMPARE) {
	return compare_caa(rdata1, rdata2);
}

#endif /* GENERIC_CAA_257_C */
