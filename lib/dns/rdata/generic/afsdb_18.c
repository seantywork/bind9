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

/* RFC1183 */

#ifndef RDATA_GENERIC_AFSDB_18_C
#define RDATA_GENERIC_AFSDB_18_C

#define RRTYPE_AFSDB_ATTRIBUTES (0)

static isc_result_t
fromtext_afsdb(ARGS_FROMTEXT) {
	isc_token_t token;
	isc_buffer_t buffer;
	dns_fixedname_t fn;
	dns_name_t *name = dns_fixedname_initname(&fn);
	bool ok;

	REQUIRE(type == dns_rdatatype_afsdb);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(callbacks);

	/*
	 * Subtype.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	if (token.value.as_ulong > 0xffffU) {
		RETTOK(ISC_R_RANGE);
	}
	RETERR(uint16_tobuffer(token.value.as_ulong, target));

	/*
	 * Hostname.
	 */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_string,
				      false));
	buffer_fromregion(&buffer, &token.value.as_region);
	if (origin == NULL) {
		origin = dns_rootname;
	}
	RETTOK(dns_name_fromtext(name, &buffer, origin, options));
	RETTOK(dns_name_towire(name, NULL, target));
	ok = true;
	if ((options & DNS_RDATA_CHECKNAMES) != 0) {
		ok = dns_name_ishostname(name, false);
	}
	if (!ok && (options & DNS_RDATA_CHECKNAMESFAIL) != 0) {
		RETTOK(DNS_R_BADNAME);
	}
	if (!ok && callbacks != NULL) {
		warn_badname(name, lexer, callbacks);
	}
	return ISC_R_SUCCESS;
}

static isc_result_t
totext_afsdb(ARGS_TOTEXT) {
	dns_name_t name;
	dns_name_t prefix;
	isc_region_t region;
	char buf[sizeof("64000 ")];
	unsigned int num, opts;

	REQUIRE(rdata->type == dns_rdatatype_afsdb);
	REQUIRE(rdata->length != 0);

	dns_name_init(&name);
	dns_name_init(&prefix);

	dns_rdata_toregion(rdata, &region);
	num = uint16_fromregion(&region);
	isc_region_consume(&region, 2);
	snprintf(buf, sizeof(buf), "%u ", num);
	RETERR(str_totext(buf, target));
	dns_name_fromregion(&name, &region);
	opts = name_prefix(&name, tctx->origin, &prefix) ? DNS_NAME_OMITFINALDOT
							 : 0;
	return dns_name_totext(&prefix, opts, target);
}

static isc_result_t
fromwire_afsdb(ARGS_FROMWIRE) {
	dns_name_t name;
	isc_region_t sr;
	isc_region_t tr;

	REQUIRE(type == dns_rdatatype_afsdb);

	UNUSED(type);
	UNUSED(rdclass);

	dctx = dns_decompress_setpermitted(dctx, false);

	dns_name_init(&name);

	isc_buffer_activeregion(source, &sr);
	isc_buffer_availableregion(target, &tr);
	if (tr.length < 2) {
		return ISC_R_NOSPACE;
	}
	if (sr.length < 2) {
		return ISC_R_UNEXPECTEDEND;
	}
	memmove(tr.base, sr.base, 2);
	isc_buffer_forward(source, 2);
	isc_buffer_add(target, 2);
	return dns_name_fromwire(&name, source, dctx, target);
}

static isc_result_t
towire_afsdb(ARGS_TOWIRE) {
	isc_region_t tr;
	isc_region_t sr;
	dns_name_t name;

	REQUIRE(rdata->type == dns_rdatatype_afsdb);
	REQUIRE(rdata->length != 0);

	dns_compress_setpermitted(cctx, false);
	isc_buffer_availableregion(target, &tr);
	dns_rdata_toregion(rdata, &sr);
	if (tr.length < 2) {
		return ISC_R_NOSPACE;
	}
	memmove(tr.base, sr.base, 2);
	isc_region_consume(&sr, 2);
	isc_buffer_add(target, 2);

	dns_name_init(&name);
	dns_name_fromregion(&name, &sr);

	return dns_name_towire(&name, cctx, target);
}

static int
compare_afsdb(ARGS_COMPARE) {
	int result;
	dns_name_t name1;
	dns_name_t name2;
	isc_region_t region1;
	isc_region_t region2;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_afsdb);
	REQUIRE(rdata1->length != 0);
	REQUIRE(rdata2->length != 0);

	result = memcmp(rdata1->data, rdata2->data, 2);
	if (result != 0) {
		return result < 0 ? -1 : 1;
	}

	dns_name_init(&name1);
	dns_name_init(&name2);

	dns_rdata_toregion(rdata1, &region1);
	dns_rdata_toregion(rdata2, &region2);

	isc_region_consume(&region1, 2);
	isc_region_consume(&region2, 2);

	dns_name_fromregion(&name1, &region1);
	dns_name_fromregion(&name2, &region2);

	return dns_name_rdatacompare(&name1, &name2);
}

static isc_result_t
fromstruct_afsdb(ARGS_FROMSTRUCT) {
	dns_rdata_afsdb_t *afsdb = source;
	isc_region_t region;

	REQUIRE(type == dns_rdatatype_afsdb);
	REQUIRE(afsdb != NULL);
	REQUIRE(afsdb->common.rdclass == rdclass);
	REQUIRE(afsdb->common.rdtype == type);

	UNUSED(type);
	UNUSED(rdclass);

	RETERR(uint16_tobuffer(afsdb->subtype, target));
	dns_name_toregion(&afsdb->server, &region);
	return isc_buffer_copyregion(target, &region);
}

static isc_result_t
tostruct_afsdb(ARGS_TOSTRUCT) {
	isc_region_t region;
	dns_rdata_afsdb_t *afsdb = target;
	dns_name_t name;

	REQUIRE(rdata->type == dns_rdatatype_afsdb);
	REQUIRE(afsdb != NULL);
	REQUIRE(rdata->length != 0);

	afsdb->common.rdclass = rdata->rdclass;
	afsdb->common.rdtype = rdata->type;

	dns_name_init(&afsdb->server);

	dns_rdata_toregion(rdata, &region);

	afsdb->subtype = uint16_fromregion(&region);
	isc_region_consume(&region, 2);

	dns_name_init(&name);
	dns_name_fromregion(&name, &region);

	name_duporclone(&name, mctx, &afsdb->server);
	afsdb->mctx = mctx;
	return ISC_R_SUCCESS;
}

static void
freestruct_afsdb(ARGS_FREESTRUCT) {
	dns_rdata_afsdb_t *afsdb = source;

	REQUIRE(afsdb != NULL);
	REQUIRE(afsdb->common.rdtype == dns_rdatatype_afsdb);

	if (afsdb->mctx == NULL) {
		return;
	}

	dns_name_free(&afsdb->server, afsdb->mctx);
	afsdb->mctx = NULL;
}

static isc_result_t
additionaldata_afsdb(ARGS_ADDLDATA) {
	dns_name_t name;
	isc_region_t region;

	REQUIRE(rdata->type == dns_rdatatype_afsdb);

	UNUSED(owner);

	dns_name_init(&name);
	dns_rdata_toregion(rdata, &region);
	isc_region_consume(&region, 2);
	dns_name_fromregion(&name, &region);

	return (add)(arg, &name, dns_rdatatype_a, NULL DNS__DB_FILELINE);
}

static isc_result_t
digest_afsdb(ARGS_DIGEST) {
	isc_region_t r1, r2;
	dns_name_t name;

	REQUIRE(rdata->type == dns_rdatatype_afsdb);

	dns_rdata_toregion(rdata, &r1);
	r2 = r1;
	isc_region_consume(&r2, 2);
	r1.length = 2;
	RETERR((digest)(arg, &r1));
	dns_name_init(&name);
	dns_name_fromregion(&name, &r2);

	return dns_name_digest(&name, digest, arg);
}

static bool
checkowner_afsdb(ARGS_CHECKOWNER) {
	REQUIRE(type == dns_rdatatype_afsdb);

	UNUSED(name);
	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(wildcard);

	return true;
}

static bool
checknames_afsdb(ARGS_CHECKNAMES) {
	isc_region_t region;
	dns_name_t name;

	REQUIRE(rdata->type == dns_rdatatype_afsdb);

	UNUSED(owner);

	dns_rdata_toregion(rdata, &region);
	isc_region_consume(&region, 2);
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
casecompare_afsdb(ARGS_COMPARE) {
	return compare_afsdb(rdata1, rdata2);
}
#endif /* RDATA_GENERIC_AFSDB_18_C */
