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

/* RFC 5205 */

#pragma once

#define RRTYPE_HIP_ATTRIBUTES (0)

static isc_result_t
fromtext_hip(ARGS_FROMTEXT) {
	isc_token_t token;
	isc_buffer_t buffer;
	isc_buffer_t hit_len;
	isc_buffer_t key_len;
	unsigned char *start;
	size_t len;

	REQUIRE(type == dns_rdatatype_hip);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(callbacks);

	/*
	 * Dummy HIT len.
	 */
	hit_len = *target;
	RETERR(uint8_tobuffer(0, target));

	/*
	 * Algorithm.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	if (token.value.as_ulong > 0xffU) {
		RETTOK(ISC_R_RANGE);
	}
	RETERR(uint8_tobuffer(token.value.as_ulong, target));

	/*
	 * Dummy KEY len.
	 */
	key_len = *target;
	RETERR(uint16_tobuffer(0, target));

	/*
	 * HIT (base16).
	 */
	start = isc_buffer_used(target);
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_string,
				      false));
	RETTOK(isc_hex_decodestring(DNS_AS_STR(token), target));

	/*
	 * Fill in HIT len.
	 */
	len = (unsigned char *)isc_buffer_used(target) - start;
	if (len > 0xffU) {
		RETTOK(ISC_R_RANGE);
	}
	RETERR(uint8_tobuffer((uint32_t)len, &hit_len));

	/*
	 * Public key (base64).
	 */
	start = isc_buffer_used(target);
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_string,
				      false));
	RETTOK(isc_base64_decodestring(DNS_AS_STR(token), target));

	/*
	 * Fill in KEY len.
	 */
	len = (unsigned char *)isc_buffer_used(target) - start;
	if (len > 0xffffU) {
		RETTOK(ISC_R_RANGE);
	}
	RETERR(uint16_tobuffer((uint32_t)len, &key_len));

	if (origin == NULL) {
		origin = dns_rootname;
	}

	/*
	 * Rendezvous Servers.
	 */
	do {
		RETERR(isc_lex_getmastertoken(lexer, &token,
					      isc_tokentype_string, true));
		if (token.type != isc_tokentype_string) {
			break;
		}
		buffer_fromregion(&buffer, &token.value.as_region);
		RETTOK(dns_name_wirefromtext(&buffer, origin, options, target));
	} while (1);

	/*
	 * Let upper layer handle eol/eof.
	 */
	isc_lex_ungettoken(lexer, &token);

	return ISC_R_SUCCESS;
}

static isc_result_t
totext_hip(ARGS_TOTEXT) {
	isc_region_t region;
	dns_name_t name;
	unsigned int length, key_len, hit_len;
	unsigned char algorithm;
	char buf[sizeof("225 ")];

	REQUIRE(rdata->type == dns_rdatatype_hip);
	REQUIRE(rdata->length != 0);

	dns_rdata_toregion(rdata, &region);

	hit_len = uint8_fromregion(&region);
	isc_region_consume(&region, 1);

	algorithm = uint8_fromregion(&region);
	isc_region_consume(&region, 1);

	key_len = uint16_fromregion(&region);
	isc_region_consume(&region, 2);

	if ((tctx->flags & DNS_STYLEFLAG_MULTILINE) != 0) {
		RETERR(str_totext("( ", target));
	}

	/*
	 * Algorithm
	 */
	snprintf(buf, sizeof(buf), "%u ", algorithm);
	RETERR(str_totext(buf, target));

	/*
	 * HIT.
	 */
	INSIST(hit_len < region.length);
	length = region.length;
	region.length = hit_len;
	RETERR(isc_hex_totext(&region, 1, "", target));
	region.length = length - hit_len;
	RETERR(str_totext(tctx->linebreak, target));

	/*
	 * Public KEY.
	 */
	INSIST(key_len <= region.length);
	length = region.length;
	region.length = key_len;
	RETERR(isc_base64_totext(&region, 1, "", target));
	region.length = length - key_len;
	if (region.length > 0) {
		RETERR(str_totext(tctx->linebreak, target));
	}

	/*
	 * Rendezvous Servers.
	 */
	dns_name_init(&name);
	while (region.length > 0) {
		dns_name_fromregion(&name, &region);

		RETERR(dns_name_totext(&name, 0, target));
		isc_region_consume(&region, name.length);
		if (region.length > 0) {
			RETERR(str_totext(tctx->linebreak, target));
		}
	}
	if ((tctx->flags & DNS_STYLEFLAG_MULTILINE) != 0) {
		RETERR(str_totext(" )", target));
	}
	return ISC_R_SUCCESS;
}

static isc_result_t
fromwire_hip(ARGS_FROMWIRE) {
	isc_region_t region, rr;
	dns_name_t name;
	uint8_t hit_len;
	uint16_t key_len;
	size_t len;

	REQUIRE(type == dns_rdatatype_hip);

	UNUSED(type);
	UNUSED(rdclass);

	isc_buffer_activeregion(source, &region);
	if (region.length < 4U) {
		RETERR(DNS_R_FORMERR);
	}

	rr = region;
	hit_len = uint8_fromregion(&region);
	if (hit_len == 0) {
		RETERR(DNS_R_FORMERR);
	}
	isc_region_consume(&region, 2); /* hit length + algorithm */
	key_len = uint16_fromregion(&region);
	if (key_len == 0) {
		RETERR(DNS_R_FORMERR);
	}
	isc_region_consume(&region, 2);
	len = hit_len + key_len;
	if (len > region.length) {
		RETERR(DNS_R_FORMERR);
	}

	RETERR(mem_tobuffer(target, rr.base, 4 + len));
	isc_buffer_forward(source, 4 + len);

	dctx = dns_decompress_setpermitted(dctx, false);
	while (isc_buffer_activelength(source) > 0) {
		dns_name_init(&name);
		RETERR(dns_name_fromwire(&name, source, dctx, target));
	}
	return ISC_R_SUCCESS;
}

static isc_result_t
towire_hip(ARGS_TOWIRE) {
	isc_region_t region;

	REQUIRE(rdata->type == dns_rdatatype_hip);
	REQUIRE(rdata->length != 0);

	UNUSED(cctx);

	dns_rdata_toregion(rdata, &region);
	return mem_tobuffer(target, region.base, region.length);
}

static int
compare_hip(ARGS_COMPARE) {
	isc_region_t region1;
	isc_region_t region2;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_hip);
	REQUIRE(rdata1->length != 0);
	REQUIRE(rdata2->length != 0);

	dns_rdata_toregion(rdata1, &region1);
	dns_rdata_toregion(rdata2, &region2);
	return isc_region_compare(&region1, &region2);
}

static isc_result_t
fromstruct_hip(ARGS_FROMSTRUCT) {
	dns_rdata_hip_t *hip = source;
	dns_rdata_hip_t myhip;
	isc_result_t result;

	REQUIRE(type == dns_rdatatype_hip);
	REQUIRE(hip != NULL);
	REQUIRE(hip->common.rdtype == type);
	REQUIRE(hip->common.rdclass == rdclass);
	REQUIRE(hip->hit_len > 0 && hip->hit != NULL);
	REQUIRE(hip->key_len > 0 && hip->key != NULL);
	REQUIRE((hip->servers == NULL && hip->servers_len == 0) ||
		(hip->servers != NULL && hip->servers_len != 0));

	UNUSED(type);
	UNUSED(rdclass);

	RETERR(uint8_tobuffer(hip->hit_len, target));
	RETERR(uint8_tobuffer(hip->algorithm, target));
	RETERR(uint16_tobuffer(hip->key_len, target));
	RETERR(mem_tobuffer(target, hip->hit, hip->hit_len));
	RETERR(mem_tobuffer(target, hip->key, hip->key_len));

	myhip = *hip;
	for (result = dns_rdata_hip_first(&myhip); result == ISC_R_SUCCESS;
	     result = dns_rdata_hip_next(&myhip))
	{
		/* initialize the names */
	}

	return mem_tobuffer(target, hip->servers, hip->servers_len);
}

static isc_result_t
tostruct_hip(ARGS_TOSTRUCT) {
	isc_region_t region;
	dns_rdata_hip_t *hip = target;

	REQUIRE(rdata->type == dns_rdatatype_hip);
	REQUIRE(hip != NULL);
	REQUIRE(rdata->length != 0);

	hip->common.rdclass = rdata->rdclass;
	hip->common.rdtype = rdata->type;

	dns_rdata_toregion(rdata, &region);

	hip->hit_len = uint8_fromregion(&region);
	isc_region_consume(&region, 1);

	hip->algorithm = uint8_fromregion(&region);
	isc_region_consume(&region, 1);

	hip->key_len = uint16_fromregion(&region);
	isc_region_consume(&region, 2);

	hip->hit = hip->key = hip->servers = NULL;

	hip->hit = mem_maybedup(mctx, region.base, hip->hit_len);
	isc_region_consume(&region, hip->hit_len);

	INSIST(hip->key_len <= region.length);

	hip->key = mem_maybedup(mctx, region.base, hip->key_len);
	isc_region_consume(&region, hip->key_len);

	hip->servers_len = region.length;
	if (hip->servers_len != 0) {
		hip->servers = mem_maybedup(mctx, region.base, region.length);
	}

	hip->offset = hip->servers_len;
	hip->mctx = mctx;
	return ISC_R_SUCCESS;
}

static void
freestruct_hip(ARGS_FREESTRUCT) {
	dns_rdata_hip_t *hip = source;

	REQUIRE(hip != NULL);

	if (hip->mctx == NULL) {
		return;
	}

	isc_mem_free(hip->mctx, hip->hit);
	isc_mem_free(hip->mctx, hip->key);
	if (hip->servers != NULL) {
		isc_mem_free(hip->mctx, hip->servers);
	}
	hip->mctx = NULL;
}

static isc_result_t
additionaldata_hip(ARGS_ADDLDATA) {
	REQUIRE(rdata->type == dns_rdatatype_hip);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(add);
	UNUSED(arg);

	return ISC_R_SUCCESS;
}

static isc_result_t
digest_hip(ARGS_DIGEST) {
	isc_region_t r;

	REQUIRE(rdata->type == dns_rdatatype_hip);

	dns_rdata_toregion(rdata, &r);
	return (digest)(arg, &r);
}

static bool
checkowner_hip(ARGS_CHECKOWNER) {
	REQUIRE(type == dns_rdatatype_hip);

	UNUSED(name);
	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(wildcard);

	return true;
}

static bool
checknames_hip(ARGS_CHECKNAMES) {
	REQUIRE(rdata->type == dns_rdatatype_hip);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(bad);

	return true;
}

isc_result_t
dns_rdata_hip_first(dns_rdata_hip_t *hip) {
	if (hip->servers_len == 0) {
		return ISC_R_NOMORE;
	}
	hip->offset = 0;
	return ISC_R_SUCCESS;
}

isc_result_t
dns_rdata_hip_next(dns_rdata_hip_t *hip) {
	isc_region_t region;
	dns_name_t name;

	if (hip->offset >= hip->servers_len) {
		return ISC_R_NOMORE;
	}

	region.base = hip->servers + hip->offset;
	region.length = hip->servers_len - hip->offset;
	dns_name_init(&name);
	dns_name_fromregion(&name, &region);
	hip->offset += name.length;
	INSIST(hip->offset <= hip->servers_len);
	return hip->offset < hip->servers_len ? ISC_R_SUCCESS : ISC_R_NOMORE;
}

void
dns_rdata_hip_current(dns_rdata_hip_t *hip, dns_name_t *name) {
	isc_region_t region;

	REQUIRE(hip->offset < hip->servers_len);

	region.base = hip->servers + hip->offset;
	region.length = hip->servers_len - hip->offset;
	dns_name_fromregion(name, &region);

	INSIST(name->length + hip->offset <= hip->servers_len);
}

static int
casecompare_hip(ARGS_COMPARE) {
	isc_region_t r1;
	isc_region_t r2;
	dns_name_t name1;
	dns_name_t name2;
	int order;
	uint8_t hit_len;
	uint16_t key_len;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_hip);
	REQUIRE(rdata1->length != 0);
	REQUIRE(rdata2->length != 0);

	dns_rdata_toregion(rdata1, &r1);
	dns_rdata_toregion(rdata2, &r2);

	INSIST(r1.length > 4);
	INSIST(r2.length > 4);
	order = memcmp(r1.base, r2.base, 4);
	if (order != 0) {
		return order;
	}

	hit_len = uint8_fromregion(&r1);
	isc_region_consume(&r1, 2); /* hit length + algorithm */
	key_len = uint16_fromregion(&r1);
	isc_region_consume(&r1, 2); /* key length */
	isc_region_consume(&r2, 4);

	INSIST(r1.length >= (unsigned int)(hit_len + key_len));
	INSIST(r2.length >= (unsigned int)(hit_len + key_len));
	order = memcmp(r1.base, r2.base, hit_len + key_len);
	if (order != 0) {
		return order;
	}
	isc_region_consume(&r1, hit_len + key_len);
	isc_region_consume(&r2, hit_len + key_len);

	dns_name_init(&name1);
	dns_name_init(&name2);
	while (r1.length != 0 && r2.length != 0) {
		dns_name_fromregion(&name1, &r1);
		dns_name_fromregion(&name2, &r2);
		order = dns_name_rdatacompare(&name1, &name2);
		if (order != 0) {
			return order;
		}

		isc_region_consume(&r1, name_length(&name1));
		isc_region_consume(&r2, name_length(&name2));
	}
	return isc_region_compare(&r1, &r2);
}
