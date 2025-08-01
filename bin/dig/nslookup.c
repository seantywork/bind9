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
#include <stdlib.h>
#include <unistd.h>

#include <isc/async.h>
#include <isc/attributes.h>
#include <isc/buffer.h>
#include <isc/commandline.h>
#include <isc/lib.h>
#include <isc/loop.h>
#include <isc/netaddr.h>
#include <isc/parseint.h>
#include <isc/readline.h>
#include <isc/string.h>
#include <isc/util.h>
#include <isc/work.h>

#include <dns/byaddr.h>
#include <dns/fixedname.h>
#include <dns/lib.h>
#include <dns/message.h>
#include <dns/name.h>
#include <dns/rdata.h>
#include <dns/rdataclass.h>
#include <dns/rdataset.h>
#include <dns/rdatastruct.h>
#include <dns/rdatatype.h>

#include "dighost.h"

static char cmdlinebuf[COMMSIZE];
static char *cmdline = NULL;

static bool short_form = true, tcpmode = false, tcpmode_set = false,
	    identify = false, stats = true, comments = true,
	    section_question = true, section_answer = true,
	    section_authority = true, section_additional = true, recurse = true,
	    aaonly = false, nofail = true, default_lookups = true,
	    a_noanswer = false;

static bool interactive;

static bool in_use = false;
static char defclass[MXRD] = "IN";
static char deftype[MXRD] = "A";
static int query_error = 1, print_error = 0;

static char domainopt[DNS_NAME_MAXTEXT];

static const char *rcodetext[] = { "NOERROR",	 "FORMERR",    "SERVFAIL",
				   "NXDOMAIN",	 "NOTIMP",     "REFUSED",
				   "YXDOMAIN",	 "YXRRSET",    "NXRRSET",
				   "NOTAUTH",	 "NOTZONE",    "RESERVED11",
				   "RESERVED12", "RESERVED13", "RESERVED14",
				   "RESERVED15", "BADVERS" };

static const char *rtypetext[] = {
	"rtype_0 = ",	       /* 0 */
	"internet address = ", /* 1 */
	"nameserver = ",       /* 2 */
	"md = ",	       /* 3 */
	"mf = ",	       /* 4 */
	"canonical name = ",   /* 5 */
	"soa = ",	       /* 6 */
	"mb = ",	       /* 7 */
	"mg = ",	       /* 8 */
	"mr = ",	       /* 9 */
	"rtype_10 = ",	       /* 10 */
	"protocol = ",	       /* 11 */
	"name = ",	       /* 12 */
	"hinfo = ",	       /* 13 */
	"minfo = ",	       /* 14 */
	"mail exchanger = ",   /* 15 */
	"text = ",	       /* 16 */
	"rp = ",	       /* 17 */
	"afsdb = ",	       /* 18 */
	"x25 address = ",      /* 19 */
	"isdn address = ",     /* 20 */
	"rt = ",	       /* 21 */
	"nsap = ",	       /* 22 */
	"nsap_ptr = ",	       /* 23 */
	"signature = ",	       /* 24 */
	"key = ",	       /* 25 */
	"px = ",	       /* 26 */
	"gpos = ",	       /* 27 */
	"has AAAA address ",   /* 28 */
	"loc = ",	       /* 29 */
	"next = ",	       /* 30 */
	"rtype_31 = ",	       /* 31 */
	"rtype_32 = ",	       /* 32 */
	"service = ",	       /* 33 */
	"rtype_34 = ",	       /* 34 */
	"naptr = ",	       /* 35 */
	"kx = ",	       /* 36 */
	"cert = ",	       /* 37 */
	"v6 address = ",       /* 38 */
	"dname = ",	       /* 39 */
	"rtype_40 = ",	       /* 40 */
	"optional = "	       /* 41 */
};

#define N_KNOWN_RRTYPES (sizeof(rtypetext) / sizeof(rtypetext[0]))

static char *
rcode_totext(dns_rcode_t rcode) {
	static char buf[sizeof("?65535")];
	union {
		const char *consttext;
		char *deconsttext;
	} totext;

	if (rcode >= (sizeof(rcodetext) / sizeof(rcodetext[0]))) {
		snprintf(buf, sizeof(buf), "?%u", rcode);
		totext.deconsttext = buf;
	} else {
		totext.consttext = rcodetext[rcode];
	}
	return totext.deconsttext;
}

static void
printsoa(dns_rdata_t *rdata) {
	dns_rdata_soa_t soa;
	isc_result_t result;
	char namebuf[DNS_NAME_FORMATSIZE];

	result = dns_rdata_tostruct(rdata, &soa, NULL);
	check_result(result, "dns_rdata_tostruct");

	dns_name_format(&soa.origin, namebuf, sizeof(namebuf));
	printf("\torigin = %s\n", namebuf);
	dns_name_format(&soa.contact, namebuf, sizeof(namebuf));
	printf("\tmail addr = %s\n", namebuf);
	printf("\tserial = %u\n", soa.serial);
	printf("\trefresh = %u\n", soa.refresh);
	printf("\tretry = %u\n", soa.retry);
	printf("\texpire = %u\n", soa.expire);
	printf("\tminimum = %u\n", soa.minimum);
	dns_rdata_freestruct(&soa);
}

static void
printaddr(dns_rdata_t *rdata) {
	isc_result_t result;
	char text[sizeof("ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255")];
	isc_buffer_t b;

	isc_buffer_init(&b, text, sizeof(text));
	result = dns_rdata_totext(rdata, NULL, &b);
	check_result(result, "dns_rdata_totext");
	printf("Address: %.*s\n", (int)isc_buffer_usedlength(&b),
	       (char *)isc_buffer_base(&b));
}

static void
printrdata(dns_rdata_t *rdata) {
	isc_result_t result;
	isc_buffer_t *b = NULL;
	unsigned int size = 1024;
	bool done = false;

	if (rdata->type < N_KNOWN_RRTYPES) {
		printf("%s", rtypetext[rdata->type]);
	} else {
		printf("rdata_%d = ", rdata->type);
	}

	while (!done) {
		isc_buffer_allocate(mctx, &b, size);
		result = dns_rdata_totext(rdata, NULL, b);
		if (result == ISC_R_SUCCESS) {
			printf("%.*s\n", (int)isc_buffer_usedlength(b),
			       (char *)isc_buffer_base(b));
			done = true;
		} else if (result != ISC_R_NOSPACE) {
			check_result(result, "dns_rdata_totext");
		}
		isc_buffer_free(&b);
		INSIST(size <= (UINT_MAX / 2));
		size *= 2;
	}
}

static isc_result_t
printsection(dig_query_t *query, dns_message_t *msg, bool headers,
	     dns_section_t section) {
	char namebuf[DNS_NAME_FORMATSIZE];

	UNUSED(query);
	UNUSED(headers);

	debug("printsection()");

	MSG_SECTION_FOREACH (msg, section, name) {
		ISC_LIST_FOREACH (name->list, rdataset, link) {
			DNS_RDATASET_FOREACH (rdataset) {
				dns_rdata_t rdata = DNS_RDATA_INIT;
				dns_rdataset_current(rdataset, &rdata);
				switch (rdata.type) {
				case dns_rdatatype_a:
				case dns_rdatatype_aaaa:
					if (section != DNS_SECTION_ANSWER) {
						goto def_short_section;
					}
					dns_name_format(name, namebuf,
							sizeof(namebuf));
					printf("Name:\t%s\n", namebuf);
					printaddr(&rdata);
					break;
				case dns_rdatatype_soa:
					dns_name_format(name, namebuf,
							sizeof(namebuf));
					printf("%s\n", namebuf);
					printsoa(&rdata);
					break;
				default:
				def_short_section:
					dns_name_format(name, namebuf,
							sizeof(namebuf));
					printf("%s\t", namebuf);
					printrdata(&rdata);
					break;
				}
			}
		}
	}
	return ISC_R_SUCCESS;
}

static isc_result_t
detailsection(dig_query_t *query, dns_message_t *msg, bool headers,
	      dns_section_t section) {
	char namebuf[DNS_NAME_FORMATSIZE];

	UNUSED(query);

	debug("detailsection()");

	if (headers) {
		switch (section) {
		case DNS_SECTION_QUESTION:
			puts("    QUESTIONS:");
			break;
		case DNS_SECTION_ANSWER:
			puts("    ANSWERS:");
			break;
		case DNS_SECTION_AUTHORITY:
			puts("    AUTHORITY RECORDS:");
			break;
		case DNS_SECTION_ADDITIONAL:
			puts("    ADDITIONAL RECORDS:");
			break;
		}
	}

	MSG_SECTION_FOREACH (msg, section, name) {
		ISC_LIST_FOREACH (name->list, rdataset, link) {
			if (section == DNS_SECTION_QUESTION) {
				dns_name_format(name, namebuf, sizeof(namebuf));
				printf("\t%s, ", namebuf);
				dns_rdatatype_format(rdataset->type, namebuf,
						     sizeof(namebuf));
				printf("type = %s, ", namebuf);
				dns_rdataclass_format(rdataset->rdclass,
						      namebuf, sizeof(namebuf));
				printf("class = %s\n", namebuf);
			}
			DNS_RDATASET_FOREACH (rdataset) {
				dns_rdata_t rdata = DNS_RDATA_INIT;
				dns_rdataset_current(rdataset, &rdata);

				dns_name_format(name, namebuf, sizeof(namebuf));
				printf("    ->  %s\n", namebuf);

				switch (rdata.type) {
				case dns_rdatatype_soa:
					printsoa(&rdata);
					break;
				default:
					printf("\t");
					printrdata(&rdata);
				}
				printf("\tttl = %u\n", rdataset->ttl);
			}
		}
	}
	return ISC_R_SUCCESS;
}

static void
received(unsigned int bytes, isc_sockaddr_t *from, dig_query_t *query) {
	UNUSED(bytes);
	UNUSED(from);
	UNUSED(query);
}

static void
trying(char *frm, dig_lookup_t *lookup) {
	UNUSED(frm);
	UNUSED(lookup);
}

static void
chase_cnamechain(dns_message_t *msg, dns_name_t *qname) {
	isc_result_t result;
	dns_rdataset_t *rdataset;
	dns_rdata_cname_t cname;
	dns_rdata_t rdata = DNS_RDATA_INIT;
	unsigned int i = msg->counts[DNS_SECTION_ANSWER];

	while (i-- > 0) {
		rdataset = NULL;
		result = dns_message_findname(msg, DNS_SECTION_ANSWER, qname,
					      dns_rdatatype_cname, 0, NULL,
					      &rdataset);
		if (result != ISC_R_SUCCESS) {
			return;
		}
		result = dns_rdataset_first(rdataset);
		check_result(result, "dns_rdataset_first");
		dns_rdata_reset(&rdata);
		dns_rdataset_current(rdataset, &rdata);
		result = dns_rdata_tostruct(&rdata, &cname, NULL);
		check_result(result, "dns_rdata_tostruct");
		dns_name_copy(&cname.cname, qname);
		dns_rdata_freestruct(&cname);
	}
}

static isc_result_t
printmessage(dig_query_t *query, const isc_buffer_t *msgbuf, dns_message_t *msg,
	     bool headers) {
	UNUSED(msgbuf);

	/* I've we've gotten this far, we've reached a server. */
	query_error = 0;

	debug("printmessage()");

	if (!default_lookups || query->lookup->rdtype == dns_rdatatype_a) {
		char servtext[ISC_SOCKADDR_FORMATSIZE];
		isc_sockaddr_format(&query->sockaddr, servtext,
				    sizeof(servtext));
		printf("Server:\t\t%s\n", query->userarg);
		printf("Address:\t%s\n", servtext);

		puts("");
	}

	if (!short_form) {
		puts("------------");
		/*		detailheader(query, msg);*/
		detailsection(query, msg, true, DNS_SECTION_QUESTION);
		detailsection(query, msg, true, DNS_SECTION_ANSWER);
		detailsection(query, msg, true, DNS_SECTION_AUTHORITY);
		detailsection(query, msg, true, DNS_SECTION_ADDITIONAL);
		puts("------------");
	}

	if (msg->rcode != 0) {
		char nametext[DNS_NAME_FORMATSIZE];
		dns_name_format(query->lookup->name, nametext,
				sizeof(nametext));
		printf("** server can't find %s: %s\n", nametext,
		       rcode_totext(msg->rcode));
		debug("returning with rcode == 0");

		/* the lookup failed */
		print_error |= 1;
		return ISC_R_SUCCESS;
	}

	if (default_lookups && query->lookup->rdtype == dns_rdatatype_a) {
		char namestr[DNS_NAME_FORMATSIZE];
		dig_lookup_t *lookup;
		dns_fixedname_t fixed;
		dns_name_t *name;

		/* Add AAAA lookup. */
		name = dns_fixedname_initname(&fixed);
		dns_name_copy(query->lookup->name, name);
		chase_cnamechain(msg, name);
		dns_name_format(name, namestr, sizeof(namestr));
		lookup = clone_lookup(query->lookup, false);
		if (lookup != NULL) {
			strlcpy(lookup->textname, namestr,
				sizeof(lookup->textname));
			lookup->rdtype = dns_rdatatype_aaaa;
			lookup->rdtypeset = true;
			lookup->origin = NULL;
			lookup->retries = tries;
			ISC_LIST_APPEND(lookup_list, lookup, link);
		}
	}

	if ((msg->flags & DNS_MESSAGEFLAG_AA) == 0 &&
	    (!default_lookups || query->lookup->rdtype == dns_rdatatype_a))
	{
		puts("Non-authoritative answer:");
	}
	if (!ISC_LIST_EMPTY(msg->sections[DNS_SECTION_ANSWER])) {
		printsection(query, msg, headers, DNS_SECTION_ANSWER);
	} else {
		if (default_lookups && query->lookup->rdtype == dns_rdatatype_a)
		{
			a_noanswer = true;
		} else if (!default_lookups ||
			   (query->lookup->rdtype == dns_rdatatype_aaaa &&
			    a_noanswer))
		{
			printf("*** Can't find %s: No answer\n",
			       query->lookup->textname);
		}
	}

	if (((msg->flags & DNS_MESSAGEFLAG_AA) == 0) &&
	    (query->lookup->rdtype != dns_rdatatype_a) &&
	    (query->lookup->rdtype != dns_rdatatype_aaaa))
	{
		puts("\nAuthoritative answers can be found from:");
		printsection(query, msg, headers, DNS_SECTION_AUTHORITY);
		printsection(query, msg, headers, DNS_SECTION_ADDITIONAL);
	}
	return ISC_R_SUCCESS;
}

static void
show_settings(bool full, bool serv_only) {
	isc_sockaddr_t sockaddr;
	isc_result_t result;

	ISC_LIST_FOREACH (server_list, srv, link) {
		char sockstr[ISC_SOCKADDR_FORMATSIZE];

		result = get_address(srv->servername, port, &sockaddr);
		check_result(result, "get_address");

		isc_sockaddr_format(&sockaddr, sockstr, sizeof(sockstr));
		printf("Default server: %s\nAddress: %s\n", srv->userarg,
		       sockstr);
		if (!full) {
			return;
		}
	}
	if (serv_only) {
		return;
	}
	printf("\nSet options:\n");
	printf("  %s\t\t\t%s\t\t%s\n", tcpmode ? "vc" : "novc",
	       short_form ? "nodebug" : "debug", debugging ? "d2" : "nod2");
	printf("  %s\t\t%s\n", usesearch ? "search" : "nosearch",
	       recurse ? "recurse" : "norecurse");
	printf("  timeout = %u\t\tretry = %d\tport = %u\tndots = %d\n", timeout,
	       tries, port, ndots);
	printf("  querytype = %-8s\tclass = %s\n", deftype, defclass);
	printf("  srchlist = ");
	ISC_LIST_FOREACH (search_list, listent, link) {
		printf("%s", listent->origin);
		if (ISC_LIST_NEXT(listent, link) != NULL) {
			printf("/");
		}
	}
	printf("\n");
}

static bool
testtype(char *typetext) {
	isc_result_t result;
	isc_textregion_t tr;
	dns_rdatatype_t rdtype;

	tr.base = typetext;
	tr.length = strlen(typetext);
	result = dns_rdatatype_fromtext(&rdtype, &tr);
	if (result == ISC_R_SUCCESS) {
		return true;
	} else {
		printf("unknown query type: %s\n", typetext);
		return false;
	}
}

static bool
testclass(char *typetext) {
	isc_result_t result;
	isc_textregion_t tr;
	dns_rdataclass_t rdclass;

	tr.base = typetext;
	tr.length = strlen(typetext);
	result = dns_rdataclass_fromtext(&rdclass, &tr);
	if (result == ISC_R_SUCCESS) {
		return true;
	} else {
		printf("unknown query class: %s\n", typetext);
		return false;
	}
}

static void
set_port(const char *value) {
	uint32_t n;
	isc_result_t result = parse_uint(&n, value, 65535, "port");
	if (result == ISC_R_SUCCESS) {
		port = (uint16_t)n;
		port_set = true;
	}
}

static void
set_timeout(const char *value) {
	uint32_t n;
	isc_result_t result = parse_uint(&n, value, UINT_MAX, "timeout");
	if (result == ISC_R_SUCCESS) {
		timeout = n;
	}
}

static void
set_tries(const char *value) {
	uint32_t n;
	isc_result_t result = parse_uint(&n, value, INT_MAX, "tries");
	if (result == ISC_R_SUCCESS) {
		tries = n;
	}
}

static void
set_ndots(const char *value) {
	uint32_t n;
	isc_result_t result = parse_uint(&n, value, 128, "ndots");
	if (result == ISC_R_SUCCESS) {
		ndots = n;
	}
}

static void
setoption(char *opt) {
	size_t l = strlen(opt);

#define CHECKOPT(A, N) \
	((l >= N) && (l < sizeof(A)) && (strncasecmp(opt, A, l) == 0))

	if (CHECKOPT("all", 3)) {
		show_settings(true, false);
	} else if (strncasecmp(opt, "class=", 6) == 0) {
		if (testclass(&opt[6])) {
			strlcpy(defclass, &opt[6], sizeof(defclass));
		}
	} else if (strncasecmp(opt, "cl=", 3) == 0) {
		if (testclass(&opt[3])) {
			strlcpy(defclass, &opt[3], sizeof(defclass));
		}
	} else if (strncasecmp(opt, "type=", 5) == 0) {
		if (testtype(&opt[5])) {
			strlcpy(deftype, &opt[5], sizeof(deftype));
			default_lookups = false;
		}
	} else if (strncasecmp(opt, "ty=", 3) == 0) {
		if (testtype(&opt[3])) {
			strlcpy(deftype, &opt[3], sizeof(deftype));
			default_lookups = false;
		}
	} else if (strncasecmp(opt, "querytype=", 10) == 0) {
		if (testtype(&opt[10])) {
			strlcpy(deftype, &opt[10], sizeof(deftype));
			default_lookups = false;
		}
	} else if (strncasecmp(opt, "query=", 6) == 0) {
		if (testtype(&opt[6])) {
			strlcpy(deftype, &opt[6], sizeof(deftype));
			default_lookups = false;
		}
	} else if (strncasecmp(opt, "qu=", 3) == 0) {
		if (testtype(&opt[3])) {
			strlcpy(deftype, &opt[3], sizeof(deftype));
			default_lookups = false;
		}
	} else if (strncasecmp(opt, "q=", 2) == 0) {
		if (testtype(&opt[2])) {
			strlcpy(deftype, &opt[2], sizeof(deftype));
			default_lookups = false;
		}
	} else if (strncasecmp(opt, "domain=", 7) == 0) {
		strlcpy(domainopt, &opt[7], sizeof(domainopt));
		set_search_domain(domainopt);
		usesearch = true;
	} else if (strncasecmp(opt, "do=", 3) == 0) {
		strlcpy(domainopt, &opt[3], sizeof(domainopt));
		set_search_domain(domainopt);
		usesearch = true;
	} else if (strncasecmp(opt, "port=", 5) == 0) {
		set_port(&opt[5]);
	} else if (strncasecmp(opt, "po=", 3) == 0) {
		set_port(&opt[3]);
	} else if (strncasecmp(opt, "timeout=", 8) == 0) {
		set_timeout(&opt[8]);
	} else if (strncasecmp(opt, "t=", 2) == 0) {
		set_timeout(&opt[2]);
	} else if (CHECKOPT("recurse", 3)) {
		recurse = true;
	} else if (CHECKOPT("norecurse", 5)) {
		recurse = false;
	} else if (strncasecmp(opt, "retry=", 6) == 0) {
		set_tries(&opt[6]);
	} else if (strncasecmp(opt, "ret=", 4) == 0) {
		set_tries(&opt[4]);
	} else if (CHECKOPT("defname", 3)) {
		usesearch = true;
	} else if (CHECKOPT("nodefname", 5)) {
		usesearch = false;
	} else if (CHECKOPT("vc", 2)) {
		tcpmode = true;
		tcpmode_set = true;
	} else if (CHECKOPT("novc", 4)) {
		tcpmode = false;
		tcpmode_set = true;
	} else if (CHECKOPT("debug", 3)) {
		short_form = false;
		showsearch = true;
	} else if (CHECKOPT("nodebug", 5)) {
		short_form = true;
		showsearch = false;
	} else if (CHECKOPT("d2", 2)) {
		debugging = true;
	} else if (CHECKOPT("nod2", 4)) {
		debugging = false;
	} else if (CHECKOPT("search", 3)) {
		usesearch = true;
	} else if (CHECKOPT("nosearch", 5)) {
		usesearch = false;
	} else if (CHECKOPT("sil", 3)) {
		/* deprecation_msg = false; */
	} else if (CHECKOPT("fail", 3)) {
		nofail = false;
	} else if (CHECKOPT("nofail", 5)) {
		nofail = true;
	} else if (strncasecmp(opt, "ndots=", 6) == 0) {
		set_ndots(&opt[6]);
	} else {
		printf("*** Invalid option: %s\n", opt);
	}
}

static void
addlookup(char *opt) {
	dig_lookup_t *lookup;
	isc_result_t result;
	isc_textregion_t tr;
	dns_rdatatype_t rdtype;
	dns_rdataclass_t rdclass;
	char store[MXNAME];

	debug("addlookup()");

	a_noanswer = false;

	tr.base = deftype;
	tr.length = strlen(deftype);
	result = dns_rdatatype_fromtext(&rdtype, &tr);
	if (result != ISC_R_SUCCESS) {
		printf("unknown query type: %s\n", deftype);
		rdclass = dns_rdatatype_a;
	}
	tr.base = defclass;
	tr.length = strlen(defclass);
	result = dns_rdataclass_fromtext(&rdclass, &tr);
	if (result != ISC_R_SUCCESS) {
		printf("unknown query class: %s\n", defclass);
		rdclass = dns_rdataclass_in;
	}
	lookup = make_empty_lookup();
	if (get_reverse(store, sizeof(store), opt, true) == ISC_R_SUCCESS) {
		strlcpy(lookup->textname, store, sizeof(lookup->textname));
		lookup->rdtype = dns_rdatatype_ptr;
		lookup->rdtypeset = true;
	} else {
		strlcpy(lookup->textname, opt, sizeof(lookup->textname));
		lookup->rdtype = rdtype;
		lookup->rdtypeset = true;
	}
	lookup->rdclass = rdclass;
	lookup->rdclassset = true;
	lookup->trace = false;
	lookup->trace_root = lookup->trace;
	lookup->ns_search_only = false;
	lookup->identify = identify;
	lookup->recurse = recurse;
	lookup->aaonly = aaonly;
	lookup->retries = tries;
	lookup->setqid = false;
	lookup->qid = 0;
	lookup->comments = comments;
	if (lookup->rdtype == dns_rdatatype_any && !tcpmode_set) {
		lookup->tcp_mode = true;
	} else {
		lookup->tcp_mode = tcpmode;
	}
	lookup->stats = stats;
	lookup->section_question = section_question;
	lookup->section_answer = section_answer;
	lookup->section_authority = section_authority;
	lookup->section_additional = section_additional;
	lookup->new_search = true;
	lookup->besteffort = false;
	if (nofail) {
		lookup->servfail_stops = false;
	}
	ISC_LIST_INIT(lookup->q);
	ISC_LINK_INIT(lookup, link);
	ISC_LIST_APPEND(lookup_list, lookup, link);
	lookup->origin = NULL;
	ISC_LIST_INIT(lookup->my_server_list);
	debug("looking up %s", lookup->textname);
}

static void
do_next_command(char *input) {
	char *ptr, *arg, *last;

	if ((ptr = strtok_r(input, " \t\r\n", &last)) == NULL) {
		return;
	}
	arg = strtok_r(NULL, " \t\r\n", &last);
	if ((strcasecmp(ptr, "set") == 0) && (arg != NULL)) {
		setoption(arg);
	} else if ((strcasecmp(ptr, "server") == 0) ||
		   (strcasecmp(ptr, "lserver") == 0))
	{
		set_nameserver(arg);
		check_ra = false;
		show_settings(true, true);
	} else if (strcasecmp(ptr, "exit") == 0) {
		in_use = false;
	} else if (strcasecmp(ptr, "help") == 0 || strcasecmp(ptr, "?") == 0) {
		printf("The '%s' command is not yet implemented.\n", ptr);
	} else if (strcasecmp(ptr, "finger") == 0 ||
		   strcasecmp(ptr, "root") == 0 || strcasecmp(ptr, "ls") == 0 ||
		   strcasecmp(ptr, "view") == 0)
	{
		printf("The '%s' command is not implemented.\n", ptr);
	} else {
		addlookup(ptr);
	}
}

static void
readline_next_command(void *arg) {
	char *ptr = NULL;

	UNUSED(arg);

	isc_loopmgr_blocking();
	ptr = readline("> ");
	isc_loopmgr_nonblocking();
	if (ptr == NULL) {
		return;
	}

	if (*ptr != 0) {
		add_history(ptr);
		strlcpy(cmdlinebuf, ptr, COMMSIZE);
		cmdline = cmdlinebuf;
	}
	free(ptr);
}

static void
fgets_next_command(void *arg) {
	UNUSED(arg);

	cmdline = fgets(cmdlinebuf, COMMSIZE, stdin);
}

ISC_NORETURN static void
usage(void);

static void
usage(void) {
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, "   nslookup [-opt ...]             # interactive mode "
			"using default server\n");
	fprintf(stderr, "   nslookup [-opt ...] - server    # interactive mode "
			"using 'server'\n");
	fprintf(stderr, "   nslookup [-opt ...] host        # just look up "
			"'host' using default server\n");
	fprintf(stderr, "   nslookup [-opt ...] host server # just look up "
			"'host' using 'server'\n");
	exit(EXIT_FAILURE);
}

static void
parse_args(int argc, char **argv) {
	bool have_lookup = false;

	usesearch = true;
	for (argc--, argv++; argc > 0 && argv[0] != NULL; argc--, argv++) {
		debug("main parsing %s", argv[0]);
		if (argv[0][0] == '-') {
			if (strncasecmp(argv[0], "-ver", 4) == 0) {
				printf("nslookup %s\n", PACKAGE_VERSION);
				exit(EXIT_SUCCESS);
			} else if (argv[0][1] != 0) {
				setoption(&argv[0][1]);
			} else {
				have_lookup = true;
			}
		} else {
			if (!have_lookup) {
				have_lookup = true;
				in_use = true;
				addlookup(argv[0]);
			} else {
				if (argv[1] != NULL) {
					usage();
				}
				set_nameserver(argv[0]);
				check_ra = false;
			}
		}
	}
}

static void
start_next_command(void);

static void
process_next_command(void *arg ISC_ATTR_UNUSED) {
	isc_loop_t *loop = isc_loop_main();
	if (cmdline == NULL) {
		in_use = false;
	} else {
		do_next_command(cmdline);
		if (ISC_LIST_HEAD(lookup_list) != NULL) {
			isc_async_run(loop, run_loop, NULL);
			return;
		}
	}

	start_next_command();
}

static void
start_next_command(void) {
	isc_loop_t *loop = isc_loop_main();
	if (!in_use) {
		isc_loopmgr_shutdown();
		return;
	}

	cmdline = NULL;

	isc_loopmgr_pause();
	if (interactive) {
		isc_work_enqueue(loop, readline_next_command,
				 process_next_command, loop);
	} else {
		isc_work_enqueue(loop, fgets_next_command, process_next_command,
				 loop);
	}
	isc_loopmgr_resume();
}

static void
read_loop(void *arg) {
	UNUSED(arg);

	start_next_command();
}

int
main(int argc, char **argv) {
	interactive = isatty(0);

	ISC_LIST_INIT(lookup_list);
	ISC_LIST_INIT(server_list);
	ISC_LIST_INIT(search_list);

	check_ra = true;

	/* setup dighost callbacks */
	dighost_printmessage = printmessage;
	dighost_received = received;
	dighost_trying = trying;
	dighost_shutdown = start_next_command;

	setup_libs(argc, argv);

	setup_system(false, false);
	parse_args(argc, argv);
	if (keyfile[0] != 0) {
		setup_file_key();
	} else if (keysecret[0] != 0) {
		setup_text_key();
	}
	if (domainopt[0] != '\0') {
		set_search_domain(domainopt);
	}
	if (in_use) {
		isc_loopmgr_setup(run_loop, NULL);
	} else {
		isc_loopmgr_setup(read_loop, NULL);
	}
	in_use = !in_use;

	isc_loopmgr_run();

	puts("");
	debug("done, and starting to shut down");
	cancel_all();
	destroy_libs();

	return query_error | print_error;
}
