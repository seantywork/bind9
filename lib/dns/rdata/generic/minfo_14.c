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

#ifndef RDATA_GENERIC_MINFO_14_C
#define RDATA_GENERIC_MINFO_14_C

#define RRTYPE_MINFO_ATTRIBUTES (0)

static isc_result_t
fromtext_minfo(ARGS_FROMTEXT) {
	isc_token_t token;
	dns_fixedname_t fn;
	dns_name_t *name = dns_fixedname_initname(&fn);
	isc_buffer_t buffer;
	int i;
	bool ok;

	REQUIRE(type == dns_rdatatype_minfo);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(callbacks);

	if (origin == NULL) {
		origin = dns_rootname;
	}

	for (i = 0; i < 2; i++) {
		RETERR(isc_lex_getmastertoken(lexer, &token,
					      isc_tokentype_string, false));
		buffer_fromregion(&buffer, &token.value.as_region);
		RETTOK(dns_name_fromtext(name, &buffer, origin, options));
		RETTOK(dns_name_towire(name, NULL, target));
		ok = true;
		if ((options & DNS_RDATA_CHECKNAMES) != 0) {
			ok = dns_name_ismailbox(name);
		}
		if (!ok && (options & DNS_RDATA_CHECKNAMESFAIL) != 0) {
			RETTOK(DNS_R_BADNAME);
		}
		if (!ok && callbacks != NULL) {
			warn_badname(name, lexer, callbacks);
		}
	}
	return ISC_R_SUCCESS;
}

static isc_result_t
totext_minfo(ARGS_TOTEXT) {
	isc_region_t region;
	dns_name_t rmail;
	dns_name_t email;
	dns_name_t prefix;
	unsigned int opts;

	REQUIRE(rdata->type == dns_rdatatype_minfo);
	REQUIRE(rdata->length != 0);

	dns_name_init(&rmail);
	dns_name_init(&email);
	dns_name_init(&prefix);

	dns_rdata_toregion(rdata, &region);

	dns_name_fromregion(&rmail, &region);
	isc_region_consume(&region, rmail.length);

	dns_name_fromregion(&email, &region);
	isc_region_consume(&region, email.length);

	opts = name_prefix(&rmail, tctx->origin, &prefix)
		       ? DNS_NAME_OMITFINALDOT
		       : 0;
	RETERR(dns_name_totext(&prefix, opts, target));

	RETERR(str_totext(" ", target));

	opts = name_prefix(&email, tctx->origin, &prefix)
		       ? DNS_NAME_OMITFINALDOT
		       : 0;
	return dns_name_totext(&prefix, opts, target);
}

static isc_result_t
fromwire_minfo(ARGS_FROMWIRE) {
	dns_name_t rmail;
	dns_name_t email;

	REQUIRE(type == dns_rdatatype_minfo);

	UNUSED(type);
	UNUSED(rdclass);

	dctx = dns_decompress_setpermitted(dctx, true);

	dns_name_init(&rmail);
	dns_name_init(&email);

	RETERR(dns_name_fromwire(&rmail, source, dctx, target));
	return dns_name_fromwire(&email, source, dctx, target);
}

static isc_result_t
towire_minfo(ARGS_TOWIRE) {
	isc_region_t region;
	dns_name_t rmail;
	dns_name_t email;

	REQUIRE(rdata->type == dns_rdatatype_minfo);
	REQUIRE(rdata->length != 0);

	dns_compress_setpermitted(cctx, true);

	dns_name_init(&rmail);
	dns_name_init(&email);

	dns_rdata_toregion(rdata, &region);

	dns_name_fromregion(&rmail, &region);
	isc_region_consume(&region, name_length(&rmail));

	RETERR(dns_name_towire(&rmail, cctx, target));

	dns_name_fromregion(&rmail, &region);
	isc_region_consume(&region, rmail.length);

	return dns_name_towire(&rmail, cctx, target);
}

static int
compare_minfo(ARGS_COMPARE) {
	isc_region_t region1;
	isc_region_t region2;
	dns_name_t name1;
	dns_name_t name2;
	int order;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_minfo);
	REQUIRE(rdata1->length != 0);
	REQUIRE(rdata2->length != 0);

	dns_name_init(&name1);
	dns_name_init(&name2);

	dns_rdata_toregion(rdata1, &region1);
	dns_rdata_toregion(rdata2, &region2);

	dns_name_fromregion(&name1, &region1);
	dns_name_fromregion(&name2, &region2);

	order = dns_name_rdatacompare(&name1, &name2);
	if (order != 0) {
		return order;
	}

	isc_region_consume(&region1, name_length(&name1));
	isc_region_consume(&region2, name_length(&name2));

	dns_name_init(&name1);
	dns_name_init(&name2);

	dns_name_fromregion(&name1, &region1);
	dns_name_fromregion(&name2, &region2);

	order = dns_name_rdatacompare(&name1, &name2);
	return order;
}

static isc_result_t
fromstruct_minfo(ARGS_FROMSTRUCT) {
	dns_rdata_minfo_t *minfo = source;
	isc_region_t region;

	REQUIRE(type == dns_rdatatype_minfo);
	REQUIRE(minfo != NULL);
	REQUIRE(minfo->common.rdtype == type);
	REQUIRE(minfo->common.rdclass == rdclass);

	UNUSED(type);
	UNUSED(rdclass);

	dns_name_toregion(&minfo->rmailbox, &region);
	RETERR(isc_buffer_copyregion(target, &region));
	dns_name_toregion(&minfo->emailbox, &region);
	return isc_buffer_copyregion(target, &region);
}

static isc_result_t
tostruct_minfo(ARGS_TOSTRUCT) {
	dns_rdata_minfo_t *minfo = target;
	isc_region_t region;
	dns_name_t name;

	REQUIRE(rdata->type == dns_rdatatype_minfo);
	REQUIRE(minfo != NULL);
	REQUIRE(rdata->length != 0);

	minfo->common.rdclass = rdata->rdclass;
	minfo->common.rdtype = rdata->type;

	dns_name_init(&name);
	dns_rdata_toregion(rdata, &region);
	dns_name_fromregion(&name, &region);
	dns_name_init(&minfo->rmailbox);
	name_duporclone(&name, mctx, &minfo->rmailbox);
	isc_region_consume(&region, name_length(&name));

	dns_name_fromregion(&name, &region);
	dns_name_init(&minfo->emailbox);
	name_duporclone(&name, mctx, &minfo->emailbox);
	minfo->mctx = mctx;
	return ISC_R_SUCCESS;
}

static void
freestruct_minfo(ARGS_FREESTRUCT) {
	dns_rdata_minfo_t *minfo = source;

	REQUIRE(minfo != NULL);
	REQUIRE(minfo->common.rdtype == dns_rdatatype_minfo);

	if (minfo->mctx == NULL) {
		return;
	}

	dns_name_free(&minfo->rmailbox, minfo->mctx);
	dns_name_free(&minfo->emailbox, minfo->mctx);
	minfo->mctx = NULL;
}

static isc_result_t
additionaldata_minfo(ARGS_ADDLDATA) {
	REQUIRE(rdata->type == dns_rdatatype_minfo);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(add);
	UNUSED(arg);

	return ISC_R_SUCCESS;
}

static isc_result_t
digest_minfo(ARGS_DIGEST) {
	isc_region_t r;
	dns_name_t name;
	isc_result_t result;

	REQUIRE(rdata->type == dns_rdatatype_minfo);

	dns_rdata_toregion(rdata, &r);
	dns_name_init(&name);
	dns_name_fromregion(&name, &r);
	result = dns_name_digest(&name, digest, arg);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	isc_region_consume(&r, name_length(&name));
	dns_name_init(&name);
	dns_name_fromregion(&name, &r);

	return dns_name_digest(&name, digest, arg);
}

static bool
checkowner_minfo(ARGS_CHECKOWNER) {
	REQUIRE(type == dns_rdatatype_minfo);

	UNUSED(name);
	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(wildcard);

	return true;
}

static bool
checknames_minfo(ARGS_CHECKNAMES) {
	isc_region_t region;
	dns_name_t name;

	REQUIRE(rdata->type == dns_rdatatype_minfo);

	UNUSED(owner);

	dns_rdata_toregion(rdata, &region);
	dns_name_init(&name);
	dns_name_fromregion(&name, &region);
	if (!dns_name_ismailbox(&name)) {
		if (bad != NULL) {
			dns_name_clone(&name, bad);
		}
		return false;
	}
	isc_region_consume(&region, name_length(&name));
	dns_name_fromregion(&name, &region);
	if (!dns_name_ismailbox(&name)) {
		if (bad != NULL) {
			dns_name_clone(&name, bad);
		}
		return false;
	}
	return true;
}

static int
casecompare_minfo(ARGS_COMPARE) {
	return compare_minfo(rdata1, rdata2);
}

#endif /* RDATA_GENERIC_MINFO_14_C */
