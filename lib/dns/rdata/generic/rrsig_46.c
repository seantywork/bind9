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

/* RFC2535 */

#ifndef RDATA_GENERIC_RRSIG_46_C
#define RDATA_GENERIC_RRSIG_46_C

#define RRTYPE_RRSIG_ATTRIBUTES                                     \
	(DNS_RDATATYPEATTR_DNSSEC | DNS_RDATATYPEATTR_ZONECUTAUTH | \
	 DNS_RDATATYPEATTR_ATCNAME)

static isc_result_t
fromtext_rrsig(ARGS_FROMTEXT) {
	isc_token_t token;
	unsigned char alg, c;
	long i;
	dns_rdatatype_t covered;
	char *e;
	isc_result_t result;
	isc_buffer_t buffer;
	uint32_t time_signed, time_expire;
	unsigned int used;

	REQUIRE(type == dns_rdatatype_rrsig);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(callbacks);

	/*
	 * Type covered.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_string,
				      false));
	result = dns_rdatatype_fromtext(&covered, &token.value.as_textregion);
	if (result != ISC_R_SUCCESS && result != ISC_R_NOTIMPLEMENTED) {
		i = strtol(DNS_AS_STR(token), &e, 10);
		if (i < 0 || i > 65535) {
			RETTOK(ISC_R_RANGE);
		}
		if (*e != 0) {
			RETTOK(result);
		}
		covered = (dns_rdatatype_t)i;
	}
	RETERR(uint16_tobuffer(covered, target));

	/*
	 * Algorithm.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_string,
				      false));
	RETTOK(dns_secalg_fromtext(&alg, &token.value.as_textregion));
	RETERR(mem_tobuffer(target, &alg, 1));

	/*
	 * Labels.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	if (token.value.as_ulong > 0xffU) {
		RETTOK(ISC_R_RANGE);
	}
	c = (unsigned char)token.value.as_ulong;
	RETERR(mem_tobuffer(target, &c, 1));

	/*
	 * Original ttl.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	RETERR(uint32_tobuffer(token.value.as_ulong, target));

	/*
	 * Signature expiration.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_string,
				      false));
	if (strlen(DNS_AS_STR(token)) <= 10U && *DNS_AS_STR(token) != '-' &&
	    *DNS_AS_STR(token) != '+')
	{
		char *end;
		unsigned long u;
		uint64_t u64;

		u64 = u = strtoul(DNS_AS_STR(token), &end, 10);
		if (u == ULONG_MAX || *end != 0) {
			RETTOK(DNS_R_SYNTAX);
		}
		if (u64 > 0xffffffffUL) {
			RETTOK(ISC_R_RANGE);
		}
		time_expire = u;
	} else {
		RETTOK(dns_time32_fromtext(DNS_AS_STR(token), &time_expire));
	}
	RETERR(uint32_tobuffer(time_expire, target));

	/*
	 * Time signed.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_string,
				      false));
	if (strlen(DNS_AS_STR(token)) <= 10U && *DNS_AS_STR(token) != '-' &&
	    *DNS_AS_STR(token) != '+')
	{
		char *end;
		unsigned long u;
		uint64_t u64;

		u64 = u = strtoul(DNS_AS_STR(token), &end, 10);
		if (u == ULONG_MAX || *end != 0) {
			RETTOK(DNS_R_SYNTAX);
		}
		if (u64 > 0xffffffffUL) {
			RETTOK(ISC_R_RANGE);
		}
		time_signed = u;
	} else {
		RETTOK(dns_time32_fromtext(DNS_AS_STR(token), &time_signed));
	}
	RETERR(uint32_tobuffer(time_signed, target));

	/*
	 * Key footprint.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	RETERR(uint16_tobuffer(token.value.as_ulong, target));

	/*
	 * Signer.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_string,
				      false));
	buffer_fromregion(&buffer, &token.value.as_region);
	if (origin == NULL) {
		origin = dns_rootname;
	}
	RETTOK(dns_name_wirefromtext(&buffer, origin, options, target));

	/*
	 * Sig.
	 */
	used = isc_buffer_usedlength(target);

	RETERR(isc_base64_tobuffer(lexer, target, -2));

	if (alg == DNS_KEYALG_PRIVATEDNS || alg == DNS_KEYALG_PRIVATEOID) {
		isc_buffer_t b;

		/*
		 * Set up 'b' so that the signature data can be parsed.
		 */
		b = *target;
		b.active = b.used;
		b.current = used;

		RETERR(check_private(&b, alg));
	}

	return ISC_R_SUCCESS;
}

static isc_result_t
totext_rrsig(ARGS_TOTEXT) {
	isc_region_t sr;
	char buf[sizeof("4294967295")]; /* Also TYPE65000. */
	dns_rdatatype_t covered;
	unsigned long ttl;
	unsigned long when;
	unsigned long exp;
	unsigned long foot;
	dns_name_t name;

	REQUIRE(rdata->type == dns_rdatatype_rrsig);
	REQUIRE(rdata->length != 0);

	dns_rdata_toregion(rdata, &sr);

	/*
	 * Type covered.
	 */
	covered = uint16_fromregion(&sr);
	isc_region_consume(&sr, 2);
	/*
	 * XXXAG We should have something like dns_rdatatype_isknown()
	 * that does the right thing with type 0.
	 */
	if (dns_rdatatype_isknown(covered) && covered != 0) {
		RETERR(dns_rdatatype_totext(covered, target));
	} else {
		snprintf(buf, sizeof(buf), "TYPE%u", covered);
		RETERR(str_totext(buf, target));
	}
	RETERR(str_totext(" ", target));

	/*
	 * Algorithm.
	 */
	snprintf(buf, sizeof(buf), "%u", sr.base[0]);
	isc_region_consume(&sr, 1);
	RETERR(str_totext(buf, target));
	RETERR(str_totext(" ", target));

	/*
	 * Labels.
	 */
	snprintf(buf, sizeof(buf), "%u", sr.base[0]);
	isc_region_consume(&sr, 1);
	RETERR(str_totext(buf, target));
	RETERR(str_totext(" ", target));

	/*
	 * Ttl.
	 */
	ttl = uint32_fromregion(&sr);
	isc_region_consume(&sr, 4);
	snprintf(buf, sizeof(buf), "%lu", ttl);
	RETERR(str_totext(buf, target));

	if ((tctx->flags & DNS_STYLEFLAG_MULTILINE) != 0) {
		RETERR(str_totext(" (", target));
	}
	RETERR(str_totext(tctx->linebreak, target));

	/*
	 * Sig exp.
	 */
	exp = uint32_fromregion(&sr);
	isc_region_consume(&sr, 4);
	RETERR(dns_time32_totext(exp, target));
	RETERR(str_totext(" ", target));

	/*
	 * Time signed.
	 */
	when = uint32_fromregion(&sr);
	isc_region_consume(&sr, 4);
	RETERR(dns_time32_totext(when, target));
	RETERR(str_totext(" ", target));

	/*
	 * Footprint.
	 */
	foot = uint16_fromregion(&sr);
	isc_region_consume(&sr, 2);
	snprintf(buf, sizeof(buf), "%lu", foot);
	RETERR(str_totext(buf, target));
	RETERR(str_totext(" ", target));

	/*
	 * Signer.
	 */
	dns_name_init(&name);
	dns_name_fromregion(&name, &sr);
	isc_region_consume(&sr, name_length(&name));
	RETERR(dns_name_totext(&name, 0, target));

	/*
	 * Sig.
	 */
	RETERR(str_totext(tctx->linebreak, target));
	if ((tctx->flags & DNS_STYLEFLAG_NOCRYPTO) == 0) {
		if (tctx->width == 0) { /* No splitting */
			RETERR(isc_base64_totext(&sr, 60, "", target));
		} else {
			RETERR(isc_base64_totext(&sr, tctx->width - 2,
						 tctx->linebreak, target));
		}
	} else {
		RETERR(str_totext("[omitted]", target));
	}

	if ((tctx->flags & DNS_STYLEFLAG_MULTILINE) != 0) {
		RETERR(str_totext(" )", target));
	}

	return ISC_R_SUCCESS;
}

static isc_result_t
fromwire_rrsig(ARGS_FROMWIRE) {
	isc_region_t sr;
	dns_name_t name;
	unsigned char algorithm;

	REQUIRE(type == dns_rdatatype_rrsig);

	UNUSED(type);
	UNUSED(rdclass);

	dctx = dns_decompress_setpermitted(dctx, false);

	isc_buffer_activeregion(source, &sr);
	/*
	 * type covered: 2
	 * algorithm: 1
	 * labels: 1
	 * original ttl: 4
	 * signature expiration: 4
	 * time signed: 4
	 * key footprint: 2
	 */
	if (sr.length < 18) {
		return ISC_R_UNEXPECTEDEND;
	}

	algorithm = sr.base[2];

	isc_buffer_forward(source, 18);
	RETERR(mem_tobuffer(target, sr.base, 18));

	/*
	 * Signer.
	 */
	dns_name_init(&name);
	RETERR(dns_name_fromwire(&name, source, dctx, target));

	/*
	 * Sig.
	 */
	isc_buffer_activeregion(source, &sr);
	if (sr.length < 1) {
		return DNS_R_FORMERR;
	}

	if (algorithm == DNS_KEYALG_PRIVATEDNS ||
	    algorithm == DNS_KEYALG_PRIVATEOID)
	{
		isc_buffer_t b = *source;
		RETERR(check_private(&b, algorithm));
	}

	isc_buffer_forward(source, sr.length);
	return mem_tobuffer(target, sr.base, sr.length);
}

static isc_result_t
towire_rrsig(ARGS_TOWIRE) {
	isc_region_t sr;
	dns_name_t name;

	REQUIRE(rdata->type == dns_rdatatype_rrsig);
	REQUIRE(rdata->length != 0);

	dns_compress_setpermitted(cctx, false);
	dns_rdata_toregion(rdata, &sr);
	/*
	 * type covered: 2
	 * algorithm: 1
	 * labels: 1
	 * original ttl: 4
	 * signature expiration: 4
	 * time signed: 4
	 * key footprint: 2
	 */
	RETERR(mem_tobuffer(target, sr.base, 18));
	isc_region_consume(&sr, 18);

	/*
	 * Signer.
	 */
	dns_name_init(&name);
	dns_name_fromregion(&name, &sr);
	isc_region_consume(&sr, name_length(&name));
	RETERR(dns_name_towire(&name, cctx, target));

	/*
	 * Signature.
	 */
	return mem_tobuffer(target, sr.base, sr.length);
}

static int
compare_rrsig(ARGS_COMPARE) {
	isc_region_t r1;
	isc_region_t r2;
	dns_name_t name1;
	dns_name_t name2;
	int order;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_rrsig);
	REQUIRE(rdata1->length != 0);
	REQUIRE(rdata2->length != 0);

	dns_rdata_toregion(rdata1, &r1);
	dns_rdata_toregion(rdata2, &r2);

	INSIST(r1.length > 18);
	INSIST(r2.length > 18);
	r1.length = 18;
	r2.length = 18;
	order = isc_region_compare(&r1, &r2);
	if (order != 0) {
		return order;
	}

	dns_name_init(&name1);
	dns_name_init(&name2);
	dns_rdata_toregion(rdata1, &r1);
	dns_rdata_toregion(rdata2, &r2);
	isc_region_consume(&r1, 18);
	isc_region_consume(&r2, 18);
	dns_name_fromregion(&name1, &r1);
	dns_name_fromregion(&name2, &r2);
	order = dns_name_rdatacompare(&name1, &name2);
	if (order != 0) {
		return order;
	}

	isc_region_consume(&r1, name_length(&name1));
	isc_region_consume(&r2, name_length(&name2));

	return isc_region_compare(&r1, &r2);
}

static isc_result_t
fromstruct_rrsig(ARGS_FROMSTRUCT) {
	dns_rdata_rrsig_t *sig = source;

	REQUIRE(type == dns_rdatatype_rrsig);
	REQUIRE(sig != NULL);
	REQUIRE(sig->common.rdtype == type);
	REQUIRE(sig->common.rdclass == rdclass);
	REQUIRE(sig->signature != NULL || sig->siglen == 0);

	UNUSED(type);
	UNUSED(rdclass);

	/*
	 * Type covered.
	 */
	RETERR(uint16_tobuffer(sig->covered, target));

	/*
	 * Algorithm.
	 */
	RETERR(uint8_tobuffer(sig->algorithm, target));

	/*
	 * Labels.
	 */
	RETERR(uint8_tobuffer(sig->labels, target));

	/*
	 * Original TTL.
	 */
	RETERR(uint32_tobuffer(sig->originalttl, target));

	/*
	 * Expire time.
	 */
	RETERR(uint32_tobuffer(sig->timeexpire, target));

	/*
	 * Time signed.
	 */
	RETERR(uint32_tobuffer(sig->timesigned, target));

	/*
	 * Key ID.
	 */
	RETERR(uint16_tobuffer(sig->keyid, target));

	/*
	 * Signer name.
	 */
	RETERR(name_tobuffer(&sig->signer, target));

	/*
	 * Signature.
	 */
	return mem_tobuffer(target, sig->signature, sig->siglen);
}

static isc_result_t
tostruct_rrsig(ARGS_TOSTRUCT) {
	isc_region_t sr;
	dns_rdata_rrsig_t *sig = target;
	dns_name_t signer;

	REQUIRE(rdata->type == dns_rdatatype_rrsig);
	REQUIRE(sig != NULL);
	REQUIRE(rdata->length != 0);

	sig->common.rdclass = rdata->rdclass;
	sig->common.rdtype = rdata->type;

	dns_rdata_toregion(rdata, &sr);

	/*
	 * Type covered.
	 */
	sig->covered = uint16_fromregion(&sr);
	isc_region_consume(&sr, 2);

	/*
	 * Algorithm.
	 */
	sig->algorithm = uint8_fromregion(&sr);
	isc_region_consume(&sr, 1);

	/*
	 * Labels.
	 */
	sig->labels = uint8_fromregion(&sr);
	isc_region_consume(&sr, 1);

	/*
	 * Original TTL.
	 */
	sig->originalttl = uint32_fromregion(&sr);
	isc_region_consume(&sr, 4);

	/*
	 * Expire time.
	 */
	sig->timeexpire = uint32_fromregion(&sr);
	isc_region_consume(&sr, 4);

	/*
	 * Time signed.
	 */
	sig->timesigned = uint32_fromregion(&sr);
	isc_region_consume(&sr, 4);

	/*
	 * Key ID.
	 */
	sig->keyid = uint16_fromregion(&sr);
	isc_region_consume(&sr, 2);

	dns_name_init(&signer);
	dns_name_fromregion(&signer, &sr);
	dns_name_init(&sig->signer);
	name_duporclone(&signer, mctx, &sig->signer);
	isc_region_consume(&sr, name_length(&sig->signer));

	/*
	 * Signature.
	 */
	sig->siglen = sr.length;
	sig->signature = mem_maybedup(mctx, sr.base, sig->siglen);
	sig->mctx = mctx;
	return ISC_R_SUCCESS;
}

static void
freestruct_rrsig(ARGS_FREESTRUCT) {
	dns_rdata_rrsig_t *sig = (dns_rdata_rrsig_t *)source;

	REQUIRE(sig != NULL);
	REQUIRE(sig->common.rdtype == dns_rdatatype_rrsig);

	if (sig->mctx == NULL) {
		return;
	}

	dns_name_free(&sig->signer, sig->mctx);
	if (sig->signature != NULL) {
		isc_mem_free(sig->mctx, sig->signature);
	}
	sig->mctx = NULL;
}

static isc_result_t
additionaldata_rrsig(ARGS_ADDLDATA) {
	REQUIRE(rdata->type == dns_rdatatype_rrsig);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(add);
	UNUSED(arg);

	return ISC_R_SUCCESS;
}

static isc_result_t
digest_rrsig(ARGS_DIGEST) {
	isc_region_t r1, r2;
	dns_name_t name;

	REQUIRE(rdata->type == dns_rdatatype_rrsig);

	dns_rdata_toregion(rdata, &r1);
	r2 = r1;

	/*
	 * Type covered (2) + Algorithm (1) +
	 * Labels (1) + Original TTL (4) +
	 * Expire time (4) +  Time signed (4) +
	 * Key ID (2).
	 */
	isc_region_consume(&r2, 18);
	r1.length = 18;
	RETERR((digest)(arg, &r1));

	/* Signer */
	dns_name_init(&name);
	dns_name_fromregion(&name, &r2);
	RETERR(dns_name_digest(&name, digest, arg));
	isc_region_consume(&r2, name_length(&name));

	/* Signature */
	return (digest)(arg, &r2);
}

static dns_rdatatype_t
covers_rrsig(dns_rdata_t *rdata) {
	dns_rdatatype_t type;
	isc_region_t r;

	REQUIRE(rdata->type == dns_rdatatype_rrsig);

	dns_rdata_toregion(rdata, &r);
	type = uint16_fromregion(&r);

	return type;
}

static bool
checkowner_rrsig(ARGS_CHECKOWNER) {
	REQUIRE(type == dns_rdatatype_rrsig);

	UNUSED(name);
	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(wildcard);

	return true;
}

static bool
checknames_rrsig(ARGS_CHECKNAMES) {
	REQUIRE(rdata->type == dns_rdatatype_rrsig);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(bad);

	return true;
}

static int
casecompare_rrsig(ARGS_COMPARE) {
	return compare_rrsig(rdata1, rdata2);
}

#endif /* RDATA_GENERIC_RRSIG_46_C */
