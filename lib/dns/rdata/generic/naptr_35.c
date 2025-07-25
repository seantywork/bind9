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

/* RFC2915 */

#ifndef RDATA_GENERIC_NAPTR_35_C
#define RDATA_GENERIC_NAPTR_35_C

#define RRTYPE_NAPTR_ATTRIBUTES (0)

#include <isc/regex.h>

/*
 * Check the wire format of the Regexp field.
 * Don't allow embedded NUL's.
 */
static isc_result_t
txt_valid_regex(const unsigned char *txt) {
	unsigned int nsub = 0;
	char regex[256];
	char *cp;
	bool flags = false;
	bool replace = false;
	unsigned char c;
	unsigned char delim;
	unsigned int len;
	int n;

	len = *txt++;
	if (len == 0U) {
		return ISC_R_SUCCESS;
	}

	delim = *txt++;
	len--;

	/*
	 * Digits, backslash and flags can't be delimiters.
	 */
	switch (delim) {
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	case '\\':
	case 'i':
	case 0:
		return DNS_R_SYNTAX;
	}

	cp = regex;
	while (len-- > 0) {
		c = *txt++;
		if (c == 0) {
			return DNS_R_SYNTAX;
		}
		if (c == delim && !replace) {
			replace = true;
			continue;
		} else if (c == delim && !flags) {
			flags = true;
			continue;
		} else if (c == delim) {
			return DNS_R_SYNTAX;
		}
		/*
		 * Flags are not escaped.
		 */
		if (flags) {
			switch (c) {
			case 'i':
				continue;
			default:
				return DNS_R_SYNTAX;
			}
		}
		if (!replace) {
			*cp++ = c;
		}
		if (c == '\\') {
			if (len == 0) {
				return DNS_R_SYNTAX;
			}
			c = *txt++;
			if (c == 0) {
				return DNS_R_SYNTAX;
			}
			len--;
			if (replace) {
				switch (c) {
				case '0':
					return DNS_R_SYNTAX;
				case '1':
					if (nsub < 1) {
						nsub = 1;
					}
					break;
				case '2':
					if (nsub < 2) {
						nsub = 2;
					}
					break;
				case '3':
					if (nsub < 3) {
						nsub = 3;
					}
					break;
				case '4':
					if (nsub < 4) {
						nsub = 4;
					}
					break;
				case '5':
					if (nsub < 5) {
						nsub = 5;
					}
					break;
				case '6':
					if (nsub < 6) {
						nsub = 6;
					}
					break;
				case '7':
					if (nsub < 7) {
						nsub = 7;
					}
					break;
				case '8':
					if (nsub < 8) {
						nsub = 8;
					}
					break;
				case '9':
					if (nsub < 9) {
						nsub = 9;
					}
					break;
				}
			}
			if (!replace) {
				*cp++ = c;
			}
		}
	}
	if (!flags) {
		return DNS_R_SYNTAX;
	}
	*cp = '\0';
	n = isc_regex_validate(regex);
	if (n < 0 || nsub > (unsigned int)n) {
		return DNS_R_SYNTAX;
	}
	return ISC_R_SUCCESS;
}

static isc_result_t
fromtext_naptr(ARGS_FROMTEXT) {
	isc_token_t token;
	isc_buffer_t buffer;
	unsigned char *regex;

	REQUIRE(type == dns_rdatatype_naptr);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(callbacks);

	/*
	 * Order.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	if (token.value.as_ulong > 0xffffU) {
		RETTOK(ISC_R_RANGE);
	}
	RETERR(uint16_tobuffer(token.value.as_ulong, target));

	/*
	 * Preference.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	if (token.value.as_ulong > 0xffffU) {
		RETTOK(ISC_R_RANGE);
	}
	RETERR(uint16_tobuffer(token.value.as_ulong, target));

	/*
	 * Flags.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_qstring,
				      false));
	RETTOK(txt_fromtext(&token.value.as_textregion, target));

	/*
	 * Service.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_qstring,
				      false));
	RETTOK(txt_fromtext(&token.value.as_textregion, target));

	/*
	 * Regexp.
	 */
	regex = isc_buffer_used(target);
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_qstring,
				      false));
	RETTOK(txt_fromtext(&token.value.as_textregion, target));
	RETTOK(txt_valid_regex(regex));

	/*
	 * Replacement.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_string,
				      false));
	buffer_fromregion(&buffer, &token.value.as_region);
	if (origin == NULL) {
		origin = dns_rootname;
	}
	RETTOK(dns_name_wirefromtext(&buffer, origin, options, target));
	return ISC_R_SUCCESS;
}

static isc_result_t
totext_naptr(ARGS_TOTEXT) {
	isc_region_t region;
	dns_name_t name;
	dns_name_t prefix;
	unsigned int opts;
	char buf[sizeof("64000")];
	unsigned short num;

	REQUIRE(rdata->type == dns_rdatatype_naptr);
	REQUIRE(rdata->length != 0);

	dns_name_init(&name);
	dns_name_init(&prefix);

	dns_rdata_toregion(rdata, &region);

	/*
	 * Order.
	 */
	num = uint16_fromregion(&region);
	isc_region_consume(&region, 2);
	snprintf(buf, sizeof(buf), "%u", num);
	RETERR(str_totext(buf, target));
	RETERR(str_totext(" ", target));

	/*
	 * Preference.
	 */
	num = uint16_fromregion(&region);
	isc_region_consume(&region, 2);
	snprintf(buf, sizeof(buf), "%u", num);
	RETERR(str_totext(buf, target));
	RETERR(str_totext(" ", target));

	/*
	 * Flags.
	 */
	RETERR(txt_totext(&region, true, target));
	RETERR(str_totext(" ", target));

	/*
	 * Service.
	 */
	RETERR(txt_totext(&region, true, target));
	RETERR(str_totext(" ", target));

	/*
	 * Regexp.
	 */
	RETERR(txt_totext(&region, true, target));
	RETERR(str_totext(" ", target));

	/*
	 * Replacement.
	 */
	dns_name_fromregion(&name, &region);
	opts = name_prefix(&name, tctx->origin, &prefix) ? DNS_NAME_OMITFINALDOT
							 : 0;
	return dns_name_totext(&prefix, opts, target);
}

static isc_result_t
fromwire_naptr(ARGS_FROMWIRE) {
	dns_name_t name;
	isc_region_t sr;
	unsigned char *regex;

	REQUIRE(type == dns_rdatatype_naptr);

	UNUSED(type);
	UNUSED(rdclass);

	dctx = dns_decompress_setpermitted(dctx, false);

	dns_name_init(&name);

	/*
	 * Order, preference.
	 */
	isc_buffer_activeregion(source, &sr);
	if (sr.length < 4) {
		return ISC_R_UNEXPECTEDEND;
	}
	RETERR(mem_tobuffer(target, sr.base, 4));
	isc_buffer_forward(source, 4);

	/*
	 * Flags.
	 */
	RETERR(txt_fromwire(source, target));

	/*
	 * Service.
	 */
	RETERR(txt_fromwire(source, target));

	/*
	 * Regexp.
	 */
	regex = isc_buffer_used(target);
	RETERR(txt_fromwire(source, target));
	RETERR(txt_valid_regex(regex));

	/*
	 * Replacement.
	 */
	return dns_name_fromwire(&name, source, dctx, target);
}

static isc_result_t
towire_naptr(ARGS_TOWIRE) {
	dns_name_t name;
	isc_region_t sr;

	REQUIRE(rdata->type == dns_rdatatype_naptr);
	REQUIRE(rdata->length != 0);

	dns_compress_setpermitted(cctx, false);
	/*
	 * Order, preference.
	 */
	dns_rdata_toregion(rdata, &sr);
	RETERR(mem_tobuffer(target, sr.base, 4));
	isc_region_consume(&sr, 4);

	/*
	 * Flags.
	 */
	RETERR(mem_tobuffer(target, sr.base, sr.base[0] + 1));
	isc_region_consume(&sr, sr.base[0] + 1);

	/*
	 * Service.
	 */
	RETERR(mem_tobuffer(target, sr.base, sr.base[0] + 1));
	isc_region_consume(&sr, sr.base[0] + 1);

	/*
	 * Regexp.
	 */
	RETERR(mem_tobuffer(target, sr.base, sr.base[0] + 1));
	isc_region_consume(&sr, sr.base[0] + 1);

	/*
	 * Replacement.
	 */
	dns_name_init(&name);
	dns_name_fromregion(&name, &sr);
	return dns_name_towire(&name, cctx, target);
}

static int
compare_naptr(ARGS_COMPARE) {
	dns_name_t name1;
	dns_name_t name2;
	isc_region_t region1;
	isc_region_t region2;
	int order, len;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_naptr);
	REQUIRE(rdata1->length != 0);
	REQUIRE(rdata2->length != 0);

	dns_rdata_toregion(rdata1, &region1);
	dns_rdata_toregion(rdata2, &region2);

	/*
	 * Order, preference.
	 */
	order = memcmp(region1.base, region2.base, 4);
	if (order != 0) {
		return order < 0 ? -1 : 1;
	}
	isc_region_consume(&region1, 4);
	isc_region_consume(&region2, 4);

	/*
	 * Flags.
	 */
	len = ISC_MIN(region1.base[0], region2.base[0]);
	order = memcmp(region1.base, region2.base, len + 1);
	if (order != 0) {
		return order < 0 ? -1 : 1;
	}
	isc_region_consume(&region1, region1.base[0] + 1);
	isc_region_consume(&region2, region2.base[0] + 1);

	/*
	 * Service.
	 */
	len = ISC_MIN(region1.base[0], region2.base[0]);
	order = memcmp(region1.base, region2.base, len + 1);
	if (order != 0) {
		return order < 0 ? -1 : 1;
	}
	isc_region_consume(&region1, region1.base[0] + 1);
	isc_region_consume(&region2, region2.base[0] + 1);

	/*
	 * Regexp.
	 */
	len = ISC_MIN(region1.base[0], region2.base[0]);
	order = memcmp(region1.base, region2.base, len + 1);
	if (order != 0) {
		return order < 0 ? -1 : 1;
	}
	isc_region_consume(&region1, region1.base[0] + 1);
	isc_region_consume(&region2, region2.base[0] + 1);

	/*
	 * Replacement.
	 */
	dns_name_init(&name1);
	dns_name_init(&name2);

	dns_name_fromregion(&name1, &region1);
	dns_name_fromregion(&name2, &region2);

	return dns_name_rdatacompare(&name1, &name2);
}

static isc_result_t
fromstruct_naptr(ARGS_FROMSTRUCT) {
	dns_rdata_naptr_t *naptr = source;
	isc_region_t region;

	REQUIRE(type == dns_rdatatype_naptr);
	REQUIRE(naptr != NULL);
	REQUIRE(naptr->common.rdtype == type);
	REQUIRE(naptr->common.rdclass == rdclass);
	REQUIRE(naptr->flags != NULL || naptr->flags_len == 0);
	REQUIRE(naptr->service != NULL || naptr->service_len == 0);
	REQUIRE(naptr->regexp != NULL || naptr->regexp_len == 0);

	UNUSED(type);
	UNUSED(rdclass);

	RETERR(uint16_tobuffer(naptr->order, target));
	RETERR(uint16_tobuffer(naptr->preference, target));
	RETERR(uint8_tobuffer(naptr->flags_len, target));
	RETERR(mem_tobuffer(target, naptr->flags, naptr->flags_len));
	RETERR(uint8_tobuffer(naptr->service_len, target));
	RETERR(mem_tobuffer(target, naptr->service, naptr->service_len));
	RETERR(uint8_tobuffer(naptr->regexp_len, target));
	RETERR(mem_tobuffer(target, naptr->regexp, naptr->regexp_len));
	dns_name_toregion(&naptr->replacement, &region);
	return isc_buffer_copyregion(target, &region);
}

static isc_result_t
tostruct_naptr(ARGS_TOSTRUCT) {
	dns_rdata_naptr_t *naptr = target;
	isc_region_t r;
	dns_name_t name;

	REQUIRE(rdata->type == dns_rdatatype_naptr);
	REQUIRE(naptr != NULL);
	REQUIRE(rdata->length != 0);

	naptr->common.rdclass = rdata->rdclass;
	naptr->common.rdtype = rdata->type;

	naptr->flags = NULL;
	naptr->service = NULL;
	naptr->regexp = NULL;

	dns_rdata_toregion(rdata, &r);

	naptr->order = uint16_fromregion(&r);
	isc_region_consume(&r, 2);

	naptr->preference = uint16_fromregion(&r);
	isc_region_consume(&r, 2);

	naptr->flags_len = uint8_fromregion(&r);
	isc_region_consume(&r, 1);
	INSIST(naptr->flags_len <= r.length);
	naptr->flags = mem_maybedup(mctx, r.base, naptr->flags_len);
	isc_region_consume(&r, naptr->flags_len);

	naptr->service_len = uint8_fromregion(&r);
	isc_region_consume(&r, 1);
	INSIST(naptr->service_len <= r.length);
	naptr->service = mem_maybedup(mctx, r.base, naptr->service_len);
	isc_region_consume(&r, naptr->service_len);

	naptr->regexp_len = uint8_fromregion(&r);
	isc_region_consume(&r, 1);
	INSIST(naptr->regexp_len <= r.length);
	naptr->regexp = mem_maybedup(mctx, r.base, naptr->regexp_len);
	isc_region_consume(&r, naptr->regexp_len);

	dns_name_init(&name);
	dns_name_fromregion(&name, &r);
	dns_name_init(&naptr->replacement);
	name_duporclone(&name, mctx, &naptr->replacement);
	naptr->mctx = mctx;
	return ISC_R_SUCCESS;
}

static void
freestruct_naptr(ARGS_FREESTRUCT) {
	dns_rdata_naptr_t *naptr = source;

	REQUIRE(naptr != NULL);
	REQUIRE(naptr->common.rdtype == dns_rdatatype_naptr);

	if (naptr->mctx == NULL) {
		return;
	}

	if (naptr->flags != NULL) {
		isc_mem_free(naptr->mctx, naptr->flags);
	}
	if (naptr->service != NULL) {
		isc_mem_free(naptr->mctx, naptr->service);
	}
	if (naptr->regexp != NULL) {
		isc_mem_free(naptr->mctx, naptr->regexp);
	}
	dns_name_free(&naptr->replacement, naptr->mctx);
	naptr->mctx = NULL;
}

static isc_result_t
additionaldata_naptr(ARGS_ADDLDATA) {
	dns_name_t name;
	isc_region_t sr;
	dns_rdatatype_t atype;
	unsigned int i, flagslen;
	char *cp;

	REQUIRE(rdata->type == dns_rdatatype_naptr);

	UNUSED(owner);

	/*
	 * Order, preference.
	 */
	dns_rdata_toregion(rdata, &sr);
	isc_region_consume(&sr, 4);

	/*
	 * Flags.
	 */
	atype = 0;
	flagslen = sr.base[0];
	cp = (char *)&sr.base[1];
	for (i = 0; i < flagslen; i++, cp++) {
		if (*cp == 'S' || *cp == 's') {
			atype = dns_rdatatype_srv;
			break;
		}
		if (*cp == 'A' || *cp == 'a') {
			atype = dns_rdatatype_a;
			break;
		}
	}
	isc_region_consume(&sr, flagslen + 1);

	/*
	 * Service.
	 */
	isc_region_consume(&sr, sr.base[0] + 1);

	/*
	 * Regexp.
	 */
	isc_region_consume(&sr, sr.base[0] + 1);

	/*
	 * Replacement.
	 */
	dns_name_init(&name);
	dns_name_fromregion(&name, &sr);

	if (atype != 0) {
		return (add)(arg, &name, atype, NULL DNS__DB_FILELINE);
	}

	return ISC_R_SUCCESS;
}

static isc_result_t
digest_naptr(ARGS_DIGEST) {
	isc_region_t r1, r2;
	unsigned int length, n;
	isc_result_t result;
	dns_name_t name;

	REQUIRE(rdata->type == dns_rdatatype_naptr);

	dns_rdata_toregion(rdata, &r1);
	r2 = r1;
	length = 0;

	/*
	 * Order, preference.
	 */
	length += 4;
	isc_region_consume(&r2, 4);

	/*
	 * Flags.
	 */
	n = r2.base[0] + 1;
	length += n;
	isc_region_consume(&r2, n);

	/*
	 * Service.
	 */
	n = r2.base[0] + 1;
	length += n;
	isc_region_consume(&r2, n);

	/*
	 * Regexp.
	 */
	n = r2.base[0] + 1;
	length += n;
	isc_region_consume(&r2, n);

	/*
	 * Digest the RR up to the replacement name.
	 */
	r1.length = length;
	result = (digest)(arg, &r1);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	/*
	 * Replacement.
	 */

	dns_name_init(&name);
	dns_name_fromregion(&name, &r2);

	return dns_name_digest(&name, digest, arg);
}

static bool
checkowner_naptr(ARGS_CHECKOWNER) {
	REQUIRE(type == dns_rdatatype_naptr);

	UNUSED(name);
	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(wildcard);

	return true;
}

static bool
checknames_naptr(ARGS_CHECKNAMES) {
	REQUIRE(rdata->type == dns_rdatatype_naptr);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(bad);

	return true;
}

static int
casecompare_naptr(ARGS_COMPARE) {
	return compare_naptr(rdata1, rdata2);
}

#endif /* RDATA_GENERIC_NAPTR_35_C */
