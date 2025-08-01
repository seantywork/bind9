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

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

#include <isc/ascii.h>
#include <isc/buffer.h>
#include <isc/parseint.h>
#include <isc/region.h>
#include <isc/result.h>
#include <isc/stdio.h>
#include <isc/string.h>
#include <isc/types.h>
#include <isc/util.h>

#include <dns/cert.h>
#include <dns/ds.h>
#include <dns/dsdigest.h>
#include <dns/keyflags.h>
#include <dns/keyvalues.h>
#include <dns/rcode.h>
#include <dns/rdataclass.h>
#include <dns/secalg.h>
#include <dns/secproto.h>

#include <dst/dst.h>

#define RETERR(x)                        \
	do {                             \
		isc_result_t _r = (x);   \
		if (_r != ISC_R_SUCCESS) \
			return ((_r));   \
	} while (0)

#define NUMBERSIZE sizeof("037777777777") /* 2^32-1 octal + NUL */

#define TOTEXTONLY 0x01

/* clang-format off */
#define SENTINEL { 0, NULL, 0 }
/* clang-format on */

#define RCODENAMES                                     \
	/* standard rcodes */                          \
	{ dns_rcode_noerror, "NOERROR", 0 },           \
		{ dns_rcode_formerr, "FORMERR", 0 },   \
		{ dns_rcode_servfail, "SERVFAIL", 0 }, \
		{ dns_rcode_nxdomain, "NXDOMAIN", 0 }, \
		{ dns_rcode_notimp, "NOTIMP", 0 },     \
		{ dns_rcode_refused, "REFUSED", 0 },   \
		{ dns_rcode_yxdomain, "YXDOMAIN", 0 }, \
		{ dns_rcode_yxrrset, "YXRRSET", 0 },   \
		{ dns_rcode_nxrrset, "NXRRSET", 0 },   \
		{ dns_rcode_notauth, "NOTAUTH", 0 },   \
		{ dns_rcode_notzone, "NOTZONE", 0 },   \
		{ 11, "RESERVED11", TOTEXTONLY },      \
		{ 12, "RESERVED12", TOTEXTONLY },      \
		{ 13, "RESERVED13", TOTEXTONLY },      \
		{ 14, "RESERVED14", TOTEXTONLY },      \
		{ 15, "RESERVED15", TOTEXTONLY },

#define ERCODENAMES                          \
	/* extended rcodes */                \
	{ dns_rcode_badvers, "BADVERS", 0 }, \
		{ dns_rcode_badcookie, "BADCOOKIE", 0 }, SENTINEL

#define TSIGRCODENAMES                                   \
	/* extended rcodes */                            \
	{ dns_tsigerror_badsig, "BADSIG", 0 },           \
		{ dns_tsigerror_badkey, "BADKEY", 0 },   \
		{ dns_tsigerror_badtime, "BADTIME", 0 }, \
		{ dns_tsigerror_badmode, "BADMODE", 0 }, \
		{ dns_tsigerror_badname, "BADNAME", 0 }, \
		{ dns_tsigerror_badalg, "BADALG", 0 },   \
		{ dns_tsigerror_badtrunc, "BADTRUNC", 0 }, SENTINEL

/* RFC4398 section 2.1 */

#define CERTNAMES                                                           \
	{ 1, "PKIX", 0 }, { 2, "SPKI", 0 }, { 3, "PGP", 0 },                \
		{ 4, "IPKIX", 0 }, { 5, "ISPKI", 0 }, { 6, "IPGP", 0 },     \
		{ 7, "ACPKIX", 0 }, { 8, "IACPKIX", 0 }, { 253, "URI", 0 }, \
		{ 254, "OID", 0 }, SENTINEL

/* RFC2535 section 7, RFC3110 */

#define SECALGNAMES                                             \
	{ DNS_KEYALG_RSAMD5, "RSAMD5", 0 },                     \
		{ DNS_KEYALG_DH_DEPRECATED, "DH", 0 },          \
		{ DNS_KEYALG_DSA, "DSA", 0 },                   \
		{ DNS_KEYALG_RSASHA1, "RSASHA1", 0 },           \
		{ DNS_KEYALG_NSEC3DSA, "NSEC3DSA", 0 },         \
		{ DNS_KEYALG_NSEC3RSASHA1, "NSEC3RSASHA1", 0 }, \
		{ DNS_KEYALG_RSASHA256, "RSASHA256", 0 },       \
		{ DNS_KEYALG_RSASHA512, "RSASHA512", 0 },       \
		{ DNS_KEYALG_ECCGOST, "ECCGOST", 0 },           \
		{ DNS_KEYALG_ECDSA256, "ECDSAP256SHA256", 0 },  \
		{ DNS_KEYALG_ECDSA256, "ECDSA256", 0 },         \
		{ DNS_KEYALG_ECDSA384, "ECDSAP384SHA384", 0 },  \
		{ DNS_KEYALG_ECDSA384, "ECDSA384", 0 },         \
		{ DNS_KEYALG_ED25519, "ED25519", 0 },           \
		{ DNS_KEYALG_ED448, "ED448", 0 },               \
		{ DNS_KEYALG_INDIRECT, "INDIRECT", 0 },         \
		{ DNS_KEYALG_PRIVATEDNS, "PRIVATEDNS", 0 },     \
		{ DNS_KEYALG_PRIVATEOID, "PRIVATEOID", 0 }, SENTINEL

/*
 * PRIVATEDNS subtypes we support.
 */
#define PRIVATEDNSS /* currently empty */

/*
 * PRIVATEOID subtypes we support.
 */
#define PRIVATEOIDS                                         \
	{ DST_ALG_RSASHA256PRIVATEOID, "RSASHA256OID", 0 }, \
		{ DST_ALG_RSASHA512PRIVATEOID, "RSASHA512OID", 0 },

/* RFC2535 section 7.1 */

#define SECPROTONAMES                                                     \
	{ 0, "NONE", 0 }, { 1, "TLS", 0 }, { 2, "EMAIL", 0 },             \
		{ 3, "DNSSEC", 0 }, { 4, "IPSEC", 0 }, { 255, "ALL", 0 }, \
		SENTINEL

#define HASHALGNAMES { 1, "SHA-1", 0 }, SENTINEL

/* RFC3658, RFC4509, RFC5933, RFC6605, RFC9558, RFC9563 */

#if defined(DNS_DSDIGEST_SHA256PRIVATE) &&     \
	defined(DNS_DSDIGEST_SHA384PRIVATE) && \
	defined(DNS_DSDIGEST_SM3PRIVATE)
#define DSDIGESTPRIVATENAMES                                          \
	{ DNS_DSDIGEST_SHA256PRIVATE, "SHA-256-PRIVATE", 0 },         \
		{ DNS_DSDIGEST_SHA256PRIVATE, "SHA256PRIVATE", 0 },   \
		{ DNS_DSDIGEST_SHA384PRIVATE, "SHA-384-PRIVATE", 0 }, \
		{ DNS_DSDIGEST_SHA384PRIVATE, "SHA384PRIVATE", 0 },   \
		{ DNS_DSDIGEST_SM3PRIVATE, "SM3-PRIVATE", 0 },        \
		{ DNS_DSDIGEST_SM3PRIVATE, "SM3PRIVATE", 0 },
#else
#define DSDIGESTPRIVATENAMES
#endif

#define DSDIGESTNAMES                                                        \
	{ DNS_DSDIGEST_SHA1, "SHA-1", 0 }, { DNS_DSDIGEST_SHA1, "SHA1", 0 }, \
		{ DNS_DSDIGEST_SHA256, "SHA-256", 0 },                       \
		{ DNS_DSDIGEST_SHA256, "SHA256", 0 },                        \
		{ DNS_DSDIGEST_GOST, "GOST", 0 },                            \
		{ DNS_DSDIGEST_SM3, "SM3", 0 },                              \
		{ DNS_DSDIGEST_SHA384, "SHA-384", 0 },                       \
		{ DNS_DSDIGEST_SHA384, "SHA384", 0 },                        \
		{ DNS_DSDIGEST_GOST2012, "GOST-2012", 0 },                   \
		{ DNS_DSDIGEST_GOST2012, "GOST2012", 0 },                    \
		DSDIGESTPRIVATENAMES SENTINEL

struct tbl {
	unsigned int value;
	const char *name;
	int flags;
};

static struct tbl rcodes[] = { RCODENAMES ERCODENAMES };
static struct tbl tsigrcodes[] = { RCODENAMES TSIGRCODENAMES };
static struct tbl certs[] = { CERTNAMES };
static struct tbl secalgs[] = { SECALGNAMES };
static struct tbl secprotos[] = { SECPROTONAMES };
static struct tbl hashalgs[] = { HASHALGNAMES };
static struct tbl dsdigests[] = { DSDIGESTNAMES };
static struct tbl privatednss[] = { PRIVATEDNSS SENTINEL };
static struct tbl privateoids[] = { PRIVATEOIDS SENTINEL };
static struct tbl dstalgorithms[] = { PRIVATEDNSS PRIVATEOIDS SECALGNAMES };

static struct keyflag {
	const char *name;
	unsigned int value;
	unsigned int mask;
} keyflags[] = { { "NOCONF", 0x4000, 0xC000 },
		 { "NOAUTH", 0x8000, 0xC000 },
		 { "NOKEY", 0xC000, 0xC000 },
		 { "FLAG2", 0x2000, 0x2000 },
		 { "EXTEND", 0x1000, 0x1000 },
		 { "FLAG4", 0x0800, 0x0800 },
		 { "FLAG5", 0x0400, 0x0400 },
		 { "USER", 0x0000, 0x0300 },
		 { "ZONE", 0x0100, 0x0300 },
		 { "HOST", 0x0200, 0x0300 },
		 { "NTYP3", 0x0300, 0x0300 },
		 { "FLAG8", 0x0080, 0x0080 },
		 { "FLAG9", 0x0040, 0x0040 },
		 { "FLAG10", 0x0020, 0x0020 },
		 { "FLAG11", 0x0010, 0x0010 },
		 { "SIG0", 0x0000, 0x000F },
		 { "SIG1", 0x0001, 0x000F },
		 { "SIG2", 0x0002, 0x000F },
		 { "SIG3", 0x0003, 0x000F },
		 { "SIG4", 0x0004, 0x000F },
		 { "SIG5", 0x0005, 0x000F },
		 { "SIG6", 0x0006, 0x000F },
		 { "SIG7", 0x0007, 0x000F },
		 { "SIG8", 0x0008, 0x000F },
		 { "SIG9", 0x0009, 0x000F },
		 { "SIG10", 0x000A, 0x000F },
		 { "SIG11", 0x000B, 0x000F },
		 { "SIG12", 0x000C, 0x000F },
		 { "SIG13", 0x000D, 0x000F },
		 { "SIG14", 0x000E, 0x000F },
		 { "SIG15", 0x000F, 0x000F },
		 { "KSK", DNS_KEYFLAG_KSK, DNS_KEYFLAG_KSK },
		 { NULL, 0, 0 } };

static isc_result_t
str_totext(const char *source, isc_buffer_t *target) {
	unsigned int l;
	isc_region_t region;

	isc_buffer_availableregion(target, &region);
	l = strlen(source);

	if (l > region.length) {
		return ISC_R_NOSPACE;
	}

	memmove(region.base, source, l);
	isc_buffer_add(target, l);
	return ISC_R_SUCCESS;
}

static isc_result_t
maybe_numeric(unsigned int *valuep, isc_textregion_t *source, unsigned int max,
	      bool hex_allowed) {
	isc_result_t result;
	uint32_t n;
	char buffer[NUMBERSIZE];
	int v;

	if (!isdigit((unsigned char)source->base[0]) ||
	    source->length > NUMBERSIZE - 1)
	{
		return ISC_R_BADNUMBER;
	}

	/*
	 * We have a potential number.	Try to parse it with
	 * isc_parse_uint32().	isc_parse_uint32() requires
	 * null termination, so we must make a copy.
	 */
	v = snprintf(buffer, sizeof(buffer), "%.*s", (int)source->length,
		     source->base);
	if (v < 0 || (unsigned int)v != source->length) {
		return ISC_R_BADNUMBER;
	}
	INSIST(buffer[source->length] == '\0');

	result = isc_parse_uint32(&n, buffer, 10);
	if (result == ISC_R_BADNUMBER && hex_allowed) {
		result = isc_parse_uint32(&n, buffer, 16);
	}
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	if (n > max) {
		return ISC_R_RANGE;
	}
	*valuep = n;
	return ISC_R_SUCCESS;
}

static isc_result_t
dns_mnemonic_fromtext(unsigned int *valuep, isc_textregion_t *source,
		      struct tbl *table, unsigned int max) {
	isc_result_t result;
	int i;

	result = maybe_numeric(valuep, source, max, false);
	if (result != ISC_R_BADNUMBER) {
		return result;
	}

	for (i = 0; table[i].name != NULL; i++) {
		unsigned int n;
		n = strlen(table[i].name);
		if (n == source->length && (table[i].flags & TOTEXTONLY) == 0 &&
		    strncasecmp(source->base, table[i].name, n) == 0)
		{
			*valuep = table[i].value;
			return ISC_R_SUCCESS;
		}
	}
	return DNS_R_UNKNOWN;
}

static isc_result_t
dns_mnemonic_totext(unsigned int value, isc_buffer_t *target,
		    struct tbl *table) {
	int i = 0;
	char buf[sizeof("4294967296")];
	while (table[i].name != NULL) {
		if (table[i].value == value) {
			return str_totext(table[i].name, target);
		}
		i++;
	}
	snprintf(buf, sizeof(buf), "%u", value);
	return str_totext(buf, target);
}

isc_result_t
dns_rcode_fromtext(dns_rcode_t *rcodep, isc_textregion_t *source) {
	unsigned int value;
	RETERR(dns_mnemonic_fromtext(&value, source, rcodes, 0xffff));
	*rcodep = value;
	return ISC_R_SUCCESS;
}

isc_result_t
dns_rcode_totext(dns_rcode_t rcode, isc_buffer_t *target) {
	return dns_mnemonic_totext(rcode, target, rcodes);
}

isc_result_t
dns_tsigrcode_fromtext(dns_rcode_t *rcodep, isc_textregion_t *source) {
	unsigned int value;
	RETERR(dns_mnemonic_fromtext(&value, source, tsigrcodes, 0xffff));
	*rcodep = value;
	return ISC_R_SUCCESS;
}

isc_result_t
dns_tsigrcode_totext(dns_rcode_t rcode, isc_buffer_t *target) {
	return dns_mnemonic_totext(rcode, target, tsigrcodes);
}

isc_result_t
dns_cert_fromtext(dns_cert_t *certp, isc_textregion_t *source) {
	unsigned int value;
	RETERR(dns_mnemonic_fromtext(&value, source, certs, 0xffff));
	*certp = value;
	return ISC_R_SUCCESS;
}

isc_result_t
dns_cert_totext(dns_cert_t cert, isc_buffer_t *target) {
	return dns_mnemonic_totext(cert, target, certs);
}

isc_result_t
dns_secalg_fromtext(dns_secalg_t *secalgp, isc_textregion_t *source) {
	unsigned int value;
	RETERR(dns_mnemonic_fromtext(&value, source, secalgs, 0xff));
	*secalgp = value;
	return ISC_R_SUCCESS;
}

isc_result_t
dns_secalg_totext(dns_secalg_t secalg, isc_buffer_t *target) {
	return dns_mnemonic_totext(secalg, target, secalgs);
}

void
dns_secalg_format(dns_secalg_t alg, char *cp, unsigned int size) {
	isc_buffer_t b;
	isc_region_t r;
	isc_result_t result;

	REQUIRE(cp != NULL && size > 0);
	isc_buffer_init(&b, cp, size - 1);
	result = dns_secalg_totext(alg, &b);
	isc_buffer_usedregion(&b, &r);
	r.base[r.length] = 0;
	if (result != ISC_R_SUCCESS) {
		r.base[0] = 0;
	}
}

isc_result_t
dst_privatedns_fromtext(dst_algorithm_t *dstalgp, isc_textregion_t *source) {
	unsigned int value;
	RETERR(dns_mnemonic_fromtext(&value, source, privatednss, 0));
	*dstalgp = value;
	return ISC_R_SUCCESS;
}

isc_result_t
dns_privatedns_totext(dst_algorithm_t alg, isc_buffer_t *target) {
	return dns_mnemonic_totext(alg, target, privatednss);
}

void
dns_privatedns_format(dst_algorithm_t alg, char *cp, unsigned int size) {
	isc_buffer_t b;
	isc_region_t r;
	isc_result_t result;

	REQUIRE(cp != NULL && size > 0);
	isc_buffer_init(&b, cp, size - 1);
	result = dns_privatedns_totext(alg, &b);
	isc_buffer_usedregion(&b, &r);
	r.base[r.length] = 0;
	if (result != ISC_R_SUCCESS) {
		r.base[0] = 0;
	}
}

isc_result_t
dst_privateoid_fromtext(dst_algorithm_t *dstalgp, isc_textregion_t *source) {
	unsigned int value;
	RETERR(dns_mnemonic_fromtext(&value, source, privateoids, 0));
	*dstalgp = value;
	return ISC_R_SUCCESS;
}

isc_result_t
dns_privateoid_totext(dst_algorithm_t alg, isc_buffer_t *target) {
	return dns_mnemonic_totext(alg, target, privateoids);
}

void
dns_privateoid_format(dst_algorithm_t alg, char *cp, unsigned int size) {
	isc_buffer_t b;
	isc_region_t r;
	isc_result_t result;

	REQUIRE(cp != NULL && size > 0);
	isc_buffer_init(&b, cp, size - 1);
	result = dns_privateoid_totext(alg, &b);
	isc_buffer_usedregion(&b, &r);
	r.base[r.length] = 0;
	if (result != ISC_R_SUCCESS) {
		r.base[0] = 0;
	}
}

isc_result_t
dns_secproto_fromtext(dns_secproto_t *secprotop, isc_textregion_t *source) {
	unsigned int value;
	RETERR(dns_mnemonic_fromtext(&value, source, secprotos, 0xff));
	*secprotop = value;
	return ISC_R_SUCCESS;
}

isc_result_t
dns_secproto_totext(dns_secproto_t secproto, isc_buffer_t *target) {
	return dns_mnemonic_totext(secproto, target, secprotos);
}

isc_result_t
dns_hashalg_fromtext(unsigned char *hashalg, isc_textregion_t *source) {
	unsigned int value;
	RETERR(dns_mnemonic_fromtext(&value, source, hashalgs, 0xff));
	*hashalg = value;
	return ISC_R_SUCCESS;
}

isc_result_t
dns_keyflags_fromtext(dns_keyflags_t *flagsp, isc_textregion_t *source) {
	isc_result_t result;
	char *text, *end;
	unsigned int value = 0;
#ifdef notyet
	unsigned int mask = 0;
#endif /* ifdef notyet */

	result = maybe_numeric(&value, source, 0xffff, true);
	if (result == ISC_R_SUCCESS) {
		*flagsp = value;
		return ISC_R_SUCCESS;
	}
	if (result != ISC_R_BADNUMBER) {
		return result;
	}

	text = source->base;
	end = source->base + source->length;

	while (text < end) {
		struct keyflag *p;
		unsigned int len;
		char *delim = memchr(text, '|', end - text);
		if (delim != NULL) {
			len = (unsigned int)(delim - text);
		} else {
			len = (unsigned int)(end - text);
		}
		for (p = keyflags; p->name != NULL; p++) {
			if (strncasecmp(p->name, text, len) == 0) {
				break;
			}
		}
		if (p->name == NULL) {
			return DNS_R_UNKNOWNFLAG;
		}
		value |= p->value;
#ifdef notyet
		if ((mask & p->mask) != 0) {
			warn("overlapping key flags");
		}
		mask |= p->mask;
#endif /* ifdef notyet */
		text += len;
		if (delim != NULL) {
			text++; /* Skip "|" */
		}
	}
	*flagsp = value;
	return ISC_R_SUCCESS;
}

isc_result_t
dns_dsdigest_fromtext(dns_dsdigest_t *dsdigestp, isc_textregion_t *source) {
	unsigned int value;
	RETERR(dns_mnemonic_fromtext(&value, source, dsdigests, 0xff));
	*dsdigestp = value;
	return ISC_R_SUCCESS;
}

isc_result_t
dns_dsdigest_totext(dns_dsdigest_t dsdigest, isc_buffer_t *target) {
	return dns_mnemonic_totext(dsdigest, target, dsdigests);
}

void
dns_dsdigest_format(dns_dsdigest_t typ, char *cp, unsigned int size) {
	isc_buffer_t b;
	isc_region_t r;
	isc_result_t result;

	REQUIRE(cp != NULL && size > 0);
	isc_buffer_init(&b, cp, size - 1);
	result = dns_dsdigest_totext(typ, &b);
	isc_buffer_usedregion(&b, &r);
	r.base[r.length] = 0;
	if (result != ISC_R_SUCCESS) {
		r.base[0] = 0;
	}
}

/*
 * This uses lots of hard coded values, but how often do we actually
 * add classes?
 */
isc_result_t
dns_rdataclass_fromtext(dns_rdataclass_t *classp, isc_textregion_t *source) {
#define COMPARE(string, rdclass)                                      \
	if (((sizeof(string) - 1) == source->length) &&               \
	    (strncasecmp(source->base, string, source->length) == 0)) \
	{                                                             \
		*classp = rdclass;                                    \
		return (ISC_R_SUCCESS);                               \
	}

	switch (isc_ascii_tolower(source->base[0])) {
	case 'a':
		COMPARE("any", dns_rdataclass_any);
		break;
	case 'c':
		/*
		 * RFC1035 says the mnemonic for the CHAOS class is CH,
		 * but historical BIND practice is to call it CHAOS.
		 * We will accept both forms, but only generate CH.
		 */
		COMPARE("ch", dns_rdataclass_chaos);
		COMPARE("chaos", dns_rdataclass_chaos);

		if (source->length > 5 &&
		    source->length < (5 + sizeof("65000")) &&
		    strncasecmp("class", source->base, 5) == 0)
		{
			char buf[sizeof("65000")];
			char *endp;
			unsigned int val;

			/*
			 * source->base is not required to be NUL terminated.
			 * Copy up to remaining bytes and NUL terminate.
			 */
			snprintf(buf, sizeof(buf), "%.*s",
				 (int)(source->length - 5), source->base + 5);
			val = strtoul(buf, &endp, 10);
			if (*endp == '\0' && val <= 0xffff) {
				*classp = (dns_rdataclass_t)val;
				return ISC_R_SUCCESS;
			}
		}
		break;
	case 'h':
		COMPARE("hs", dns_rdataclass_hs);
		COMPARE("hesiod", dns_rdataclass_hs);
		break;
	case 'i':
		COMPARE("in", dns_rdataclass_in);
		break;
	case 'n':
		COMPARE("none", dns_rdataclass_none);
		break;
	case 'r':
		COMPARE("reserved0", dns_rdataclass_reserved0);
		break;
	}

#undef COMPARE

	return DNS_R_UNKNOWN;
}

isc_result_t
dns_rdataclass_totext(dns_rdataclass_t rdclass, isc_buffer_t *target) {
	switch (rdclass) {
	case dns_rdataclass_any:
		return str_totext("ANY", target);
	case dns_rdataclass_chaos:
		return str_totext("CH", target);
	case dns_rdataclass_hs:
		return str_totext("HS", target);
	case dns_rdataclass_in:
		return str_totext("IN", target);
	case dns_rdataclass_none:
		return str_totext("NONE", target);
	case dns_rdataclass_reserved0:
		return str_totext("RESERVED0", target);
	default:
		return dns_rdataclass_tounknowntext(rdclass, target);
	}
}

isc_result_t
dns_rdataclass_tounknowntext(dns_rdataclass_t rdclass, isc_buffer_t *target) {
	char buf[sizeof("CLASS65535")];

	snprintf(buf, sizeof(buf), "CLASS%u", rdclass);
	return str_totext(buf, target);
}

void
dns_rdataclass_format(dns_rdataclass_t rdclass, char *array,
		      unsigned int size) {
	isc_result_t result;
	isc_buffer_t buf;

	if (size == 0U) {
		return;
	}

	isc_buffer_init(&buf, array, size);
	result = dns_rdataclass_totext(rdclass, &buf);
	/*
	 * Null terminate.
	 */
	if (result == ISC_R_SUCCESS) {
		if (isc_buffer_availablelength(&buf) >= 1) {
			isc_buffer_putuint8(&buf, 0);
		} else {
			result = ISC_R_NOSPACE;
		}
	}
	if (result != ISC_R_SUCCESS) {
		strlcpy(array, "<unknown>", size);
	}
}

isc_result_t
dst_algorithm_fromtext(dst_algorithm_t *dstalgp, isc_textregion_t *source) {
	unsigned int value;
	RETERR(dns_mnemonic_fromtext(&value, source, dstalgorithms, 255));
	*dstalgp = value;
	return ISC_R_SUCCESS;
}

isc_result_t
dst_algorithm_totext(dst_algorithm_t alg, isc_buffer_t *target) {
	return dns_mnemonic_totext(alg, target, dstalgorithms);
}

void
dst_algorithm_format(dst_algorithm_t alg, char *cp, unsigned int size) {
	isc_buffer_t b;
	isc_region_t r;
	isc_result_t result;

	REQUIRE(cp != NULL && size > 0);
	isc_buffer_init(&b, cp, size - 1);
	result = dst_algorithm_totext(alg, &b);
	isc_buffer_usedregion(&b, &r);
	r.base[r.length] = 0;
	if (result != ISC_R_SUCCESS) {
		r.base[0] = 0;
	}
}
