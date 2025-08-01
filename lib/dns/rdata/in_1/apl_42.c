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

/* RFC3123 */

#ifndef RDATA_IN_1_APL_42_C
#define RDATA_IN_1_APL_42_C

#define RRTYPE_APL_ATTRIBUTES (0)

static isc_result_t
fromtext_in_apl(ARGS_FROMTEXT) {
	isc_token_t token;
	unsigned char addr[16];
	unsigned long afi;
	uint8_t prefix;
	uint8_t len;
	bool neg;
	char *cp, *ap, *slash;
	int n;

	REQUIRE(type == dns_rdatatype_apl);
	REQUIRE(rdclass == dns_rdataclass_in);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(origin);
	UNUSED(options);
	UNUSED(callbacks);

	do {
		RETERR(isc_lex_getmastertoken(lexer, &token,
					      isc_tokentype_string, true));
		if (token.type != isc_tokentype_string) {
			break;
		}

		cp = DNS_AS_STR(token);
		neg = (*cp == '!');
		if (neg) {
			cp++;
		}
		afi = strtoul(cp, &ap, 10);
		if (*ap++ != ':' || cp == ap) {
			RETTOK(DNS_R_SYNTAX);
		}
		if (afi > 0xffffU) {
			RETTOK(ISC_R_RANGE);
		}
		slash = strchr(ap, '/');
		if (slash == NULL || slash == ap) {
			RETTOK(DNS_R_SYNTAX);
		}
		RETTOK(isc_parse_uint8(&prefix, slash + 1, 10));
		switch (afi) {
		case 1:
			*slash = '\0';
			n = inet_pton(AF_INET, ap, addr);
			*slash = '/';
			if (n != 1) {
				RETTOK(DNS_R_BADDOTTEDQUAD);
			}
			if (prefix > 32) {
				RETTOK(ISC_R_RANGE);
			}
			for (len = 4; len > 0; len--) {
				if (addr[len - 1] != 0) {
					break;
				}
			}
			break;

		case 2:
			*slash = '\0';
			n = inet_pton(AF_INET6, ap, addr);
			*slash = '/';
			if (n != 1) {
				RETTOK(DNS_R_BADAAAA);
			}
			if (prefix > 128) {
				RETTOK(ISC_R_RANGE);
			}
			for (len = 16; len > 0; len--) {
				if (addr[len - 1] != 0) {
					break;
				}
			}
			break;

		default:
			RETTOK(ISC_R_NOTIMPLEMENTED);
		}
		RETERR(uint16_tobuffer(afi, target));
		RETERR(uint8_tobuffer(prefix, target));
		RETERR(uint8_tobuffer(len | ((neg) ? 0x80 : 0), target));
		RETERR(mem_tobuffer(target, addr, len));
	} while (1);

	/*
	 * Let upper layer handle eol/eof.
	 */
	isc_lex_ungettoken(lexer, &token);

	return ISC_R_SUCCESS;
}

static isc_result_t
totext_in_apl(ARGS_TOTEXT) {
	isc_region_t sr;
	isc_region_t ir;
	uint16_t afi;
	uint8_t prefix;
	uint8_t len;
	bool neg;
	unsigned char buf[16];
	char txt[sizeof(" !64000:")];
	const char *sep = "";
	int n;

	REQUIRE(rdata->type == dns_rdatatype_apl);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);

	UNUSED(tctx);

	dns_rdata_toregion(rdata, &sr);
	ir.base = buf;
	ir.length = sizeof(buf);

	while (sr.length > 0) {
		INSIST(sr.length >= 4);
		afi = uint16_fromregion(&sr);
		isc_region_consume(&sr, 2);
		prefix = *sr.base;
		isc_region_consume(&sr, 1);
		len = (*sr.base & 0x7f);
		neg = (*sr.base & 0x80);
		isc_region_consume(&sr, 1);
		INSIST(len <= sr.length);
		n = snprintf(txt, sizeof(txt), "%s%s%u:", sep, neg ? "!" : "",
			     afi);
		INSIST(n < (int)sizeof(txt));
		RETERR(str_totext(txt, target));
		switch (afi) {
		case 1:
			INSIST(len <= 4);
			INSIST(prefix <= 32);
			memset(buf, 0, sizeof(buf));
			memmove(buf, sr.base, len);
			RETERR(inet_totext(AF_INET, tctx->flags, &ir, target));
			break;

		case 2:
			INSIST(len <= 16);
			INSIST(prefix <= 128);
			memset(buf, 0, sizeof(buf));
			memmove(buf, sr.base, len);
			RETERR(inet_totext(AF_INET6, tctx->flags, &ir, target));
			break;

		default:
			return ISC_R_NOTIMPLEMENTED;
		}
		n = snprintf(txt, sizeof(txt), "/%u", prefix);
		INSIST(n < (int)sizeof(txt));
		RETERR(str_totext(txt, target));
		isc_region_consume(&sr, len);
		sep = " ";
	}
	return ISC_R_SUCCESS;
}

static isc_result_t
fromwire_in_apl(ARGS_FROMWIRE) {
	isc_region_t sr, sr2;
	isc_region_t tr;
	uint16_t afi;
	uint8_t prefix;
	uint8_t len;

	REQUIRE(type == dns_rdatatype_apl);
	REQUIRE(rdclass == dns_rdataclass_in);

	UNUSED(type);
	UNUSED(dctx);
	UNUSED(rdclass);

	isc_buffer_activeregion(source, &sr);
	isc_buffer_availableregion(target, &tr);
	if (sr.length > tr.length) {
		return ISC_R_NOSPACE;
	}
	sr2 = sr;

	/* Zero or more items */
	while (sr.length > 0) {
		if (sr.length < 4) {
			return ISC_R_UNEXPECTEDEND;
		}
		afi = uint16_fromregion(&sr);
		isc_region_consume(&sr, 2);
		prefix = *sr.base;
		isc_region_consume(&sr, 1);
		len = (*sr.base & 0x7f);
		isc_region_consume(&sr, 1);
		if (len > sr.length) {
			return ISC_R_UNEXPECTEDEND;
		}
		switch (afi) {
		case 1:
			if (prefix > 32 || len > 4) {
				return ISC_R_RANGE;
			}
			break;
		case 2:
			if (prefix > 128 || len > 16) {
				return ISC_R_RANGE;
			}
		}
		if (len > 0 && sr.base[len - 1] == 0) {
			return DNS_R_FORMERR;
		}
		isc_region_consume(&sr, len);
	}
	isc_buffer_forward(source, sr2.length);
	return mem_tobuffer(target, sr2.base, sr2.length);
}

static isc_result_t
towire_in_apl(ARGS_TOWIRE) {
	UNUSED(cctx);

	REQUIRE(rdata->type == dns_rdatatype_apl);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);

	return mem_tobuffer(target, rdata->data, rdata->length);
}

static int
compare_in_apl(ARGS_COMPARE) {
	isc_region_t r1;
	isc_region_t r2;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_apl);
	REQUIRE(rdata1->rdclass == dns_rdataclass_in);

	dns_rdata_toregion(rdata1, &r1);
	dns_rdata_toregion(rdata2, &r2);
	return isc_region_compare(&r1, &r2);
}

static isc_result_t
fromstruct_in_apl(ARGS_FROMSTRUCT) {
	dns_rdata_in_apl_t *apl = source;
	isc_buffer_t b;

	REQUIRE(type == dns_rdatatype_apl);
	REQUIRE(rdclass == dns_rdataclass_in);
	REQUIRE(apl != NULL);
	REQUIRE(apl->common.rdtype == type);
	REQUIRE(apl->common.rdclass == rdclass);
	REQUIRE(apl->apl != NULL || apl->apl_len == 0);

	isc_buffer_init(&b, apl->apl, apl->apl_len);
	isc_buffer_add(&b, apl->apl_len);
	isc_buffer_setactive(&b, apl->apl_len);
	return fromwire_in_apl(rdclass, type, &b, DNS_DECOMPRESS_DEFAULT,
			       target);
}

static isc_result_t
tostruct_in_apl(ARGS_TOSTRUCT) {
	dns_rdata_in_apl_t *apl = target;
	isc_region_t r;

	REQUIRE(apl != NULL);
	REQUIRE(rdata->type == dns_rdatatype_apl);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);

	apl->common.rdclass = rdata->rdclass;
	apl->common.rdtype = rdata->type;

	dns_rdata_toregion(rdata, &r);
	apl->apl_len = r.length;
	apl->apl = mem_maybedup(mctx, r.base, r.length);
	apl->offset = 0;
	apl->mctx = mctx;
	return ISC_R_SUCCESS;
}

static void
freestruct_in_apl(ARGS_FREESTRUCT) {
	dns_rdata_in_apl_t *apl = source;

	REQUIRE(apl != NULL);
	REQUIRE(apl->common.rdtype == dns_rdatatype_apl);
	REQUIRE(apl->common.rdclass == dns_rdataclass_in);

	if (apl->mctx == NULL) {
		return;
	}
	if (apl->apl != NULL) {
		isc_mem_free(apl->mctx, apl->apl);
	}
	apl->mctx = NULL;
}

isc_result_t
dns_rdata_apl_first(dns_rdata_in_apl_t *apl) {
	uint32_t length;

	REQUIRE(apl != NULL);
	REQUIRE(apl->common.rdtype == dns_rdatatype_apl);
	REQUIRE(apl->common.rdclass == dns_rdataclass_in);
	REQUIRE(apl->apl != NULL || apl->apl_len == 0);

	/*
	 * If no APL return ISC_R_NOMORE.
	 */
	if (apl->apl == NULL) {
		return ISC_R_NOMORE;
	}

	/*
	 * Sanity check data.
	 */
	INSIST(apl->apl_len > 3U);
	length = apl->apl[apl->offset + 3] & 0x7f;
	INSIST(4 + length <= apl->apl_len);

	apl->offset = 0;
	return ISC_R_SUCCESS;
}

isc_result_t
dns_rdata_apl_next(dns_rdata_in_apl_t *apl) {
	uint32_t length;

	REQUIRE(apl != NULL);
	REQUIRE(apl->common.rdtype == dns_rdatatype_apl);
	REQUIRE(apl->common.rdclass == dns_rdataclass_in);
	REQUIRE(apl->apl != NULL || apl->apl_len == 0);

	/*
	 * No APL or have already reached the end return ISC_R_NOMORE.
	 */
	if (apl->apl == NULL || apl->offset == apl->apl_len) {
		return ISC_R_NOMORE;
	}

	/*
	 * Sanity check data.
	 */
	INSIST(apl->offset < apl->apl_len);
	INSIST(apl->apl_len > 3U);
	INSIST(apl->offset <= apl->apl_len - 4U);
	length = apl->apl[apl->offset + 3] & 0x7f;
	/*
	 * 16 to 32 bits promotion as 'length' is 32 bits so there is
	 * no overflow problems.
	 */
	INSIST(4 + length + apl->offset <= apl->apl_len);

	apl->offset += 4 + length;
	return (apl->offset < apl->apl_len) ? ISC_R_SUCCESS : ISC_R_NOMORE;
}

isc_result_t
dns_rdata_apl_current(dns_rdata_in_apl_t *apl, dns_rdata_apl_ent_t *ent) {
	uint32_t length;

	REQUIRE(apl != NULL);
	REQUIRE(apl->common.rdtype == dns_rdatatype_apl);
	REQUIRE(apl->common.rdclass == dns_rdataclass_in);
	REQUIRE(ent != NULL);
	REQUIRE(apl->apl != NULL || apl->apl_len == 0);
	REQUIRE(apl->offset <= apl->apl_len);

	if (apl->offset == apl->apl_len) {
		return ISC_R_NOMORE;
	}

	/*
	 * Sanity check data.
	 */
	INSIST(apl->apl_len > 3U);
	INSIST(apl->offset <= apl->apl_len - 4U);
	length = (apl->apl[apl->offset + 3] & 0x7f);
	/*
	 * 16 to 32 bits promotion as 'length' is 32 bits so there is
	 * no overflow problems.
	 */
	INSIST(4 + length + apl->offset <= apl->apl_len);

	ent->family = (apl->apl[apl->offset] << 8) + apl->apl[apl->offset + 1];
	ent->prefix = apl->apl[apl->offset + 2];
	ent->length = length;
	ent->negative = (apl->apl[apl->offset + 3] & 0x80);
	if (ent->length != 0) {
		ent->data = &apl->apl[apl->offset + 4];
	} else {
		ent->data = NULL;
	}
	return ISC_R_SUCCESS;
}

unsigned int
dns_rdata_apl_count(const dns_rdata_in_apl_t *apl) {
	return apl->apl_len;
}

static isc_result_t
additionaldata_in_apl(ARGS_ADDLDATA) {
	REQUIRE(rdata->type == dns_rdatatype_apl);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(add);
	UNUSED(arg);

	return ISC_R_SUCCESS;
}

static isc_result_t
digest_in_apl(ARGS_DIGEST) {
	isc_region_t r;

	REQUIRE(rdata->type == dns_rdatatype_apl);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);

	dns_rdata_toregion(rdata, &r);

	return (digest)(arg, &r);
}

static bool
checkowner_in_apl(ARGS_CHECKOWNER) {
	REQUIRE(type == dns_rdatatype_apl);
	REQUIRE(rdclass == dns_rdataclass_in);

	UNUSED(name);
	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(wildcard);

	return true;
}

static bool
checknames_in_apl(ARGS_CHECKNAMES) {
	REQUIRE(rdata->type == dns_rdatatype_apl);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(bad);

	return true;
}

static int
casecompare_in_apl(ARGS_COMPARE) {
	return compare_in_apl(rdata1, rdata2);
}

#endif /* RDATA_IN_1_APL_42_C */
