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

/* RFC 4701 */

#ifndef RDATA_IN_1_DHCID_49_C
#define RDATA_IN_1_DHCID_49_C 1

#define RRTYPE_DHCID_ATTRIBUTES 0

static isc_result_t
fromtext_in_dhcid(ARGS_FROMTEXT) {
	REQUIRE(type == dns_rdatatype_dhcid);
	REQUIRE(rdclass == dns_rdataclass_in);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(origin);
	UNUSED(options);
	UNUSED(callbacks);

	return isc_base64_tobuffer(lexer, target, -2);
}

static isc_result_t
totext_in_dhcid(ARGS_TOTEXT) {
	isc_region_t sr, sr2;
	/* " ; 64000 255 64000" */
	char buf[5 + 3 * 11 + 1];

	REQUIRE(rdata->type == dns_rdatatype_dhcid);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);
	REQUIRE(rdata->length != 0);

	dns_rdata_toregion(rdata, &sr);
	sr2 = sr;

	if ((tctx->flags & DNS_STYLEFLAG_MULTILINE) != 0) {
		RETERR(str_totext("( " /*)*/, target));
	}
	if (tctx->width == 0) { /* No splitting */
		RETERR(isc_base64_totext(&sr, 60, "", target));
	} else {
		RETERR(isc_base64_totext(&sr, tctx->width - 2, tctx->linebreak,
					 target));
	}
	if ((tctx->flags & DNS_STYLEFLAG_MULTILINE) != 0) {
		RETERR(str_totext(/* ( */ " )", target));
		if (rdata->length > 2) {
			snprintf(buf, sizeof(buf), " ; %u %u %u",
				 sr2.base[0] * 256U + sr2.base[1], sr2.base[2],
				 rdata->length - 3U);
			RETERR(str_totext(buf, target));
		}
	}
	return ISC_R_SUCCESS;
}

static isc_result_t
fromwire_in_dhcid(ARGS_FROMWIRE) {
	isc_region_t sr;

	REQUIRE(type == dns_rdatatype_dhcid);
	REQUIRE(rdclass == dns_rdataclass_in);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(dctx);

	isc_buffer_activeregion(source, &sr);
	if (sr.length == 0) {
		return ISC_R_UNEXPECTEDEND;
	}

	isc_buffer_forward(source, sr.length);
	return mem_tobuffer(target, sr.base, sr.length);
}

static isc_result_t
towire_in_dhcid(ARGS_TOWIRE) {
	isc_region_t sr;

	REQUIRE(rdata->type == dns_rdatatype_dhcid);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);
	REQUIRE(rdata->length != 0);

	UNUSED(cctx);

	dns_rdata_toregion(rdata, &sr);
	return mem_tobuffer(target, sr.base, sr.length);
}

static int
compare_in_dhcid(ARGS_COMPARE) {
	isc_region_t r1;
	isc_region_t r2;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_dhcid);
	REQUIRE(rdata1->rdclass == dns_rdataclass_in);
	REQUIRE(rdata1->length != 0);
	REQUIRE(rdata2->length != 0);

	dns_rdata_toregion(rdata1, &r1);
	dns_rdata_toregion(rdata2, &r2);
	return isc_region_compare(&r1, &r2);
}

static isc_result_t
fromstruct_in_dhcid(ARGS_FROMSTRUCT) {
	dns_rdata_in_dhcid_t *dhcid = source;

	REQUIRE(type == dns_rdatatype_dhcid);
	REQUIRE(rdclass == dns_rdataclass_in);
	REQUIRE(dhcid != NULL);
	REQUIRE(dhcid->common.rdtype == type);
	REQUIRE(dhcid->common.rdclass == rdclass);
	REQUIRE(dhcid->length != 0);

	UNUSED(type);
	UNUSED(rdclass);

	return mem_tobuffer(target, dhcid->dhcid, dhcid->length);
}

static isc_result_t
tostruct_in_dhcid(ARGS_TOSTRUCT) {
	dns_rdata_in_dhcid_t *dhcid = target;
	isc_region_t region;

	REQUIRE(rdata->type == dns_rdatatype_dhcid);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);
	REQUIRE(dhcid != NULL);
	REQUIRE(rdata->length != 0);

	dhcid->common.rdclass = rdata->rdclass;
	dhcid->common.rdtype = rdata->type;

	dns_rdata_toregion(rdata, &region);

	dhcid->dhcid = mem_maybedup(mctx, region.base, region.length);
	dhcid->mctx = mctx;
	return ISC_R_SUCCESS;
}

static void
freestruct_in_dhcid(ARGS_FREESTRUCT) {
	dns_rdata_in_dhcid_t *dhcid = source;

	REQUIRE(dhcid != NULL);
	REQUIRE(dhcid->common.rdtype == dns_rdatatype_dhcid);
	REQUIRE(dhcid->common.rdclass == dns_rdataclass_in);

	if (dhcid->mctx == NULL) {
		return;
	}

	if (dhcid->dhcid != NULL) {
		isc_mem_free(dhcid->mctx, dhcid->dhcid);
	}
	dhcid->mctx = NULL;
}

static isc_result_t
additionaldata_in_dhcid(ARGS_ADDLDATA) {
	REQUIRE(rdata->type == dns_rdatatype_dhcid);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(add);
	UNUSED(arg);

	return ISC_R_SUCCESS;
}

static isc_result_t
digest_in_dhcid(ARGS_DIGEST) {
	isc_region_t r;

	REQUIRE(rdata->type == dns_rdatatype_dhcid);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);

	dns_rdata_toregion(rdata, &r);

	return (digest)(arg, &r);
}

static bool
checkowner_in_dhcid(ARGS_CHECKOWNER) {
	REQUIRE(type == dns_rdatatype_dhcid);
	REQUIRE(rdclass == dns_rdataclass_in);

	UNUSED(name);
	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(wildcard);

	return true;
}

static bool
checknames_in_dhcid(ARGS_CHECKNAMES) {
	REQUIRE(rdata->type == dns_rdatatype_dhcid);
	REQUIRE(rdata->rdclass == dns_rdataclass_in);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(bad);

	return true;
}

static int
casecompare_in_dhcid(ARGS_COMPARE) {
	return compare_in_dhcid(rdata1, rdata2);
}

#endif /* RDATA_IN_1_DHCID_49_C */
