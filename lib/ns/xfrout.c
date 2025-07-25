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

#include <inttypes.h>
#include <stdbool.h>

#include <isc/formatcheck.h>
#include <isc/list.h>
#include <isc/log.h>
#include <isc/mem.h>
#include <isc/netmgr.h>
#include <isc/result.h>
#include <isc/stats.h>
#include <isc/util.h>

#include <dns/db.h>
#include <dns/dbiterator.h>
#include <dns/dlz.h>
#include <dns/fixedname.h>
#include <dns/journal.h>
#include <dns/message.h>
#include <dns/peer.h>
#include <dns/rdataclass.h>
#include <dns/rdatalist.h>
#include <dns/rdataset.h>
#include <dns/rdatasetiter.h>
#include <dns/rriterator.h>
#include <dns/soa.h>
#include <dns/stats.h>
#include <dns/tsig.h>
#include <dns/view.h>
#include <dns/zone.h>
#include <dns/zt.h>

#include <ns/client.h>
#include <ns/server.h>
#include <ns/stats.h>
#include <ns/xfrout.h>

/*! \file
 * \brief
 * Outgoing AXFR and IXFR.
 */

/*
 * TODO:
 *  - IXFR over UDP
 */

#define XFROUT_PROTOCOL_LOGARGS \
	DNS_LOGCATEGORY_XFER_OUT, NS_LOGMODULE_XFER_OUT, ISC_LOG_INFO

#define XFROUT_DEBUG_LOGARGS(n) \
	DNS_LOGCATEGORY_XFER_OUT, NS_LOGMODULE_XFER_OUT, ISC_LOG_DEBUG(n)

#define XFROUT_RR_LOGARGS \
	DNS_LOGCATEGORY_XFER_OUT, NS_LOGMODULE_XFER_OUT, XFROUT_RR_LOGLEVEL

#define XFROUT_RR_LOGLEVEL ISC_LOG_DEBUG(8)

/*%
 * Fail unconditionally and log as a client error.
 * The test against ISC_R_SUCCESS is there to keep the Solaris compiler
 * from complaining about "end-of-loop code not reached".
 */
#define FAILC(code, msg)                                                 \
	do {                                                             \
		result = (code);                                         \
		ns_client_log(client, DNS_LOGCATEGORY_XFER_OUT,          \
			      NS_LOGMODULE_XFER_OUT, ISC_LOG_INFO,       \
			      "bad zone transfer request: %s (%s)", msg, \
			      isc_result_totext(code));                  \
		if (result != ISC_R_SUCCESS)                             \
			goto failure;                                    \
	} while (0)

#define FAILQ(code, msg, question, rdclass)                                  \
	do {                                                                 \
		char _buf1[DNS_NAME_FORMATSIZE];                             \
		char _buf2[DNS_RDATACLASS_FORMATSIZE];                       \
		result = (code);                                             \
		dns_name_format(question, _buf1, sizeof(_buf1));             \
		dns_rdataclass_format(rdclass, _buf2, sizeof(_buf2));        \
		ns_client_log(client, DNS_LOGCATEGORY_XFER_OUT,              \
			      NS_LOGMODULE_XFER_OUT, ISC_LOG_INFO,           \
			      "bad zone transfer request: '%s/%s': %s (%s)", \
			      _buf1, _buf2, msg, isc_result_totext(code));   \
		if (result != ISC_R_SUCCESS)                                 \
			goto failure;                                        \
	} while (0)

#define CHECK(op)                            \
	do {                                 \
		result = (op);               \
		if (result != ISC_R_SUCCESS) \
			goto failure;        \
	} while (0)

/**************************************************************************/

static void
inc_stats(ns_client_t *client, dns_zone_t *zone, isc_statscounter_t counter) {
	ns_stats_increment(client->manager->sctx->nsstats, counter);
	if (zone != NULL) {
		isc_stats_t *zonestats = dns_zone_getrequeststats(zone);
		if (zonestats != NULL) {
			isc_stats_increment(zonestats, counter);
		}
	}
}

/**************************************************************************/

/*% Log an RR (for debugging) */

static void
log_rr(dns_name_t *name, dns_rdata_t *rdata, uint32_t ttl) {
	isc_result_t result;
	isc_buffer_t buf;
	char mem[2000];
	dns_rdatalist_t rdl;
	dns_rdataset_t rds;
	dns_rdata_t rd = DNS_RDATA_INIT;

	dns_rdatalist_init(&rdl);
	rdl.type = rdata->type;
	rdl.rdclass = rdata->rdclass;
	rdl.ttl = ttl;
	if (dns_rdatatype_issig(rdata->type)) {
		rdl.covers = dns_rdata_covers(rdata);
	} else {
		rdl.covers = dns_rdatatype_none;
	}
	dns_rdataset_init(&rds);
	dns_rdata_init(&rd);
	dns_rdata_clone(rdata, &rd);
	ISC_LIST_APPEND(rdl.rdata, &rd, link);
	dns_rdatalist_tordataset(&rdl, &rds);

	isc_buffer_init(&buf, mem, sizeof(mem));
	result = dns_rdataset_totext(&rds, name, false, false, &buf);

	/*
	 * We could use xfrout_log(), but that would produce
	 * very long lines with a repetitive prefix.
	 */
	if (result == ISC_R_SUCCESS) {
		/*
		 * Get rid of final newline.
		 */
		INSIST(buf.used >= 1 &&
		       ((char *)buf.base)[buf.used - 1] == '\n');
		buf.used--;

		isc_log_write(XFROUT_RR_LOGARGS, "%.*s",
			      (int)isc_buffer_usedlength(&buf),
			      (char *)isc_buffer_base(&buf));
	} else {
		isc_log_write(XFROUT_RR_LOGARGS, "<RR too large to print>");
	}
}

/**************************************************************************/
/*
 * An 'rrstream_t' is a polymorphic iterator that returns
 * a stream of resource records.  There are multiple implementations,
 * e.g. for generating AXFR and IXFR records streams.
 */

typedef struct rrstream_methods rrstream_methods_t;

typedef struct rrstream {
	isc_mem_t *mctx;
	rrstream_methods_t *methods;
} rrstream_t;

struct rrstream_methods {
	isc_result_t (*first)(rrstream_t *);
	isc_result_t (*next)(rrstream_t *);
	void (*current)(rrstream_t *, dns_name_t **, uint32_t *,
			dns_rdata_t **);
	void (*pause)(rrstream_t *);
	void (*destroy)(rrstream_t **);
};

static void
rrstream_noop_pause(rrstream_t *rs) {
	UNUSED(rs);
}

/**************************************************************************/
/*
 * An 'ixfr_rrstream_t' is an 'rrstream_t' that returns
 * an IXFR-like RR stream from a journal file.
 *
 * The SOA at the beginning of each sequence of additions
 * or deletions are included in the stream, but the extra
 * SOAs at the beginning and end of the entire transfer are
 * not included.
 */

typedef struct ixfr_rrstream {
	rrstream_t common;
	dns_journal_t *journal;
} ixfr_rrstream_t;

/* Forward declarations. */
static void
ixfr_rrstream_destroy(rrstream_t **sp);

static rrstream_methods_t ixfr_rrstream_methods;

/*
 * Returns: anything dns_journal_open() or dns_journal_iter_init()
 * may return.
 */

static isc_result_t
ixfr_rrstream_create(isc_mem_t *mctx, const char *journal_filename,
		     uint32_t begin_serial, uint32_t end_serial, size_t *sizep,
		     rrstream_t **sp) {
	isc_result_t result;
	ixfr_rrstream_t *s = NULL;

	INSIST(sp != NULL && *sp == NULL);

	s = isc_mem_get(mctx, sizeof(*s));
	s->common.mctx = NULL;
	isc_mem_attach(mctx, &s->common.mctx);
	s->common.methods = &ixfr_rrstream_methods;
	s->journal = NULL;

	CHECK(dns_journal_open(mctx, journal_filename, DNS_JOURNAL_READ,
			       &s->journal));
	CHECK(dns_journal_iter_init(s->journal, begin_serial, end_serial,
				    sizep));

	*sp = (rrstream_t *)s;
	return ISC_R_SUCCESS;

failure:
	ixfr_rrstream_destroy((rrstream_t **)(void *)&s);
	return result;
}

static isc_result_t
ixfr_rrstream_first(rrstream_t *rs) {
	ixfr_rrstream_t *s = (ixfr_rrstream_t *)rs;
	return dns_journal_first_rr(s->journal);
}

static isc_result_t
ixfr_rrstream_next(rrstream_t *rs) {
	ixfr_rrstream_t *s = (ixfr_rrstream_t *)rs;
	return dns_journal_next_rr(s->journal);
}

static void
ixfr_rrstream_current(rrstream_t *rs, dns_name_t **name, uint32_t *ttl,
		      dns_rdata_t **rdata) {
	ixfr_rrstream_t *s = (ixfr_rrstream_t *)rs;
	dns_journal_current_rr(s->journal, name, ttl, rdata);
}

static void
ixfr_rrstream_destroy(rrstream_t **rsp) {
	ixfr_rrstream_t *s = (ixfr_rrstream_t *)*rsp;
	if (s->journal != NULL) {
		dns_journal_destroy(&s->journal);
	}
	isc_mem_putanddetach(&s->common.mctx, s, sizeof(*s));
}

static rrstream_methods_t ixfr_rrstream_methods = {
	ixfr_rrstream_first, ixfr_rrstream_next, ixfr_rrstream_current,
	rrstream_noop_pause, ixfr_rrstream_destroy
};

/**************************************************************************/
/*
 * An 'axfr_rrstream_t' is an 'rrstream_t' that returns
 * an AXFR-like RR stream from a database.
 *
 * The SOAs at the beginning and end of the transfer are
 * not included in the stream.
 */

typedef struct axfr_rrstream {
	rrstream_t common;
	dns_rriterator_t it;
	bool it_valid;
} axfr_rrstream_t;

/*
 * Forward declarations.
 */
static void
axfr_rrstream_destroy(rrstream_t **rsp);

static rrstream_methods_t axfr_rrstream_methods;

static isc_result_t
axfr_rrstream_create(isc_mem_t *mctx, dns_db_t *db, dns_dbversion_t *ver,
		     rrstream_t **sp) {
	axfr_rrstream_t *s;
	isc_result_t result;

	INSIST(sp != NULL && *sp == NULL);

	s = isc_mem_get(mctx, sizeof(*s));
	s->common.mctx = NULL;
	isc_mem_attach(mctx, &s->common.mctx);
	s->common.methods = &axfr_rrstream_methods;
	s->it_valid = false;

	CHECK(dns_rriterator_init(&s->it, db, ver, 0));
	s->it_valid = true;

	*sp = (rrstream_t *)s;
	return ISC_R_SUCCESS;

failure:
	axfr_rrstream_destroy((rrstream_t **)(void *)&s);
	return result;
}

static isc_result_t
axfr_rrstream_first(rrstream_t *rs) {
	axfr_rrstream_t *s = (axfr_rrstream_t *)rs;
	isc_result_t result;
	result = dns_rriterator_first(&s->it);
	if (result != ISC_R_SUCCESS) {
		return result;
	}
	/* Skip SOA records. */
	for (;;) {
		dns_name_t *name_dummy = NULL;
		uint32_t ttl_dummy;
		dns_rdata_t *rdata = NULL;
		dns_rriterator_current(&s->it, &name_dummy, &ttl_dummy, NULL,
				       &rdata);
		if (rdata->type != dns_rdatatype_soa) {
			break;
		}
		result = dns_rriterator_next(&s->it);
		if (result != ISC_R_SUCCESS) {
			break;
		}
	}
	return result;
}

static isc_result_t
axfr_rrstream_next(rrstream_t *rs) {
	axfr_rrstream_t *s = (axfr_rrstream_t *)rs;
	isc_result_t result;

	/* Skip SOA records. */
	for (;;) {
		dns_name_t *name_dummy = NULL;
		uint32_t ttl_dummy;
		dns_rdata_t *rdata = NULL;
		result = dns_rriterator_next(&s->it);
		if (result != ISC_R_SUCCESS) {
			break;
		}
		dns_rriterator_current(&s->it, &name_dummy, &ttl_dummy, NULL,
				       &rdata);
		if (rdata->type != dns_rdatatype_soa) {
			break;
		}
	}
	return result;
}

static void
axfr_rrstream_current(rrstream_t *rs, dns_name_t **name, uint32_t *ttl,
		      dns_rdata_t **rdata) {
	axfr_rrstream_t *s = (axfr_rrstream_t *)rs;
	dns_rriterator_current(&s->it, name, ttl, NULL, rdata);
}

static void
axfr_rrstream_pause(rrstream_t *rs) {
	axfr_rrstream_t *s = (axfr_rrstream_t *)rs;
	dns_rriterator_pause(&s->it);
}

static void
axfr_rrstream_destroy(rrstream_t **rsp) {
	axfr_rrstream_t *s = (axfr_rrstream_t *)*rsp;
	if (s->it_valid) {
		dns_rriterator_destroy(&s->it);
	}
	isc_mem_putanddetach(&s->common.mctx, s, sizeof(*s));
}

static rrstream_methods_t axfr_rrstream_methods = {
	axfr_rrstream_first, axfr_rrstream_next, axfr_rrstream_current,
	axfr_rrstream_pause, axfr_rrstream_destroy
};

/**************************************************************************/
/*
 * An 'soa_rrstream_t' is a degenerate 'rrstream_t' that returns
 * a single SOA record.
 */

typedef struct soa_rrstream {
	rrstream_t common;
	dns_difftuple_t *soa_tuple;
} soa_rrstream_t;

/*
 * Forward declarations.
 */
static void
soa_rrstream_destroy(rrstream_t **rsp);

static rrstream_methods_t soa_rrstream_methods;

static isc_result_t
soa_rrstream_create(isc_mem_t *mctx, dns_db_t *db, dns_dbversion_t *ver,
		    rrstream_t **sp) {
	soa_rrstream_t *s;
	isc_result_t result;

	INSIST(sp != NULL && *sp == NULL);

	s = isc_mem_get(mctx, sizeof(*s));
	s->common.mctx = NULL;
	isc_mem_attach(mctx, &s->common.mctx);
	s->common.methods = &soa_rrstream_methods;
	s->soa_tuple = NULL;

	CHECK(dns_db_createsoatuple(db, ver, mctx, DNS_DIFFOP_EXISTS,
				    &s->soa_tuple));

	*sp = (rrstream_t *)s;
	return ISC_R_SUCCESS;

failure:
	soa_rrstream_destroy((rrstream_t **)(void *)&s);
	return result;
}

static isc_result_t
soa_rrstream_first(rrstream_t *rs) {
	UNUSED(rs);
	return ISC_R_SUCCESS;
}

static isc_result_t
soa_rrstream_next(rrstream_t *rs) {
	UNUSED(rs);
	return ISC_R_NOMORE;
}

static void
soa_rrstream_current(rrstream_t *rs, dns_name_t **name, uint32_t *ttl,
		     dns_rdata_t **rdata) {
	soa_rrstream_t *s = (soa_rrstream_t *)rs;
	*name = &s->soa_tuple->name;
	*ttl = s->soa_tuple->ttl;
	*rdata = &s->soa_tuple->rdata;
}

static void
soa_rrstream_destroy(rrstream_t **rsp) {
	soa_rrstream_t *s = (soa_rrstream_t *)*rsp;
	if (s->soa_tuple != NULL) {
		dns_difftuple_free(&s->soa_tuple);
	}
	isc_mem_putanddetach(&s->common.mctx, s, sizeof(*s));
}

static rrstream_methods_t soa_rrstream_methods = {
	soa_rrstream_first, soa_rrstream_next, soa_rrstream_current,
	rrstream_noop_pause, soa_rrstream_destroy
};

/**************************************************************************/
/*
 * A 'compound_rrstream_t' objects owns a soa_rrstream
 * and another rrstream, the "data stream".  It returns
 * a concatenated stream consisting of the soa_rrstream, then
 * the data stream, then the soa_rrstream again.
 *
 * The component streams are owned by the compound_rrstream_t
 * and are destroyed with it.
 */

typedef struct compound_rrstream {
	rrstream_t common;
	rrstream_t *components[3];
	int state;
	isc_result_t result;
} compound_rrstream_t;

/*
 * Forward declarations.
 */
static void
compound_rrstream_destroy(rrstream_t **rsp);

static isc_result_t
compound_rrstream_next(rrstream_t *rs);

static rrstream_methods_t compound_rrstream_methods;

/*
 * Requires:
 *	soa_stream != NULL && *soa_stream != NULL
 *	data_stream != NULL && *data_stream != NULL
 *	sp != NULL && *sp == NULL
 *
 * Ensures:
 *	*soa_stream == NULL
 *	*data_stream == NULL
 *	*sp points to a valid compound_rrstream_t
 *	The soa and data streams will be destroyed
 *	when the compound_rrstream_t is destroyed.
 */
static isc_result_t
compound_rrstream_create(isc_mem_t *mctx, rrstream_t **soa_stream,
			 rrstream_t **data_stream, rrstream_t **sp) {
	compound_rrstream_t *s;

	INSIST(sp != NULL && *sp == NULL);

	s = isc_mem_get(mctx, sizeof(*s));
	s->common.mctx = NULL;
	isc_mem_attach(mctx, &s->common.mctx);
	s->common.methods = &compound_rrstream_methods;
	s->components[0] = *soa_stream;
	s->components[1] = *data_stream;
	s->components[2] = *soa_stream;
	s->state = -1;
	s->result = ISC_R_FAILURE;

	*data_stream = NULL;
	*soa_stream = NULL;
	*sp = (rrstream_t *)s;
	return ISC_R_SUCCESS;
}

static isc_result_t
compound_rrstream_first(rrstream_t *rs) {
	compound_rrstream_t *s = (compound_rrstream_t *)rs;
	s->state = 0;
	do {
		rrstream_t *curstream = s->components[s->state];
		s->result = curstream->methods->first(curstream);
	} while (s->result == ISC_R_NOMORE && s->state < 2);
	return s->result;
}

static isc_result_t
compound_rrstream_next(rrstream_t *rs) {
	compound_rrstream_t *s = (compound_rrstream_t *)rs;
	rrstream_t *curstream = s->components[s->state];
	s->result = curstream->methods->next(curstream);
	while (s->result == ISC_R_NOMORE) {
		/*
		 * Make sure locks held by the current stream
		 * are released before we switch streams.
		 */
		curstream->methods->pause(curstream);
		if (s->state == 2) {
			return ISC_R_NOMORE;
		}
		s->state++;
		curstream = s->components[s->state];
		s->result = curstream->methods->first(curstream);
	}
	return s->result;
}

static void
compound_rrstream_current(rrstream_t *rs, dns_name_t **name, uint32_t *ttl,
			  dns_rdata_t **rdata) {
	compound_rrstream_t *s = (compound_rrstream_t *)rs;
	rrstream_t *curstream;
	INSIST(0 <= s->state && s->state < 3);
	INSIST(s->result == ISC_R_SUCCESS);
	curstream = s->components[s->state];
	curstream->methods->current(curstream, name, ttl, rdata);
}

static void
compound_rrstream_pause(rrstream_t *rs) {
	compound_rrstream_t *s = (compound_rrstream_t *)rs;
	rrstream_t *curstream;
	INSIST(0 <= s->state && s->state < 3);
	curstream = s->components[s->state];
	curstream->methods->pause(curstream);
}

static void
compound_rrstream_destroy(rrstream_t **rsp) {
	compound_rrstream_t *s = (compound_rrstream_t *)*rsp;
	s->components[0]->methods->destroy(&s->components[0]);
	s->components[1]->methods->destroy(&s->components[1]);
	s->components[2] = NULL; /* Copy of components[0]. */
	isc_mem_putanddetach(&s->common.mctx, s, sizeof(*s));
}

static rrstream_methods_t compound_rrstream_methods = {
	compound_rrstream_first, compound_rrstream_next,
	compound_rrstream_current, compound_rrstream_pause,
	compound_rrstream_destroy
};

/**************************************************************************/

/*%
 * Structure holding outgoing transfer statistics
 */
struct xfr_stats {
	uint64_t nmsg;	  /*%< Number of messages sent */
	uint64_t nrecs;	  /*%< Number of records sent */
	uint64_t nbytes;  /*%< Number of bytes sent */
	isc_time_t start; /*%< Start time of the transfer */
	isc_time_t end;	  /*%< End time of the transfer */
};

/*%
 * An 'xfrout_ctx_t' contains the state of an outgoing AXFR or IXFR
 * in progress.
 */
typedef struct {
	isc_mem_t *mctx;
	ns_client_t *client;
	unsigned int id;       /* ID of request */
	dns_name_t *qname;     /* Question name of request */
	dns_rdatatype_t qtype; /* dns_rdatatype_{a,i}xfr */
	dns_rdataclass_t qclass;
	dns_zone_t *zone; /* (necessary for stats) */
	dns_db_t *db;
	dns_dbversion_t *ver;
	rrstream_t *stream;  /* The XFR RR stream */
	bool question_added; /* QUESTION section sent? */
	bool end_of_stream;  /* EOS has been reached */
	isc_buffer_t buf;    /* Buffer for message owner
			      * names and rdatas */
	isc_buffer_t txbuf;  /* Transmit message buffer */
	size_t cbytes;	     /* Length of current message */
	void *txmem;
	unsigned int txmemlen;
	dns_tsigkey_t *tsigkey; /* Key used to create TSIG */
	isc_buffer_t *lasttsig; /* the last TSIG */
	bool verified_tsig;	/* verified request MAC */
	bool many_answers;
	int sends; /* Send in progress */
	bool shuttingdown;
	bool poll;
	const char *mnemonic;	/* Style of transfer */
	uint32_t end_serial;	/* Serial number after XFR is done */
	struct xfr_stats stats; /*%< Transfer statistics */

	/* Timeouts */
	uint64_t maxtime; /*%< Maximum XFR timeout (in ms) */
	isc_nm_timer_t *maxtime_timer;

	uint64_t idletime; /*%< XFR idle timeout (in ms) */

	/* Delayed send */
	isc_nm_timer_t *delayed_send_timer;
} xfrout_ctx_t;

static void
xfrout_ctx_create(isc_mem_t *mctx, ns_client_t *client, unsigned int id,
		  dns_name_t *qname, dns_rdatatype_t qtype,
		  dns_rdataclass_t qclass, dns_zone_t *zone, dns_db_t *db,
		  dns_dbversion_t *ver, rrstream_t *stream,
		  dns_tsigkey_t *tsigkey, isc_buffer_t *lasttsig,
		  bool verified_tsig, unsigned int maxtime,
		  unsigned int idletime, bool many_answers,
		  xfrout_ctx_t **xfrp);

static void
sendstream(xfrout_ctx_t *xfr);

static void
xfrout_senddone(isc_nmhandle_t *handle, isc_result_t result, void *arg);

static void
xfrout_fail(xfrout_ctx_t *xfr, isc_result_t result, const char *msg);

static void
xfrout_maybe_destroy(xfrout_ctx_t *xfr);

static void
xfrout_ctx_destroy(xfrout_ctx_t **xfrp);

static void
xfrout_client_timeout(void *arg, isc_result_t result);

static void
xfrout_log1(ns_client_t *client, dns_name_t *zonename, dns_rdataclass_t rdclass,
	    int level, const char *fmt, ...) ISC_FORMAT_PRINTF(5, 6);

static void
xfrout_log(xfrout_ctx_t *xfr, int level, const char *fmt, ...)
	ISC_FORMAT_PRINTF(3, 4);

static void
xfrout_delayed_timeout(void *arg, isc_result_t result);

/**************************************************************************/

void
ns_xfr_start(ns_client_t *client, dns_rdatatype_t reqtype) {
	isc_result_t result;
	dns_name_t *question_name;
	dns_rdataset_t *question_rdataset;
	dns_zone_t *zone = NULL, *raw = NULL, *mayberaw;
	dns_db_t *db = NULL;
	dns_dbversion_t *ver = NULL;
	dns_rdataclass_t question_class;
	rrstream_t *soa_stream = NULL;
	rrstream_t *data_stream = NULL;
	rrstream_t *stream = NULL;
	dns_difftuple_t *current_soa_tuple = NULL;
	dns_rdata_t soa_rdata = DNS_RDATA_INIT;
	bool have_soa = false;
	const char *mnemonic = NULL;
	isc_mem_t *mctx = client->manager->mctx;
	dns_message_t *request = client->message;
	xfrout_ctx_t *xfr = NULL;
	dns_transfer_format_t format = client->inner.view->transfer_format;
	isc_netaddr_t na;
	dns_peer_t *peer = NULL;
	isc_buffer_t *tsigbuf = NULL;
	char *journalfile;
	char msg[NS_CLIENT_ACLMSGSIZE("zone transfer")];
	char keyname[DNS_NAME_FORMATSIZE];
	bool is_poll = false;
	bool is_dlz = false;
	bool is_ixfr = false;
	bool useviewacl = false;
	uint32_t begin_serial = 0, current_serial;

	switch (reqtype) {
	case dns_rdatatype_axfr:
		mnemonic = "AXFR";
		break;
	case dns_rdatatype_ixfr:
		mnemonic = "IXFR";
		break;
	default:
		UNREACHABLE();
	}

	ns_client_log(client, DNS_LOGCATEGORY_XFER_OUT, NS_LOGMODULE_XFER_OUT,
		      ISC_LOG_DEBUG(6), "%s request", mnemonic);
	/*
	 * Apply quota.
	 */
	result = isc_quota_acquire(&client->manager->sctx->xfroutquota);
	if (result != ISC_R_SUCCESS) {
		isc_log_write(DNS_LOGCATEGORY_XFER_OUT, NS_LOGMODULE_XFER_OUT,
			      ISC_LOG_WARNING, "%s request denied: %s",
			      mnemonic, isc_result_totext(result));
		goto max_quota;
	}

	/*
	 * Interpret the question section.
	 */
	INSIST(!ISC_LIST_EMPTY(request->sections[DNS_SECTION_QUESTION]));

	/*
	 * The question section must contain exactly one question, and
	 * it must be for AXFR/IXFR as appropriate.
	 */
	question_name = ISC_LIST_HEAD(request->sections[DNS_SECTION_QUESTION]);
	question_rdataset = ISC_LIST_HEAD(question_name->list);
	question_class = question_rdataset->rdclass;
	INSIST(question_rdataset->type == reqtype);
	if (ISC_LIST_NEXT(question_rdataset, link) != NULL) {
		FAILC(DNS_R_FORMERR, "multiple questions");
	}

	if (ISC_LIST_NEXT(question_name, link) != NULL) {
		FAILC(DNS_R_FORMERR, "multiple questions");
	}

	result = dns_view_findzone(client->inner.view, question_name,
				   DNS_ZTFIND_EXACT, &zone);
	if (result != ISC_R_SUCCESS || dns_zone_gettype(zone) == dns_zone_dlz) {
		/*
		 * The normal zone table does not have a match, or this is
		 * marked in the zone table as a DLZ zone. Check the DLZ
		 * databases for a match.
		 */
		if (!ISC_LIST_EMPTY(client->inner.view->dlz_searched)) {
			result = dns_dlzallowzonexfr(
				client->inner.view, question_name,
				&client->inner.peeraddr, &db);
			if (result == ISC_R_DEFAULT) {
				useviewacl = true;
				result = ISC_R_SUCCESS;
			}
			if (result == ISC_R_NOPERM) {
				char _buf1[DNS_NAME_FORMATSIZE];
				char _buf2[DNS_RDATACLASS_FORMATSIZE];

				result = DNS_R_REFUSED;
				dns_name_format(question_name, _buf1,
						sizeof(_buf1));
				dns_rdataclass_format(question_class, _buf2,
						      sizeof(_buf2));
				ns_client_log(client, DNS_LOGCATEGORY_SECURITY,
					      NS_LOGMODULE_XFER_OUT,
					      ISC_LOG_ERROR,
					      "zone transfer '%s/%s' denied",
					      _buf1, _buf2);
				goto failure;
			}
			if (result != ISC_R_SUCCESS) {
				FAILQ(DNS_R_NOTAUTH, "non-authoritative zone",
				      question_name, question_class);
			}
			is_dlz = true;
		} else {
			/*
			 * not DLZ and not in normal zone table, we are
			 * not authoritative
			 */
			FAILQ(DNS_R_NOTAUTH, "non-authoritative zone",
			      question_name, question_class);
		}
	} else {
		/* zone table has a match */
		switch (dns_zone_gettype(zone)) {
		/*
		 * Primary, secondary, and mirror zones are OK for transfer.
		 */
		case dns_zone_primary:
		case dns_zone_secondary:
		case dns_zone_mirror:
		case dns_zone_dlz:
			break;
		default:
			FAILQ(DNS_R_NOTAUTH, "non-authoritative zone",
			      question_name, question_class);
		}
		CHECK(dns_zone_getdb(zone, &db));
		dns_db_currentversion(db, &ver);
	}

	xfrout_log1(client, question_name, question_class, ISC_LOG_DEBUG(6),
		    "%s question section OK", mnemonic);

	/*
	 * Check the authority section.  Look for a SOA record with
	 * the same name and class as the question.
	 */
	MSG_SECTION_FOREACH (request, DNS_SECTION_AUTHORITY, soa_name) {
		/*
		 * Ignore data whose owner name is not the zone apex.
		 */
		if (!dns_name_equal(soa_name, question_name)) {
			continue;
		}

		ISC_LIST_FOREACH (soa_name->list, soa_rdataset, link) {
			/*
			 * Ignore non-SOA data.
			 */
			if (soa_rdataset->type != dns_rdatatype_soa) {
				continue;
			}
			if (soa_rdataset->rdclass != question_class) {
				continue;
			}

			CHECK(dns_rdataset_first(soa_rdataset));
			dns_rdataset_current(soa_rdataset, &soa_rdata);
			result = dns_rdataset_next(soa_rdataset);
			if (result == ISC_R_SUCCESS) {
				FAILC(DNS_R_FORMERR, "IXFR authority section "
						     "has multiple SOAs");
			}
			have_soa = true;
			goto got_soa;
		}
	}
got_soa:
	if (result != ISC_R_NOMORE) {
		CHECK(result);
	}

	xfrout_log1(client, question_name, question_class, ISC_LOG_DEBUG(6),
		    "%s authority section OK", mnemonic);

	/*
	 * If not a DLZ zone or we are falling back to the view's transfer
	 * ACL, decide whether to allow this transfer.
	 */
	if (!is_dlz || useviewacl) {
		dns_acl_t *acl;

		ns_client_aclmsg("zone transfer", question_name, reqtype,
				 client->inner.view->rdclass, msg, sizeof(msg));
		if (useviewacl) {
			acl = client->inner.view->transferacl;
		} else {
			acl = dns_zone_getxfracl(zone);
		}
		CHECK(ns_client_checkacl(client, NULL, msg, acl, true,
					 ISC_LOG_ERROR));
	}

	/*
	 * AXFR over UDP is not possible.
	 */
	if (reqtype == dns_rdatatype_axfr &&
	    (client->inner.attributes & NS_CLIENTATTR_TCP) == 0)
	{
		FAILC(DNS_R_FORMERR, "attempted AXFR over UDP");
	}

	/*
	 * Look up the requesting server in the peer table.
	 */
	isc_netaddr_fromsockaddr(&na, &client->inner.peeraddr);
	(void)dns_peerlist_peerbyaddr(client->inner.view->peers, &na, &peer);

	/*
	 * Decide on the transfer format (one-answer or many-answers).
	 */
	if (peer != NULL) {
		(void)dns_peer_gettransferformat(peer, &format);
	}

	/*
	 * Get a dynamically allocated copy of the current SOA.
	 */
	if (is_dlz) {
		dns_db_currentversion(db, &ver);
	}

	CHECK(dns_db_createsoatuple(db, ver, mctx, DNS_DIFFOP_EXISTS,
				    &current_soa_tuple));

	current_serial = dns_soa_getserial(&current_soa_tuple->rdata);
	if (reqtype == dns_rdatatype_ixfr) {
		size_t jsize;
		uint64_t dbsize;

		if (!have_soa) {
			FAILC(DNS_R_FORMERR, "IXFR request missing SOA");
		}

		begin_serial = dns_soa_getserial(&soa_rdata);

		/*
		 * RFC1995 says "If an IXFR query with the same or
		 * newer version number than that of the server
		 * is received, it is replied to with a single SOA
		 * record of the server's current version, just as
		 * in AXFR".  The claim about AXFR is incorrect,
		 * but other than that, we do as the RFC says.
		 *
		 * Sending a single SOA record is also how we refuse
		 * IXFR over UDP (currently, we always do).
		 */
		if (DNS_SERIAL_GE(begin_serial, current_serial) ||
		    (client->inner.attributes & NS_CLIENTATTR_TCP) == 0)
		{
			CHECK(soa_rrstream_create(mctx, db, ver, &stream));
			is_poll = true;
			goto have_stream;
		}

		/*
		 * Outgoing IXFR may have been disabled for this peer
		 * or globally.
		 */
		if ((client->inner.attributes & NS_CLIENTATTR_TCP) != 0) {
			bool provide_ixfr;

			provide_ixfr = client->inner.view->provideixfr;
			if (peer != NULL) {
				(void)dns_peer_getprovideixfr(peer,
							      &provide_ixfr);
			}
			if (!provide_ixfr) {
				xfrout_log1(client, question_name,
					    question_class, ISC_LOG_DEBUG(4),
					    "IXFR delta response disabled due "
					    "to 'provide-ixfr no;' being set");
				mnemonic = "AXFR-style IXFR";
				goto axfr_fallback;
			}
		}

		journalfile = is_dlz ? NULL : dns_zone_getjournal(zone);
		if (journalfile != NULL) {
			result = ixfr_rrstream_create(
				mctx, journalfile, begin_serial, current_serial,
				&jsize, &data_stream);
		} else {
			result = ISC_R_NOTFOUND;
		}
		if (result == ISC_R_NOTFOUND || result == ISC_R_RANGE) {
			xfrout_log1(client, question_name, question_class,
				    ISC_LOG_INFO,
				    "IXFR version not in journal, "
				    "falling back to AXFR");
			mnemonic = "AXFR-style IXFR";
			goto axfr_fallback;
		}
		CHECK(result);

		result = dns_db_getsize(db, ver, NULL, &dbsize);
		if (result == ISC_R_SUCCESS) {
			uint32_t ratio = dns_zone_getixfrratio(zone);
			if (ratio != 0 && ((100 * jsize) / dbsize) > ratio) {
				data_stream->methods->destroy(&data_stream);
				data_stream = NULL;
				xfrout_log1(client, question_name,
					    question_class, ISC_LOG_INFO,
					    "IXFR delta size (%zu bytes) "
					    "exceeds the maximum ratio to "
					    "database size "
					    "(%" PRIu64 " bytes), "
					    "falling back to AXFR",
					    jsize, dbsize);
				mnemonic = "AXFR-style IXFR";
				goto axfr_fallback;
			} else {
				xfrout_log1(client, question_name,
					    question_class, ISC_LOG_DEBUG(4),
					    "IXFR delta size (%zu bytes); "
					    "database size "
					    "(%" PRIu64 " bytes)",
					    jsize, dbsize);
			}
		}
		is_ixfr = true;
	} else {
	axfr_fallback:
		CHECK(axfr_rrstream_create(mctx, db, ver, &data_stream));
	}

	/*
	 * Bracket the data stream with SOAs.
	 */
	CHECK(soa_rrstream_create(mctx, db, ver, &soa_stream));
	CHECK(compound_rrstream_create(mctx, &soa_stream, &data_stream,
				       &stream));
	soa_stream = NULL;
	data_stream = NULL;

have_stream:
	CHECK(dns_message_getquerytsig(request, mctx, &tsigbuf));
	/*
	 * Create the xfrout context object.  This transfers the ownership
	 * of "stream", "db", "ver", and "quota" to the xfrout context object.
	 */

	if (is_dlz) {
		xfrout_ctx_create(mctx, client, request->id, question_name,
				  reqtype, question_class, zone, db, ver,
				  stream, dns_message_gettsigkey(request),
				  tsigbuf, request->verified_sig, 3600, 3600,
				  (format == dns_many_answers) ? true : false,
				  &xfr);
	} else {
		xfrout_ctx_create(
			mctx, client, request->id, question_name, reqtype,
			question_class, zone, db, ver, stream,
			dns_message_gettsigkey(request), tsigbuf,
			request->verified_sig, dns_zone_getmaxxfrout(zone),
			dns_zone_getidleout(zone),
			(format == dns_many_answers) ? true : false, &xfr);
	}

	xfr->end_serial = current_serial;
	xfr->mnemonic = mnemonic;
	stream = NULL;

	CHECK(xfr->stream->methods->first(xfr->stream));

	if (xfr->tsigkey != NULL) {
		dns_name_format(xfr->tsigkey->name, keyname, sizeof(keyname));
	} else {
		keyname[0] = '\0';
	}
	xfr->poll = is_poll;
	if (is_poll) {
		xfr->mnemonic = "IXFR poll response";
		xfrout_log1(client, question_name, question_class,
			    ISC_LOG_DEBUG(1), "IXFR poll up to date%s%s",
			    (xfr->tsigkey != NULL) ? ": TSIG " : "", keyname);
	} else if (is_ixfr) {
		xfrout_log1(client, question_name, question_class, ISC_LOG_INFO,
			    "%s started%s%s (serial %u -> %u)", mnemonic,
			    (xfr->tsigkey != NULL) ? ": TSIG " : "", keyname,
			    begin_serial, current_serial);
	} else {
		xfrout_log1(client, question_name, question_class, ISC_LOG_INFO,
			    "%s started%s%s (serial %u)", mnemonic,
			    (xfr->tsigkey != NULL) ? ": TSIG " : "", keyname,
			    current_serial);
	}

	if (zone != NULL) {
		dns_zone_getraw(zone, &raw);
		mayberaw = (raw != NULL) ? raw : zone;
		if ((client->inner.attributes & NS_CLIENTATTR_WANTEXPIRE) !=
			    0 &&
		    (dns_zone_gettype(mayberaw) == dns_zone_secondary ||
		     dns_zone_gettype(mayberaw) == dns_zone_mirror))
		{
			isc_time_t expiretime;
			uint32_t secs;
			dns_zone_getexpiretime(zone, &expiretime);
			secs = isc_time_seconds(&expiretime);
			if (secs >= client->inner.now &&
			    result == ISC_R_SUCCESS)
			{
				client->inner.attributes |=
					NS_CLIENTATTR_HAVEEXPIRE;
				client->inner.expire = secs - client->inner.now;
			}
		}
		if (raw != NULL) {
			dns_zone_detach(&raw);
		}
	}

	/* Start the timers */
	if (xfr->maxtime > 0) {
		xfrout_log(xfr, ISC_LOG_DEBUG(1),
			   "starting maxtime timer %" PRIu64 " ms",
			   xfr->maxtime);
		isc_nm_timer_start(xfr->maxtime_timer, xfr->maxtime);
	}

	/*
	 * Hand the context over to sendstream().  Set xfr to NULL;
	 * sendstream() is responsible for either passing the
	 * context on to a later event handler or destroying it.
	 */
	sendstream(xfr);
	xfr = NULL;

	result = ISC_R_SUCCESS;

failure:
	if (result == DNS_R_REFUSED) {
		inc_stats(client, zone, ns_statscounter_xfrrej);
	}
	if (current_soa_tuple != NULL) {
		dns_difftuple_free(&current_soa_tuple);
	}
	if (stream != NULL) {
		stream->methods->destroy(&stream);
	}
	if (soa_stream != NULL) {
		soa_stream->methods->destroy(&soa_stream);
	}
	if (data_stream != NULL) {
		data_stream->methods->destroy(&data_stream);
	}
	if (ver != NULL) {
		dns_db_closeversion(db, &ver, false);
	}
	if (db != NULL) {
		dns_db_detach(&db);
	}
	if (zone != NULL) {
		dns_zone_detach(&zone);
	}

	if (xfr != NULL) {
		xfrout_fail(xfr, result, "setting up zone transfer");
	} else if (result != ISC_R_SUCCESS) {
		isc_quota_release(&client->manager->sctx->xfroutquota);
	max_quota:
		ns_client_log(client, DNS_LOGCATEGORY_XFER_OUT,
			      NS_LOGMODULE_XFER_OUT, ISC_LOG_DEBUG(3),
			      "zone transfer setup failed");
		ns_client_error(client, result);
		isc_nmhandle_detach(&client->inner.reqhandle);
	}
}

static void
xfrout_ctx_create(isc_mem_t *mctx, ns_client_t *client, unsigned int id,
		  dns_name_t *qname, dns_rdatatype_t qtype,
		  dns_rdataclass_t qclass, dns_zone_t *zone, dns_db_t *db,
		  dns_dbversion_t *ver, rrstream_t *stream,
		  dns_tsigkey_t *tsigkey, isc_buffer_t *lasttsig,
		  bool verified_tsig, unsigned int maxtime,
		  unsigned int idletime, bool many_answers,
		  xfrout_ctx_t **xfrp) {
	xfrout_ctx_t *xfr = NULL;
	unsigned int len = NS_CLIENT_TCP_BUFFER_SIZE;
	void *mem = NULL;

	REQUIRE(xfrp != NULL && *xfrp == NULL);

	xfr = isc_mem_get(mctx, sizeof(*xfr));
	*xfr = (xfrout_ctx_t){
		.client = client,
		.id = id,
		.qname = qname,
		.qtype = qtype,
		.qclass = qclass,
		.maxtime = maxtime * 1000,   /* in milliseconds */
		.idletime = idletime * 1000, /* In milliseconds */
		.tsigkey = tsigkey,
		.lasttsig = lasttsig,
		.verified_tsig = verified_tsig,
		.many_answers = many_answers,
	};

	isc_mem_attach(mctx, &xfr->mctx);

	if (zone != NULL) { /* zone will be NULL if it's DLZ */
		dns_zone_attach(zone, &xfr->zone);
	}
	dns_db_attach(db, &xfr->db);
	dns_db_attachversion(db, ver, &xfr->ver);

	xfr->stats.start = isc_time_now();

	isc_nm_timer_create(xfr->client->inner.handle, xfrout_client_timeout,
			    xfr, &xfr->maxtime_timer);

	isc_nm_timer_create(xfr->client->inner.handle, xfrout_delayed_timeout,
			    xfr, &xfr->delayed_send_timer);

	/*
	 * Allocate a temporary buffer for the uncompressed response
	 * message data.  The buffer size must be 65535 bytes
	 * (NS_CLIENT_TCP_BUFFER_SIZE): small enough that compressed
	 * data will fit in a single TCP message, and big enough to
	 * hold a maximum-sized RR.
	 *
	 * Note that although 65535-byte RRs are allowed in principle, they
	 * cannot be zone-transferred (at least not if uncompressible),
	 * because the message and RR headers would push the size of the
	 * TCP message over the 65535 byte limit.
	 */
	mem = isc_mem_get(mctx, len);
	isc_buffer_init(&xfr->buf, mem, len);

	/*
	 * Allocate another temporary buffer for the compressed
	 * response message.
	 */
	mem = isc_mem_get(mctx, len);
	isc_buffer_init(&xfr->txbuf, (char *)mem, len);
	xfr->txmem = mem;
	xfr->txmemlen = len;

	/*
	 * These MUST be after the last "goto failure;" / CHECK to
	 * prevent a double free by the caller.
	 */
	xfr->stream = stream;

	*xfrp = xfr;
}

static void
xfrout_send(xfrout_ctx_t *xfr) {
	const bool is_tcp =
		((xfr->client->inner.attributes & NS_CLIENTATTR_TCP) != 0);

	if (is_tcp) {
		isc_region_t used;

		isc_buffer_usedregion(&xfr->txbuf, &used);

		isc_nmhandle_attach(xfr->client->inner.handle,
				    &xfr->client->inner.sendhandle);
		if (xfr->idletime > 0) {
			isc_nmhandle_setwritetimeout(
				xfr->client->inner.sendhandle, xfr->idletime);
		}
		isc_nm_send(xfr->client->inner.sendhandle, &used,
			    xfrout_senddone, xfr);
		xfr->sends++;
		xfr->cbytes = used.length;
	} else {
		ns_client_send(xfr->client);
		xfr->stream->methods->pause(xfr->stream);
		isc_nmhandle_detach(&xfr->client->inner.reqhandle);
		xfrout_ctx_destroy(&xfr);
	}
}

static void
xfrout_delayed_timeout(void *arg, isc_result_t result) {
	xfrout_ctx_t *xfr = (xfrout_ctx_t *)arg;
	UNUSED(result);

	isc_nm_timer_stop(xfr->delayed_send_timer);
	xfrout_send(xfr);
}

static void
xfrout_enqueue_send(xfrout_ctx_t *xfr) {
	uint64_t timeout = 0;

	/*
	 * System test helper options to simulate network issues.
	 *
	 * Both "transferslowly" and "transferstuck" are not meant to be
	 * used together (and are not actually used this way).
	 */
	if (ns_server_getoption(xfr->client->manager->sctx,
				NS_SERVER_TRANSFERSLOWLY))
	{
		/* Sleep for a bit over a second. */
		timeout = 1000;
	} else if (ns_server_getoption(xfr->client->manager->sctx,
				       NS_SERVER_TRANSFERSTUCK))
	{
		/* Sleep for a bit over a minute. */
		timeout = 60 * 1000;
	}

	if (timeout == 0) {
		xfrout_send(xfr);
		return;
	}

	/* delay */
	isc_nm_timer_start(xfr->delayed_send_timer, timeout);
}

/*
 * Arrange to send as much as we can of "stream" without blocking.
 *
 * Requires:
 *	The stream iterator is initialized and points at an RR,
 *      or possibly at the end of the stream (that is, the
 *      _first method of the iterator has been called).
 */
static void
sendstream(xfrout_ctx_t *xfr) {
	dns_message_t *tcpmsg = NULL;
	dns_message_t *msg = NULL; /* Client message if UDP, tcpmsg if TCP */
	isc_result_t result;
	dns_rdataset_t *qrdataset;
	dns_name_t *msgname = NULL;
	dns_rdata_t *msgrdata = NULL;
	dns_rdatalist_t *msgrdl = NULL;
	dns_rdataset_t *msgrds = NULL;
	dns_compress_t cctx;
	bool cleanup_cctx = false;
	bool is_tcp;
	int n_rrs;

	isc_buffer_clear(&xfr->buf);
	isc_buffer_clear(&xfr->txbuf);

	is_tcp = ((xfr->client->inner.attributes & NS_CLIENTATTR_TCP) != 0);
	if (!is_tcp) {
		/*
		 * In the UDP case, we put the response data directly into
		 * the client message.
		 */
		msg = xfr->client->message;
		CHECK(dns_message_reply(msg, true));
	} else {
		/*
		 * TCP. Build a response dns_message_t, temporarily storing
		 * the raw, uncompressed owner names and RR data contiguously
		 * in xfr->buf.  We know that if the uncompressed data fits
		 * in xfr->buf, the compressed data will surely fit in a TCP
		 * message.
		 */

		dns_message_create(xfr->mctx, NULL, NULL,
				   DNS_MESSAGE_INTENTRENDER, &tcpmsg);
		msg = tcpmsg;

		msg->id = xfr->id;
		msg->rcode = dns_rcode_noerror;
		msg->flags = DNS_MESSAGEFLAG_QR | DNS_MESSAGEFLAG_AA;
		if ((xfr->client->inner.attributes & NS_CLIENTATTR_RA) != 0) {
			msg->flags |= DNS_MESSAGEFLAG_RA;
		}
		CHECK(dns_message_settsigkey(msg, xfr->tsigkey));
		dns_message_setquerytsig(msg, xfr->lasttsig);
		if (xfr->lasttsig != NULL) {
			isc_buffer_free(&xfr->lasttsig);
		}
		msg->verified_sig = xfr->verified_tsig;

		/*
		 * Add a EDNS option to the message?
		 */
		if ((xfr->client->inner.attributes & NS_CLIENTATTR_WANTOPT) !=
		    0)
		{
			dns_rdataset_t *opt = NULL;

			CHECK(ns_client_addopt(xfr->client, msg, &opt));
			CHECK(dns_message_setopt(msg, opt));
			/*
			 * Add to first message only.
			 */
			xfr->client->inner.attributes &=
				~NS_CLIENTATTR_WANTNSID;
			xfr->client->inner.attributes &=
				~NS_CLIENTATTR_HAVEEXPIRE;
		}

		/*
		 * Account for reserved space.
		 */
		if (xfr->tsigkey != NULL) {
			INSIST(msg->reserved != 0U);
		}
		isc_buffer_add(&xfr->buf, msg->reserved);

		/*
		 * Include a question section in the first message only.
		 * BIND 8.2.1 will not recognize an IXFR if it does not
		 * have a question section.
		 */
		if (!xfr->question_added) {
			dns_name_t *qname = NULL;
			isc_region_t r;

			/*
			 * Reserve space for the 12-byte message header
			 * and 4 bytes of question.
			 */
			isc_buffer_add(&xfr->buf, 12 + 4);

			qrdataset = NULL;
			dns_message_gettemprdataset(msg, &qrdataset);
			dns_rdataset_makequestion(qrdataset,
						  xfr->client->message->rdclass,
						  xfr->qtype);

			dns_message_gettempname(msg, &qname);
			isc_buffer_availableregion(&xfr->buf, &r);
			INSIST(r.length >= xfr->qname->length);
			r.length = xfr->qname->length;
			isc_buffer_putmem(&xfr->buf, xfr->qname->ndata,
					  xfr->qname->length);
			dns_name_fromregion(qname, &r);
			ISC_LIST_INIT(qname->list);
			ISC_LIST_APPEND(qname->list, qrdataset, link);

			dns_message_addname(msg, qname, DNS_SECTION_QUESTION);
			xfr->question_added = true;
		} else {
			/*
			 * Reserve space for the 12-byte message header
			 */
			isc_buffer_add(&xfr->buf, 12);
			msg->tcp_continuation = 1;
		}
	}

	/*
	 * Try to fit in as many RRs as possible, unless "one-answer"
	 * format has been requested.
	 */
	for (n_rrs = 0;; n_rrs++) {
		dns_name_t *name = NULL;
		uint32_t ttl;
		dns_rdata_t *rdata = NULL;

		unsigned int size;
		isc_region_t r;

		msgname = NULL;
		msgrdata = NULL;
		msgrdl = NULL;
		msgrds = NULL;

		xfr->stream->methods->current(xfr->stream, &name, &ttl, &rdata);
		size = name->length + 10 + rdata->length;
		isc_buffer_availableregion(&xfr->buf, &r);
		if (size >= r.length) {
			/*
			 * RR would not fit.  If there are other RRs in the
			 * buffer, send them now and leave this RR to the
			 * next message.  If this RR overflows the buffer
			 * all by itself, fail.
			 *
			 * In theory some RRs might fit in a TCP message
			 * when compressed even if they do not fit when
			 * uncompressed, but surely we don't want
			 * to send such monstrosities to an unsuspecting
			 * secondary.
			 */
			if (n_rrs == 0) {
				xfrout_log(xfr, ISC_LOG_WARNING,
					   "RR too large for zone transfer "
					   "(%d bytes)",
					   size);
				/* XXX DNS_R_RRTOOLARGE? */
				result = ISC_R_NOSPACE;
				goto failure;
			}
			break;
		}

		if (isc_log_wouldlog(XFROUT_RR_LOGLEVEL)) {
			log_rr(name, rdata, ttl); /* XXX */
		}

		dns_message_gettempname(msg, &msgname);
		isc_buffer_availableregion(&xfr->buf, &r);
		INSIST(r.length >= name->length);
		r.length = name->length;
		isc_buffer_putmem(&xfr->buf, name->ndata, name->length);
		dns_name_fromregion(msgname, &r);

		/* Reserve space for RR header. */
		isc_buffer_add(&xfr->buf, 10);

		dns_message_gettemprdata(msg, &msgrdata);
		isc_buffer_availableregion(&xfr->buf, &r);
		r.length = rdata->length;
		isc_buffer_putmem(&xfr->buf, rdata->data, rdata->length);
		dns_rdata_fromregion(msgrdata, rdata->rdclass, rdata->type, &r);

		dns_message_gettemprdatalist(msg, &msgrdl);
		msgrdl->type = rdata->type;
		msgrdl->rdclass = rdata->rdclass;
		msgrdl->ttl = ttl;
		if (dns_rdatatype_issig(rdata->type)) {
			msgrdl->covers = dns_rdata_covers(rdata);
		} else {
			msgrdl->covers = dns_rdatatype_none;
		}
		ISC_LIST_APPEND(msgrdl->rdata, msgrdata, link);

		dns_message_gettemprdataset(msg, &msgrds);
		dns_rdatalist_tordataset(msgrdl, msgrds);

		ISC_LIST_APPEND(msgname->list, msgrds, link);

		dns_message_addname(msg, msgname, DNS_SECTION_ANSWER);
		msgname = NULL;

		xfr->stats.nrecs++;

		result = xfr->stream->methods->next(xfr->stream);
		if (result == ISC_R_NOMORE) {
			xfr->end_of_stream = true;
			break;
		}
		CHECK(result);

		if (!xfr->many_answers) {
			break;
		}
		/*
		 * At this stage, at least 1 RR has been rendered into
		 * the message. Check if we want to clamp this message
		 * here (TCP only).
		 */
		if ((isc_buffer_usedlength(&xfr->buf) >=
		     xfr->client->manager->sctx->transfer_tcp_message_size) &&
		    is_tcp)
		{
			break;
		}
	}

	if (is_tcp) {
		dns_compress_init(&cctx, xfr->mctx,
				  DNS_COMPRESS_CASE | DNS_COMPRESS_LARGE);
		cleanup_cctx = true;
		CHECK(dns_message_renderbegin(msg, &cctx, &xfr->txbuf));
		CHECK(dns_message_rendersection(msg, DNS_SECTION_QUESTION, 0));
		CHECK(dns_message_rendersection(msg, DNS_SECTION_ANSWER, 0));
		CHECK(dns_message_renderend(msg));
		dns_compress_invalidate(&cctx);
		cleanup_cctx = false;

		xfrout_log(xfr, ISC_LOG_DEBUG(8),
			   "sending TCP message of %d bytes",
			   isc_buffer_usedlength(&xfr->txbuf));

		xfrout_enqueue_send(xfr);
	} else {
		xfrout_log(xfr, ISC_LOG_DEBUG(8), "sending IXFR UDP response");

		xfrout_enqueue_send(xfr);
		return;
	}

	/* Advance lasttsig to be the last TSIG generated */
	CHECK(dns_message_getquerytsig(msg, xfr->mctx, &xfr->lasttsig));

failure:
	if (tcpmsg != NULL) {
		dns_message_detach(&tcpmsg);
	}

	if (cleanup_cctx) {
		dns_compress_invalidate(&cctx);
	}
	/*
	 * Make sure to release any locks held by database
	 * iterators before returning from the event handler.
	 */
	xfr->stream->methods->pause(xfr->stream);

	if (result == ISC_R_SUCCESS) {
		return;
	}

	xfrout_fail(xfr, result, "sending zone data");
}

static void
xfrout_ctx_destroy(xfrout_ctx_t **xfrp) {
	xfrout_ctx_t *xfr = *xfrp;
	*xfrp = NULL;

	INSIST(xfr->sends == 0);

	isc_nm_timer_stop(xfr->delayed_send_timer);
	isc_nm_timer_detach(&xfr->delayed_send_timer);

	isc_nm_timer_stop(xfr->maxtime_timer);
	isc_nm_timer_detach(&xfr->maxtime_timer);

	if (xfr->stream != NULL) {
		xfr->stream->methods->destroy(&xfr->stream);
	}
	if (xfr->buf.base != NULL) {
		isc_mem_put(xfr->mctx, xfr->buf.base, xfr->buf.length);
	}
	if (xfr->txmem != NULL) {
		isc_mem_put(xfr->mctx, xfr->txmem, xfr->txmemlen);
	}
	if (xfr->lasttsig != NULL) {
		isc_buffer_free(&xfr->lasttsig);
	}

	isc_quota_release(&xfr->client->manager->sctx->xfroutquota);

	if (xfr->ver != NULL) {
		dns_db_closeversion(xfr->db, &xfr->ver, false);
	}
	if (xfr->zone != NULL) {
		dns_zone_detach(&xfr->zone);
	}
	if (xfr->db != NULL) {
		dns_db_detach(&xfr->db);
	}

	isc_mem_putanddetach(&xfr->mctx, xfr, sizeof(*xfr));
}

static void
xfrout_senddone(isc_nmhandle_t *handle, isc_result_t result, void *arg) {
	xfrout_ctx_t *xfr = (xfrout_ctx_t *)arg;

	REQUIRE((xfr->client->inner.attributes & NS_CLIENTATTR_TCP) != 0);

	INSIST(handle == xfr->client->inner.handle);

	xfr->sends--;
	INSIST(xfr->sends == 0);

	isc_nmhandle_detach(&xfr->client->inner.sendhandle);

	/*
	 * Update transfer statistics if sending succeeded, accounting for the
	 * two-byte TCP length prefix included in the number of bytes sent.
	 */
	if (result == ISC_R_SUCCESS) {
		xfr->stats.nmsg++;
		xfr->stats.nbytes += xfr->cbytes;
	}

	if (xfr->shuttingdown) {
		xfrout_maybe_destroy(xfr);
	} else if (result != ISC_R_SUCCESS) {
		xfrout_fail(xfr, result, "send");
	} else if (!xfr->end_of_stream) {
		sendstream(xfr);
	} else {
		/* End of zone transfer stream. */
		uint64_t msecs, persec;

		inc_stats(xfr->client, xfr->zone, ns_statscounter_xfrdone);
		xfr->stats.end = isc_time_now();
		msecs = isc_time_microdiff(&xfr->stats.end, &xfr->stats.start);
		msecs /= 1000;
		if (msecs == 0) {
			msecs = 1;
		}
		persec = (xfr->stats.nbytes * 1000) / msecs;
		xfrout_log(xfr, xfr->poll ? ISC_LOG_DEBUG(1) : ISC_LOG_INFO,
			   "%s ended: "
			   "%" PRIu64 " messages, %" PRIu64 " records, "
			   "%" PRIu64 " bytes, "
			   "%u.%03u secs (%u bytes/sec) (serial %u)",
			   xfr->mnemonic, xfr->stats.nmsg, xfr->stats.nrecs,
			   xfr->stats.nbytes, (unsigned int)(msecs / 1000),
			   (unsigned int)(msecs % 1000), (unsigned int)persec,
			   xfr->end_serial);

		/*
		 * We're done, unreference the handle and destroy the xfr
		 * context.
		 */
		isc_nmhandle_detach(&xfr->client->inner.reqhandle);
		xfrout_ctx_destroy(&xfr);
	}
}

static void
xfrout_fail(xfrout_ctx_t *xfr, isc_result_t result, const char *msg) {
	xfr->shuttingdown = true;
	xfrout_log(xfr, ISC_LOG_ERROR, "%s: %s", msg,
		   isc_result_totext(result));
	xfrout_maybe_destroy(xfr);
}

static void
xfrout_maybe_destroy(xfrout_ctx_t *xfr) {
	REQUIRE(xfr->shuttingdown);

	ns_client_drop(xfr->client, ISC_R_CANCELED);
	isc_nmhandle_detach(&xfr->client->inner.reqhandle);
	xfrout_ctx_destroy(&xfr);
}

static void
xfrout_client_timeout(void *arg, isc_result_t result) {
	xfrout_ctx_t *xfr = (xfrout_ctx_t *)arg;

	xfr->shuttingdown = true;
	xfrout_log(xfr, ISC_LOG_ERROR, "%s: %s", "aborted",
		   isc_result_totext(result));
}

/*
 * Log outgoing zone transfer messages in a format like
 * <client>: transfer of <zone>: <message>
 */

static void
xfrout_logv(ns_client_t *client, dns_name_t *zonename, dns_rdataclass_t rdclass,
	    int level, const char *fmt, va_list ap) ISC_FORMAT_PRINTF(5, 0);

static void
xfrout_logv(ns_client_t *client, dns_name_t *zonename, dns_rdataclass_t rdclass,
	    int level, const char *fmt, va_list ap) {
	char msgbuf[2048];
	char namebuf[DNS_NAME_FORMATSIZE];
	char classbuf[DNS_RDATACLASS_FORMATSIZE];

	dns_name_format(zonename, namebuf, sizeof(namebuf));
	dns_rdataclass_format(rdclass, classbuf, sizeof(classbuf));
	vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
	ns_client_log(client, DNS_LOGCATEGORY_XFER_OUT, NS_LOGMODULE_XFER_OUT,
		      level, "transfer of '%s/%s': %s", namebuf, classbuf,
		      msgbuf);
}

/*
 * Logging function for use when a xfrout_ctx_t has not yet been created.
 */
static void
xfrout_log1(ns_client_t *client, dns_name_t *zonename, dns_rdataclass_t rdclass,
	    int level, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	xfrout_logv(client, zonename, rdclass, level, fmt, ap);
	va_end(ap);
}

/*
 * Logging function for use when there is a xfrout_ctx_t.
 */
static void
xfrout_log(xfrout_ctx_t *xfr, int level, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	xfrout_logv(xfr->client, xfr->qname, xfr->qclass, level, fmt, ap);
	va_end(ap);
}
