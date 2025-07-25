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

/* draft-ietf-dnsext-tkey-01.txt */

#ifndef RDATA_GENERIC_TKEY_249_C
#define RDATA_GENERIC_TKEY_249_C

#define RRTYPE_TKEY_ATTRIBUTES (DNS_RDATATYPEATTR_META)

static isc_result_t
fromtext_tkey(ARGS_FROMTEXT) {
	isc_token_t token;
	dns_rcode_t rcode;
	isc_buffer_t buffer;
	long i;
	char *e;

	REQUIRE(type == dns_rdatatype_tkey);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(callbacks);

	/*
	 * Algorithm.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_string,
				      false));
	buffer_fromregion(&buffer, &token.value.as_region);
	if (origin == NULL) {
		origin = dns_rootname;
	}
	RETTOK(dns_name_wirefromtext(&buffer, origin, options, target));

	/*
	 * Inception.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	RETERR(uint32_tobuffer(token.value.as_ulong, target));

	/*
	 * Expiration.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	RETERR(uint32_tobuffer(token.value.as_ulong, target));

	/*
	 * Mode.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	if (token.value.as_ulong > 0xffffU) {
		RETTOK(ISC_R_RANGE);
	}
	RETERR(uint16_tobuffer(token.value.as_ulong, target));

	/*
	 * Error.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_string,
				      false));
	if (dns_tsigrcode_fromtext(&rcode, &token.value.as_textregion) !=
	    ISC_R_SUCCESS)
	{
		i = strtol(DNS_AS_STR(token), &e, 10);
		if (*e != 0) {
			RETTOK(DNS_R_UNKNOWN);
		}
		if (i < 0 || i > 0xffff) {
			RETTOK(ISC_R_RANGE);
		}
		rcode = (dns_rcode_t)i;
	}
	RETERR(uint16_tobuffer(rcode, target));

	/*
	 * Key Size.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	if (token.value.as_ulong > 0xffffU) {
		RETTOK(ISC_R_RANGE);
	}
	RETERR(uint16_tobuffer(token.value.as_ulong, target));

	/*
	 * Key Data.
	 */
	RETERR(isc_base64_tobuffer(lexer, target, (int)token.value.as_ulong));

	/*
	 * Other Size.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	if (token.value.as_ulong > 0xffffU) {
		RETTOK(ISC_R_RANGE);
	}
	RETERR(uint16_tobuffer(token.value.as_ulong, target));

	/*
	 * Other Data.
	 */
	return isc_base64_tobuffer(lexer, target, (int)token.value.as_ulong);
}

static isc_result_t
totext_tkey(ARGS_TOTEXT) {
	isc_region_t sr, dr;
	char buf[sizeof("4294967295 ")];
	unsigned long n;
	dns_name_t name;
	dns_name_t prefix;
	unsigned int opts;

	REQUIRE(rdata->type == dns_rdatatype_tkey);
	REQUIRE(rdata->length != 0);

	dns_rdata_toregion(rdata, &sr);

	/*
	 * Algorithm.
	 */
	dns_name_init(&name);
	dns_name_init(&prefix);
	dns_name_fromregion(&name, &sr);
	opts = name_prefix(&name, tctx->origin, &prefix) ? DNS_NAME_OMITFINALDOT
							 : 0;
	RETERR(dns_name_totext(&prefix, opts, target));
	RETERR(str_totext(" ", target));
	isc_region_consume(&sr, name_length(&name));

	/*
	 * Inception.
	 */
	n = uint32_fromregion(&sr);
	isc_region_consume(&sr, 4);
	snprintf(buf, sizeof(buf), "%lu ", n);
	RETERR(str_totext(buf, target));

	/*
	 * Expiration.
	 */
	n = uint32_fromregion(&sr);
	isc_region_consume(&sr, 4);
	snprintf(buf, sizeof(buf), "%lu ", n);
	RETERR(str_totext(buf, target));

	/*
	 * Mode.
	 */
	n = uint16_fromregion(&sr);
	isc_region_consume(&sr, 2);
	snprintf(buf, sizeof(buf), "%lu ", n);
	RETERR(str_totext(buf, target));

	/*
	 * Error.
	 */
	n = uint16_fromregion(&sr);
	isc_region_consume(&sr, 2);
	if (dns_tsigrcode_totext((dns_rcode_t)n, target) == ISC_R_SUCCESS) {
		RETERR(str_totext(" ", target));
	} else {
		snprintf(buf, sizeof(buf), "%lu ", n);
		RETERR(str_totext(buf, target));
	}

	/*
	 * Key Size.
	 */
	n = uint16_fromregion(&sr);
	isc_region_consume(&sr, 2);
	snprintf(buf, sizeof(buf), "%lu", n);
	RETERR(str_totext(buf, target));

	/*
	 * Key Data.
	 */
	REQUIRE(n <= sr.length);
	dr = sr;
	dr.length = n;
	if ((tctx->flags & DNS_STYLEFLAG_MULTILINE) != 0) {
		RETERR(str_totext(" (", target));
	}
	RETERR(str_totext(tctx->linebreak, target));
	if (tctx->width == 0) { /* No splitting */
		RETERR(isc_base64_totext(&dr, 60, "", target));
	} else {
		RETERR(isc_base64_totext(&dr, tctx->width - 2, tctx->linebreak,
					 target));
	}
	if ((tctx->flags & DNS_STYLEFLAG_MULTILINE) != 0) {
		RETERR(str_totext(" ) ", target));
	} else {
		RETERR(str_totext(" ", target));
	}
	isc_region_consume(&sr, n);

	/*
	 * Other Size.
	 */
	n = uint16_fromregion(&sr);
	isc_region_consume(&sr, 2);
	snprintf(buf, sizeof(buf), "%lu", n);
	RETERR(str_totext(buf, target));

	/*
	 * Other Data.
	 */
	REQUIRE(n <= sr.length);
	if (n != 0U) {
		dr = sr;
		dr.length = n;
		if ((tctx->flags & DNS_STYLEFLAG_MULTILINE) != 0) {
			RETERR(str_totext(" (", target));
		}
		RETERR(str_totext(tctx->linebreak, target));
		if (tctx->width == 0) { /* No splitting */
			RETERR(isc_base64_totext(&dr, 60, "", target));
		} else {
			RETERR(isc_base64_totext(&dr, tctx->width - 2,
						 tctx->linebreak, target));
		}
		if ((tctx->flags & DNS_STYLEFLAG_MULTILINE) != 0) {
			RETERR(str_totext(" )", target));
		}
	}
	return ISC_R_SUCCESS;
}

static isc_result_t
fromwire_tkey(ARGS_FROMWIRE) {
	isc_region_t sr;
	unsigned long n;
	dns_name_t name;

	REQUIRE(type == dns_rdatatype_tkey);

	UNUSED(type);
	UNUSED(rdclass);

	dctx = dns_decompress_setpermitted(dctx, false);

	/*
	 * Algorithm.
	 */
	dns_name_init(&name);
	RETERR(dns_name_fromwire(&name, source, dctx, target));

	/*
	 * Inception: 4
	 * Expiration: 4
	 * Mode: 2
	 * Error: 2
	 */
	isc_buffer_activeregion(source, &sr);
	if (sr.length < 12) {
		return ISC_R_UNEXPECTEDEND;
	}
	RETERR(mem_tobuffer(target, sr.base, 12));
	isc_region_consume(&sr, 12);
	isc_buffer_forward(source, 12);

	/*
	 * Key Length + Key Data.
	 */
	if (sr.length < 2) {
		return ISC_R_UNEXPECTEDEND;
	}
	n = uint16_fromregion(&sr);
	if (sr.length < n + 2) {
		return ISC_R_UNEXPECTEDEND;
	}
	RETERR(mem_tobuffer(target, sr.base, n + 2));
	isc_region_consume(&sr, n + 2);
	isc_buffer_forward(source, n + 2);

	/*
	 * Other Length + Other Data.
	 */
	if (sr.length < 2) {
		return ISC_R_UNEXPECTEDEND;
	}
	n = uint16_fromregion(&sr);
	if (sr.length < n + 2) {
		return ISC_R_UNEXPECTEDEND;
	}
	isc_buffer_forward(source, n + 2);
	return mem_tobuffer(target, sr.base, n + 2);
}

static isc_result_t
towire_tkey(ARGS_TOWIRE) {
	isc_region_t sr;
	dns_name_t name;

	REQUIRE(rdata->type == dns_rdatatype_tkey);
	REQUIRE(rdata->length != 0);

	dns_compress_setpermitted(cctx, false);
	/*
	 * Algorithm.
	 */
	dns_rdata_toregion(rdata, &sr);
	dns_name_init(&name);
	dns_name_fromregion(&name, &sr);
	RETERR(dns_name_towire(&name, cctx, target));
	isc_region_consume(&sr, name_length(&name));

	return mem_tobuffer(target, sr.base, sr.length);
}

static int
compare_tkey(ARGS_COMPARE) {
	isc_region_t r1;
	isc_region_t r2;
	dns_name_t name1;
	dns_name_t name2;
	int order;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_tkey);
	REQUIRE(rdata1->length != 0);
	REQUIRE(rdata2->length != 0);

	/*
	 * Algorithm.
	 */
	dns_rdata_toregion(rdata1, &r1);
	dns_rdata_toregion(rdata2, &r2);
	dns_name_init(&name1);
	dns_name_init(&name2);
	dns_name_fromregion(&name1, &r1);
	dns_name_fromregion(&name2, &r2);
	if ((order = dns_name_rdatacompare(&name1, &name2)) != 0) {
		return order;
	}
	isc_region_consume(&r1, name_length(&name1));
	isc_region_consume(&r2, name_length(&name2));
	return isc_region_compare(&r1, &r2);
}

static isc_result_t
fromstruct_tkey(ARGS_FROMSTRUCT) {
	dns_rdata_tkey_t *tkey = source;

	REQUIRE(type == dns_rdatatype_tkey);
	REQUIRE(tkey != NULL);
	REQUIRE(tkey->common.rdtype == type);
	REQUIRE(tkey->common.rdclass == rdclass);

	UNUSED(type);
	UNUSED(rdclass);

	/*
	 * Algorithm Name.
	 */
	RETERR(name_tobuffer(&tkey->algorithm, target));

	/*
	 * Inception: 32 bits.
	 */
	RETERR(uint32_tobuffer(tkey->inception, target));

	/*
	 * Expire: 32 bits.
	 */
	RETERR(uint32_tobuffer(tkey->expire, target));

	/*
	 * Mode: 16 bits.
	 */
	RETERR(uint16_tobuffer(tkey->mode, target));

	/*
	 * Error: 16 bits.
	 */
	RETERR(uint16_tobuffer(tkey->error, target));

	/*
	 * Key size: 16 bits.
	 */
	RETERR(uint16_tobuffer(tkey->keylen, target));

	/*
	 * Key.
	 */
	RETERR(mem_tobuffer(target, tkey->key, tkey->keylen));

	/*
	 * Other size: 16 bits.
	 */
	RETERR(uint16_tobuffer(tkey->otherlen, target));

	/*
	 * Other data.
	 */
	return mem_tobuffer(target, tkey->other, tkey->otherlen);
}

static isc_result_t
tostruct_tkey(ARGS_TOSTRUCT) {
	dns_rdata_tkey_t *tkey = target;
	dns_name_t alg;
	isc_region_t sr;

	REQUIRE(rdata->type == dns_rdatatype_tkey);
	REQUIRE(tkey != NULL);
	REQUIRE(rdata->length != 0);

	tkey->common.rdclass = rdata->rdclass;
	tkey->common.rdtype = rdata->type;

	dns_rdata_toregion(rdata, &sr);

	/*
	 * Algorithm Name.
	 */
	dns_name_init(&alg);
	dns_name_fromregion(&alg, &sr);
	dns_name_init(&tkey->algorithm);
	name_duporclone(&alg, mctx, &tkey->algorithm);
	isc_region_consume(&sr, name_length(&tkey->algorithm));

	/*
	 * Inception.
	 */
	tkey->inception = uint32_fromregion(&sr);
	isc_region_consume(&sr, 4);

	/*
	 * Expire.
	 */
	tkey->expire = uint32_fromregion(&sr);
	isc_region_consume(&sr, 4);

	/*
	 * Mode.
	 */
	tkey->mode = uint16_fromregion(&sr);
	isc_region_consume(&sr, 2);

	/*
	 * Error.
	 */
	tkey->error = uint16_fromregion(&sr);
	isc_region_consume(&sr, 2);

	/*
	 * Key size.
	 */
	tkey->keylen = uint16_fromregion(&sr);
	isc_region_consume(&sr, 2);

	/*
	 * Key.
	 */
	INSIST(tkey->keylen + 2U <= sr.length);
	tkey->key = mem_maybedup(mctx, sr.base, tkey->keylen);
	isc_region_consume(&sr, tkey->keylen);

	/*
	 * Other size.
	 */
	tkey->otherlen = uint16_fromregion(&sr);
	isc_region_consume(&sr, 2);

	/*
	 * Other.
	 */
	INSIST(tkey->otherlen <= sr.length);
	tkey->other = mem_maybedup(mctx, sr.base, tkey->otherlen);
	tkey->mctx = mctx;
	return ISC_R_SUCCESS;
}

static void
freestruct_tkey(ARGS_FREESTRUCT) {
	dns_rdata_tkey_t *tkey = (dns_rdata_tkey_t *)source;

	REQUIRE(tkey != NULL);

	if (tkey->mctx == NULL) {
		return;
	}

	dns_name_free(&tkey->algorithm, tkey->mctx);
	if (tkey->key != NULL) {
		isc_mem_free(tkey->mctx, tkey->key);
	}
	if (tkey->other != NULL) {
		isc_mem_free(tkey->mctx, tkey->other);
	}
	tkey->mctx = NULL;
}

static isc_result_t
additionaldata_tkey(ARGS_ADDLDATA) {
	REQUIRE(rdata->type == dns_rdatatype_tkey);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(add);
	UNUSED(arg);

	return ISC_R_SUCCESS;
}

static isc_result_t
digest_tkey(ARGS_DIGEST) {
	UNUSED(rdata);
	UNUSED(digest);
	UNUSED(arg);

	REQUIRE(rdata->type == dns_rdatatype_tkey);

	return ISC_R_NOTIMPLEMENTED;
}

static bool
checkowner_tkey(ARGS_CHECKOWNER) {
	REQUIRE(type == dns_rdatatype_tkey);

	UNUSED(name);
	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(wildcard);

	return true;
}

static bool
checknames_tkey(ARGS_CHECKNAMES) {
	REQUIRE(rdata->type == dns_rdatatype_tkey);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(bad);

	return true;
}

static int
casecompare_tkey(ARGS_COMPARE) {
	return compare_tkey(rdata1, rdata2);
}
#endif /* RDATA_GENERIC_TKEY_249_C */
