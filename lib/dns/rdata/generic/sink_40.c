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

#ifndef RDATA_GENERIC_SINK_40_C
#define RDATA_GENERIC_SINK_40_C

#include <dst/dst.h>

#define RRTYPE_SINK_ATTRIBUTES (0)

static isc_result_t
fromtext_sink(ARGS_FROMTEXT) {
	isc_token_t token;

	REQUIRE(type == dns_rdatatype_sink);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(origin);
	UNUSED(options);
	UNUSED(callbacks);

	/* meaning */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	if (token.value.as_ulong > 0xffU) {
		RETTOK(ISC_R_RANGE);
	}
	RETERR(uint8_tobuffer(token.value.as_ulong, target));

	/* coding */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	if (token.value.as_ulong > 0xffU) {
		RETTOK(ISC_R_RANGE);
	}
	RETERR(uint8_tobuffer(token.value.as_ulong, target));

	/* subcoding */
	RETERR(isc_lex_getmastertoken(lexer, &token, isc_tokentype_number,
				      false));
	if (token.value.as_ulong > 0xffU) {
		RETTOK(ISC_R_RANGE);
	}
	RETERR(uint8_tobuffer(token.value.as_ulong, target));

	return isc_base64_tobuffer(lexer, target, -1);
}

static isc_result_t
totext_sink(ARGS_TOTEXT) {
	isc_region_t sr;
	char buf[sizeof("255 255 255")];
	uint8_t meaning, coding, subcoding;

	REQUIRE(rdata->type == dns_rdatatype_sink);
	REQUIRE(rdata->length >= 3);

	dns_rdata_toregion(rdata, &sr);

	/* Meaning, Coding and Subcoding */
	meaning = uint8_fromregion(&sr);
	isc_region_consume(&sr, 1);
	coding = uint8_fromregion(&sr);
	isc_region_consume(&sr, 1);
	subcoding = uint8_fromregion(&sr);
	isc_region_consume(&sr, 1);
	snprintf(buf, sizeof(buf), "%u %u %u", meaning, coding, subcoding);
	RETERR(str_totext(buf, target));

	if (sr.length == 0U) {
		return ISC_R_SUCCESS;
	}

	/* data */
	if ((tctx->flags & DNS_STYLEFLAG_MULTILINE) != 0) {
		RETERR(str_totext(" (", target));
	}

	RETERR(str_totext(tctx->linebreak, target));

	if (tctx->width == 0) { /* No splitting */
		RETERR(isc_base64_totext(&sr, 60, "", target));
	} else {
		RETERR(isc_base64_totext(&sr, tctx->width - 2, tctx->linebreak,
					 target));
	}

	if ((tctx->flags & DNS_STYLEFLAG_MULTILINE) != 0) {
		RETERR(str_totext(" )", target));
	}

	return ISC_R_SUCCESS;
}

static isc_result_t
fromwire_sink(ARGS_FROMWIRE) {
	isc_region_t sr;

	REQUIRE(type == dns_rdatatype_sink);

	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(dctx);

	isc_buffer_activeregion(source, &sr);
	if (sr.length < 3) {
		return ISC_R_UNEXPECTEDEND;
	}

	RETERR(mem_tobuffer(target, sr.base, sr.length));
	isc_buffer_forward(source, sr.length);
	return ISC_R_SUCCESS;
}

static isc_result_t
towire_sink(ARGS_TOWIRE) {
	REQUIRE(rdata->type == dns_rdatatype_sink);
	REQUIRE(rdata->length >= 3);

	UNUSED(cctx);

	return mem_tobuffer(target, rdata->data, rdata->length);
}

static int
compare_sink(ARGS_COMPARE) {
	isc_region_t r1;
	isc_region_t r2;

	REQUIRE(rdata1->type == rdata2->type);
	REQUIRE(rdata1->rdclass == rdata2->rdclass);
	REQUIRE(rdata1->type == dns_rdatatype_sink);
	REQUIRE(rdata1->length >= 3);
	REQUIRE(rdata2->length >= 3);

	dns_rdata_toregion(rdata1, &r1);
	dns_rdata_toregion(rdata2, &r2);
	return isc_region_compare(&r1, &r2);
}

static isc_result_t
fromstruct_sink(ARGS_FROMSTRUCT) {
	dns_rdata_sink_t *sink = source;

	REQUIRE(type == dns_rdatatype_sink);
	REQUIRE(sink != NULL);
	REQUIRE(sink->common.rdtype == type);
	REQUIRE(sink->common.rdclass == rdclass);

	UNUSED(type);
	UNUSED(rdclass);

	/* Meaning */
	RETERR(uint8_tobuffer(sink->meaning, target));

	/* Coding */
	RETERR(uint8_tobuffer(sink->coding, target));

	/* Subcoding */
	RETERR(uint8_tobuffer(sink->subcoding, target));

	/* Data */
	return mem_tobuffer(target, sink->data, sink->datalen);
}

static isc_result_t
tostruct_sink(ARGS_TOSTRUCT) {
	dns_rdata_sink_t *sink = target;
	isc_region_t sr;

	REQUIRE(rdata->type == dns_rdatatype_sink);
	REQUIRE(sink != NULL);
	REQUIRE(rdata->length >= 3);

	sink->common.rdclass = rdata->rdclass;
	sink->common.rdtype = rdata->type;

	dns_rdata_toregion(rdata, &sr);

	/* Meaning */
	sink->meaning = uint8_fromregion(&sr);
	isc_region_consume(&sr, 1);

	/* Coding */
	sink->coding = uint8_fromregion(&sr);
	isc_region_consume(&sr, 1);

	/* Subcoding */
	sink->subcoding = uint8_fromregion(&sr);
	isc_region_consume(&sr, 1);

	/* Data */
	sink->datalen = sr.length;
	sink->data = mem_maybedup(mctx, sr.base, sink->datalen);
	sink->mctx = mctx;
	return ISC_R_SUCCESS;
}

static void
freestruct_sink(ARGS_FREESTRUCT) {
	dns_rdata_sink_t *sink = (dns_rdata_sink_t *)source;

	REQUIRE(sink != NULL);
	REQUIRE(sink->common.rdtype == dns_rdatatype_sink);

	if (sink->mctx == NULL) {
		return;
	}

	if (sink->data != NULL) {
		isc_mem_free(sink->mctx, sink->data);
	}
	sink->mctx = NULL;
}

static isc_result_t
additionaldata_sink(ARGS_ADDLDATA) {
	REQUIRE(rdata->type == dns_rdatatype_sink);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(add);
	UNUSED(arg);

	return ISC_R_SUCCESS;
}

static isc_result_t
digest_sink(ARGS_DIGEST) {
	isc_region_t r;

	REQUIRE(rdata->type == dns_rdatatype_sink);

	dns_rdata_toregion(rdata, &r);

	return (digest)(arg, &r);
}

static bool
checkowner_sink(ARGS_CHECKOWNER) {
	REQUIRE(type == dns_rdatatype_sink);

	UNUSED(name);
	UNUSED(type);
	UNUSED(rdclass);
	UNUSED(wildcard);

	return true;
}

static bool
checknames_sink(ARGS_CHECKNAMES) {
	REQUIRE(rdata->type == dns_rdatatype_sink);

	UNUSED(rdata);
	UNUSED(owner);
	UNUSED(bad);

	return true;
}

static int
casecompare_sink(ARGS_COMPARE) {
	return compare_sink(rdata1, rdata2);
}
#endif /* RDATA_GENERIC_SINK_40_C */
