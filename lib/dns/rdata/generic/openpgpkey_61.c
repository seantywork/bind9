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

#ifndef RDATA_GENERIC_OPENPGPKEY_61_C
#define RDATA_GENERIC_OPENPGPKEY_61_C

#define RRTYPE_OPENPGPKEY_ATTRIBUTES 0

static isc_result_t
fromtext_openpgpkey(ARGS_FROMTEXT) {
	REQUIRE(type == dns_rdatatype_openpgpkey);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(callbacks);
	UNUSED(options);
	UNUSED(origin);

	/*
	 * Keyring.
	 */
	return isc_base64_tobuffer(lexer, target, -2);
}

static isc_result_t
totext_openpgpkey(ARGS_TOTEXT) {
	isc_region_t sr;

	REQUIRE(rdata->type == dns_rdatatype_openpgpkey);
	REQUIRE(rdata->length != 0);

	dns_rdata_toregion(rdata, &sr);

	/*
	 * Keyring
	 */
	if ((tctx->flags & DNS_STYLEFLAG_MULTILINE) != 0) {
		RETERR(str_totext("( ", target));
	}

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
fromwire_openpgpkey(ARGS_FROMWIRE) {
	isc_region_t sr;

	REQUIRE(type == dns_rdatatype_openpgpkey);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(dctx);

	/*
	 * Keyring.
	 */
	isc_buffer_activeregion(source, &sr);
	if (sr.length < 1) {
		return ISC_R_UNEXPECTEDEND;
	}
	isc_buffer_forward(source, sr.length);
	return mem_tobuffer(target, sr.base, sr.length);
}

static isc_result_t
towire_openpgpkey(ARGS_TOWIRE) {
	isc_region_t sr;

	REQUIRE(rdata->type == dns_rdatatype_openpgpkey);
	REQUIRE(rdata->length != 0);

	UNUSED(cctx);

	dns_rdata_toregion(rdata, &sr);
	return mem_tobuffer(target, sr.base, sr.length);
}

static int
compare_openpgpkey(ARGS_COMPARE) {
	isc_region_t r1;
	isc_region_t r2;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_openpgpkey);
	REQUIRE(rdata1->length != 0);
	REQUIRE(rdata2->length != 0);

	dns_rdata_toregion(rdata1, &r1);
	dns_rdata_toregion(rdata2, &r2);
	return isc_region_compare(&r1, &r2);
}

static isc_result_t
fromstruct_openpgpkey(ARGS_FROMSTRUCT) {
	dns_rdata_openpgpkey_t *sig = source;

	REQUIRE(type == dns_rdatatype_openpgpkey);
	REQUIRE(sig != NULL);
	REQUIRE(sig->common.rdtype == type);
	REQUIRE(sig->common.rdclass == rdclass);
	REQUIRE(sig->keyring != NULL && sig->length != 0);

	UNUSED(type);
	UNUSED(rdclass);

	/*
	 * Keyring.
	 */
	return mem_tobuffer(target, sig->keyring, sig->length);
}

static isc_result_t
tostruct_openpgpkey(ARGS_TOSTRUCT) {
	isc_region_t sr;
	dns_rdata_openpgpkey_t *sig = target;

	REQUIRE(rdata->type == dns_rdatatype_openpgpkey);
	REQUIRE(sig != NULL);
	REQUIRE(rdata->length != 0);

	sig->common.rdclass = rdata->rdclass;
	sig->common.rdtype = rdata->type;

	dns_rdata_toregion(rdata, &sr);

	/*
	 * Keyring.
	 */
	sig->length = sr.length;
	sig->keyring = mem_maybedup(mctx, sr.base, sig->length);
	sig->mctx = mctx;
	return ISC_R_SUCCESS;
}

static void
freestruct_openpgpkey(ARGS_FREESTRUCT) {
	dns_rdata_openpgpkey_t *sig = (dns_rdata_openpgpkey_t *)source;

	REQUIRE(sig != NULL);
	REQUIRE(sig->common.rdtype == dns_rdatatype_openpgpkey);

	if (sig->mctx == NULL) {
		return;
	}

	if (sig->keyring != NULL) {
		isc_mem_free(sig->mctx, sig->keyring);
	}
	sig->mctx = NULL;
}

static isc_result_t
additionaldata_openpgpkey(ARGS_ADDLDATA) {
	REQUIRE(rdata->type == dns_rdatatype_openpgpkey);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(add);
	UNUSED(arg);

	return ISC_R_SUCCESS;
}

static isc_result_t
digest_openpgpkey(ARGS_DIGEST) {
	isc_region_t r;

	REQUIRE(rdata->type == dns_rdatatype_openpgpkey);

	dns_rdata_toregion(rdata, &r);

	return (digest)(arg, &r);
}

static bool
checkowner_openpgpkey(ARGS_CHECKOWNER) {
	REQUIRE(type == dns_rdatatype_openpgpkey);

	UNUSED(name);
	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(wildcard);

	return true;
}

static bool
checknames_openpgpkey(ARGS_CHECKNAMES) {
	REQUIRE(rdata->type == dns_rdatatype_openpgpkey);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(bad);

	return true;
}

static int
casecompare_openpgpkey(ARGS_COMPARE) {
	isc_region_t r1;
	isc_region_t r2;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_openpgpkey);
	REQUIRE(rdata1->length != 0);
	REQUIRE(rdata2->length != 0);

	dns_rdata_toregion(rdata1, &r1);
	dns_rdata_toregion(rdata2, &r2);

	return isc_region_compare(&r1, &r2);
}

#endif /* RDATA_GENERIC_OPENPGPKEY_61_C */
