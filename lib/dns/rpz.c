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

/*! \file */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <isc/async.h>
#include <isc/buffer.h>
#include <isc/log.h>
#include <isc/loop.h>
#include <isc/magic.h>
#include <isc/mem.h>
#include <isc/net.h>
#include <isc/netaddr.h>
#include <isc/refcount.h>
#include <isc/result.h>
#include <isc/rwlock.h>
#include <isc/string.h>
#include <isc/util.h>
#include <isc/work.h>

#include <dns/db.h>
#include <dns/dbiterator.h>
#include <dns/fixedname.h>
#include <dns/qp.h>
#include <dns/rdata.h>
#include <dns/rdataset.h>
#include <dns/rdatasetiter.h>
#include <dns/rdatastruct.h>
#include <dns/rpz.h>
#include <dns/view.h>

#define DNS_RPZ_ZONE_MAGIC  ISC_MAGIC('r', 'p', 'z', ' ')
#define DNS_RPZ_ZONES_MAGIC ISC_MAGIC('r', 'p', 'z', 's')

#define DNS_RPZ_ZONE_VALID(rpz)	  ISC_MAGIC_VALID(rpz, DNS_RPZ_ZONE_MAGIC)
#define DNS_RPZ_ZONES_VALID(rpzs) ISC_MAGIC_VALID(rpzs, DNS_RPZ_ZONES_MAGIC)

/*
 * Parallel radix trees for databases of response policy IP addresses
 *
 * The radix or patricia trees are somewhat specialized to handle response
 * policy addresses by representing the two sets of IP addresses and name
 * server IP addresses in a single tree.  One set of IP addresses is
 * for rpz-ip policies or policies triggered by addresses in A or
 * AAAA records in responses.
 * The second set is for rpz-nsip policies or policies triggered by addresses
 * in A or AAAA records for NS records that are authorities for responses.
 *
 * Each leaf indicates that an IP address is listed in the IP address or the
 * name server IP address policy sub-zone (or both) of the corresponding
 * response policy zone.  The policy data such as a CNAME or an A record
 * is kept in the policy zone.  After an IP address has been found in a radix
 * tree, the node in the policy zone's database is found by converting
 * the IP address to a domain name in a canonical form.
 *
 *
 * The response policy zone canonical form of an IPv6 address is one of:
 *	prefix.W.W.W.W.W.W.W.W
 *	prefix.WORDS.zz
 *	prefix.WORDS.zz.WORDS
 *	prefix.zz.WORDS
 *  where
 *	prefix	is the prefix length of the IPv6 address between 1 and 128
 *	W	is a number between 0 and 65535
 *	WORDS	is one or more numbers W separated with "."
 *	zz	corresponds to :: in the standard IPv6 text representation
 *
 * The canonical form of IPv4 addresses is:
 *	prefix.B.B.B.B
 *  where
 *	prefix	is the prefix length of the address between 1 and 32
 *	B	is a number between 0 and 255
 *
 * Names for IPv4 addresses are distinguished from IPv6 addresses by having
 * 5 labels all of which are numbers, and a prefix between 1 and 32.
 */

/*
 * Nodes hashtable calculation parameters
 */
#define DNS_RPZ_HTSIZE_MAX 24
#define DNS_RPZ_HTSIZE_DIV 3

static isc_result_t
dns__rpz_shuttingdown(dns_rpz_zones_t *rpzs);
static void
dns__rpz_timer_cb(void *);
static void
dns__rpz_timer_start(dns_rpz_zone_t *rpz);

/*
 * Use a private definition of IPv6 addresses because s6_addr32 is not
 * always defined and our IPv6 addresses are in non-standard byte order
 */
typedef uint32_t dns_rpz_cidr_word_t;
#define DNS_RPZ_CIDR_WORD_BITS ((int)sizeof(dns_rpz_cidr_word_t) * 8)
#define DNS_RPZ_CIDR_KEY_BITS  ((int)sizeof(dns_rpz_cidr_key_t) * 8)
#define DNS_RPZ_CIDR_WORDS     (128 / DNS_RPZ_CIDR_WORD_BITS)
typedef struct {
	dns_rpz_cidr_word_t w[DNS_RPZ_CIDR_WORDS];
} dns_rpz_cidr_key_t;

#define ADDR_V4MAPPED 0xffff
#define KEY_IS_IPV4(prefix, ip)                                  \
	((prefix) >= 96 && (ip)->w[0] == 0 && (ip)->w[1] == 0 && \
	 (ip)->w[2] == ADDR_V4MAPPED)

#define DNS_RPZ_WORD_MASK(b)                   \
	((b) == 0 ? (dns_rpz_cidr_word_t)(-1)  \
		  : ((dns_rpz_cidr_word_t)(-1) \
		     << (DNS_RPZ_CIDR_WORD_BITS - (b))))

/*
 * Get bit #n from the array of words of an IP address.
 */
#define DNS_RPZ_IP_BIT(ip, n)                          \
	(1 & ((ip)->w[(n) / DNS_RPZ_CIDR_WORD_BITS] >> \
	      (DNS_RPZ_CIDR_WORD_BITS - 1 - ((n) % DNS_RPZ_CIDR_WORD_BITS))))

/*
 * A triplet of arrays of bits flagging the existence of
 * client-IP, IP, and NSIP policy triggers.
 */
typedef struct dns_rpz_addr_zbits dns_rpz_addr_zbits_t;
struct dns_rpz_addr_zbits {
	dns_rpz_zbits_t client_ip;
	dns_rpz_zbits_t ip;
	dns_rpz_zbits_t nsip;
};

/*
 * A CIDR or radix tree node.
 */
struct dns_rpz_cidr_node {
	dns_rpz_cidr_node_t *parent;
	dns_rpz_cidr_node_t *child[2];
	dns_rpz_cidr_key_t ip;
	dns_rpz_prefix_t prefix;
	dns_rpz_addr_zbits_t set;
	dns_rpz_addr_zbits_t sum;
};

/*
 * A pair of arrays of bits flagging the existence of
 * QNAME and NSDNAME policy triggers.
 */
typedef struct dns_rpz_nm_zbits dns_rpz_nm_zbits_t;
struct dns_rpz_nm_zbits {
	dns_rpz_zbits_t qname;
	dns_rpz_zbits_t ns;
};

/*
 * The data for a name in the summary database. This has two pairs of bits
 * for policy zones: one pair is for the exact name of the node, such as
 * example.com, and the other pair is for a wildcard child such as
 * *.example.com.
 */
typedef struct nmdata nmdata_t;
struct nmdata {
	dns_name_t name;
	isc_mem_t *mctx;
	isc_refcount_t references;
	dns_rpz_nm_zbits_t set;
	dns_rpz_nm_zbits_t wild;
};

#ifdef DNS_RPZ_TRACE
#define nmdata_ref(ptr)	  nmdata__ref(ptr, __func__, __FILE__, __LINE__)
#define nmdata_unref(ptr) nmdata__unref(ptr, __func__, __FILE__, __LINE__)
#define nmdata_attach(ptr, ptrp) \
	nmdata__attach(ptr, ptrp, __func__, __FILE__, __LINE__)
#define nmdata_detach(ptrp) nmdata__detach(ptrp, __func__, __FILE__, __LINE__)
ISC_REFCOUNT_TRACE_DECL(nmdata);
#else
ISC_REFCOUNT_DECL(nmdata);
#endif

static isc_result_t
rpz_add(dns_rpz_zone_t *rpz, const dns_name_t *src_name);
static void
rpz_del(dns_rpz_zone_t *rpz, const dns_name_t *src_name);

static nmdata_t *
new_nmdata(isc_mem_t *mctx, const dns_name_t *name, const nmdata_t *data);

/* QP trie methods */
static void
qp_attach(void *uctx, void *pval, uint32_t ival);
static void
qp_detach(void *uctx, void *pval, uint32_t ival);
static size_t
qp_makekey(dns_qpkey_t key, void *uctx, void *pval, uint32_t ival);
static void
qp_triename(void *uctx, char *buf, size_t size);

static dns_qpmethods_t qpmethods = {
	qp_attach,
	qp_detach,
	qp_makekey,
	qp_triename,
};

const char *
dns_rpz_type2str(dns_rpz_type_t type) {
	switch (type) {
	case DNS_RPZ_TYPE_CLIENT_IP:
		return "CLIENT-IP";
	case DNS_RPZ_TYPE_QNAME:
		return "QNAME";
	case DNS_RPZ_TYPE_IP:
		return "IP";
	case DNS_RPZ_TYPE_NSIP:
		return "NSIP";
	case DNS_RPZ_TYPE_NSDNAME:
		return "NSDNAME";
	case DNS_RPZ_TYPE_BAD:
		break;
	}
	FATAL_ERROR("impossible rpz type %d", type);
	return "impossible";
}

dns_rpz_policy_t
dns_rpz_str2policy(const char *str) {
	static struct {
		const char *str;
		dns_rpz_policy_t policy;
	} tbl[] = {
		{ "given", DNS_RPZ_POLICY_GIVEN },
		{ "disabled", DNS_RPZ_POLICY_DISABLED },
		{ "passthru", DNS_RPZ_POLICY_PASSTHRU },
		{ "drop", DNS_RPZ_POLICY_DROP },
		{ "tcp-only", DNS_RPZ_POLICY_TCP_ONLY },
		{ "nxdomain", DNS_RPZ_POLICY_NXDOMAIN },
		{ "nodata", DNS_RPZ_POLICY_NODATA },
		{ "cname", DNS_RPZ_POLICY_CNAME },
		{ "no-op", DNS_RPZ_POLICY_PASSTHRU }, /* old passthru */
	};
	unsigned int n;

	if (str == NULL) {
		return DNS_RPZ_POLICY_ERROR;
	}
	for (n = 0; n < sizeof(tbl) / sizeof(tbl[0]); ++n) {
		if (!strcasecmp(tbl[n].str, str)) {
			return tbl[n].policy;
		}
	}
	return DNS_RPZ_POLICY_ERROR;
}

const char *
dns_rpz_policy2str(dns_rpz_policy_t policy) {
	const char *str = NULL;

	switch (policy) {
	case DNS_RPZ_POLICY_PASSTHRU:
		str = "PASSTHRU";
		break;
	case DNS_RPZ_POLICY_DROP:
		str = "DROP";
		break;
	case DNS_RPZ_POLICY_TCP_ONLY:
		str = "TCP-ONLY";
		break;
	case DNS_RPZ_POLICY_NXDOMAIN:
		str = "NXDOMAIN";
		break;
	case DNS_RPZ_POLICY_NODATA:
		str = "NODATA";
		break;
	case DNS_RPZ_POLICY_RECORD:
		str = "Local-Data";
		break;
	case DNS_RPZ_POLICY_CNAME:
	case DNS_RPZ_POLICY_WILDCNAME:
		str = "CNAME";
		break;
	case DNS_RPZ_POLICY_MISS:
		str = "MISS";
		break;
	case DNS_RPZ_POLICY_DNS64:
		str = "DNS64";
		break;
	case DNS_RPZ_POLICY_ERROR:
		str = "ERROR";
		break;
	default:
		UNREACHABLE();
	}
	return str;
}

uint16_t
dns_rpz_str2ede(const char *str) {
	static struct {
		const char *str;
		uint16_t ede;
	} tbl[] = {
		{ "none", 0 },
		{ "forged", DNS_EDE_FORGEDANSWER },
		{ "blocked", DNS_EDE_BLOCKED },
		{ "censored", DNS_EDE_CENSORED },
		{ "filtered", DNS_EDE_FILTERED },
		{ "prohibited", DNS_EDE_PROHIBITED },
	};
	unsigned int n;

	if (str == NULL) {
		return UINT16_MAX;
	}
	for (n = 0; n < sizeof(tbl) / sizeof(tbl[0]); ++n) {
		if (!strcasecmp(tbl[n].str, str)) {
			return tbl[n].ede;
		}
	}
	return UINT16_MAX;
}

/*
 * Return the bit number of the highest set bit in 'zbit'.
 * (for example, 0x01 returns 0, 0xFF returns 7, etc.)
 */
static int
zbit_to_num(dns_rpz_zbits_t zbit) {
	dns_rpz_num_t rpz_num;

	REQUIRE(zbit != 0);
	rpz_num = 0;
	if ((zbit & 0xffffffff00000000ULL) != 0) {
		zbit >>= 32;
		rpz_num += 32;
	}
	if ((zbit & 0xffff0000) != 0) {
		zbit >>= 16;
		rpz_num += 16;
	}
	if ((zbit & 0xff00) != 0) {
		zbit >>= 8;
		rpz_num += 8;
	}
	if ((zbit & 0xf0) != 0) {
		zbit >>= 4;
		rpz_num += 4;
	}
	if ((zbit & 0xc) != 0) {
		zbit >>= 2;
		rpz_num += 2;
	}
	if ((zbit & 2) != 0) {
		++rpz_num;
	}
	return rpz_num;
}

/*
 * Make a set of bit masks given one or more bits and their type.
 */
static void
make_addr_set(dns_rpz_addr_zbits_t *tgt_set, dns_rpz_zbits_t zbits,
	      dns_rpz_type_t type) {
	switch (type) {
	case DNS_RPZ_TYPE_CLIENT_IP:
		tgt_set->client_ip = zbits;
		tgt_set->ip = 0;
		tgt_set->nsip = 0;
		break;
	case DNS_RPZ_TYPE_IP:
		tgt_set->client_ip = 0;
		tgt_set->ip = zbits;
		tgt_set->nsip = 0;
		break;
	case DNS_RPZ_TYPE_NSIP:
		tgt_set->client_ip = 0;
		tgt_set->ip = 0;
		tgt_set->nsip = zbits;
		break;
	default:
		UNREACHABLE();
	}
}

static void
make_nm_set(dns_rpz_nm_zbits_t *tgt_set, dns_rpz_num_t rpz_num,
	    dns_rpz_type_t type) {
	switch (type) {
	case DNS_RPZ_TYPE_QNAME:
		tgt_set->qname = DNS_RPZ_ZBIT(rpz_num);
		tgt_set->ns = 0;
		break;
	case DNS_RPZ_TYPE_NSDNAME:
		tgt_set->qname = 0;
		tgt_set->ns = DNS_RPZ_ZBIT(rpz_num);
		break;
	default:
		UNREACHABLE();
	}
}

/*
 * Mark a node and all of its parents as having client-IP, IP, or NSIP data
 */
static void
set_sum_pair(dns_rpz_cidr_node_t *cnode) {
	dns_rpz_addr_zbits_t sum;

	do {
		dns_rpz_cidr_node_t *child = cnode->child[0];
		sum = cnode->set;

		if (child != NULL) {
			sum.client_ip |= child->sum.client_ip;
			sum.ip |= child->sum.ip;
			sum.nsip |= child->sum.nsip;
		}

		child = cnode->child[1];
		if (child != NULL) {
			sum.client_ip |= child->sum.client_ip;
			sum.ip |= child->sum.ip;
			sum.nsip |= child->sum.nsip;
		}

		if (cnode->sum.client_ip == sum.client_ip &&
		    cnode->sum.ip == sum.ip && cnode->sum.nsip == sum.nsip)
		{
			break;
		}
		cnode->sum = sum;
		cnode = cnode->parent;
	} while (cnode != NULL);
}

/* Caller must hold rpzs->maint_lock */
static void
fix_qname_skip_recurse(dns_rpz_zones_t *rpzs) {
	dns_rpz_zbits_t mask;

	/*
	 * qname_wait_recurse and qname_skip_recurse are used to
	 * implement the "qname-wait-recurse" config option.
	 *
	 * When "qname-wait-recurse" is yes, no processing happens without
	 * recursion. In this case, qname_wait_recurse is true, and
	 * qname_skip_recurse (a bit field indicating which policy zones
	 * can be processed without recursion) is set to all 0's by
	 * fix_qname_skip_recurse().
	 *
	 * When "qname-wait-recurse" is no, qname_skip_recurse may be
	 * set to a non-zero value by fix_qname_skip_recurse(). The mask
	 * has to have bits set for the policy zones for which
	 * processing may continue without recursion, and bits cleared
	 * for the rest.
	 *
	 * (1) The ARM says:
	 *
	 *   The "qname-wait-recurse no" option overrides that default
	 *   behavior when recursion cannot change a non-error
	 *   response. The option does not affect QNAME or client-IP
	 *   triggers in policy zones listed after other zones
	 *   containing IP, NSIP and NSDNAME triggers, because those may
	 *   depend on the A, AAAA, and NS records that would be found
	 *   during recursive resolution.
	 *
	 * Let's consider the following:
	 *
	 *     zbits_req = (rpzs->have.ipv4 | rpzs->have.ipv6 |
	 *		    rpzs->have.nsdname |
	 *		    rpzs->have.nsipv4 | rpzs->have.nsipv6);
	 *
	 * zbits_req now contains bits set for zones which require
	 * recursion.
	 *
	 * But going by the description in the ARM, if the first policy
	 * zone requires recursion, then all zones after that (higher
	 * order bits) have to wait as well.  If the Nth zone requires
	 * recursion, then (N+1)th zone onwards all need to wait.
	 *
	 * So mapping this, examples:
	 *
	 * zbits_req = 0b000  mask = 0xffffffff (no zones have to wait for
	 *					 recursion)
	 * zbits_req = 0b001  mask = 0x00000000 (all zones have to wait)
	 * zbits_req = 0b010  mask = 0x00000001 (the first zone doesn't have to
	 *					 wait, second zone onwards need
	 *					 to wait)
	 * zbits_req = 0b011  mask = 0x00000000 (all zones have to wait)
	 * zbits_req = 0b100  mask = 0x00000011 (the 1st and 2nd zones don't
	 *					 have to wait, third zone
	 *					 onwards need to wait)
	 *
	 * More generally, we have to count the number of trailing 0
	 * bits in zbits_req and only these can be processed without
	 * recursion. All the rest need to wait.
	 *
	 * (2) The ARM says that "qname-wait-recurse no" option
	 * overrides the default behavior when recursion cannot change a
	 * non-error response. So, in the order of listing of policy
	 * zones, within the first policy zone where recursion may be
	 * required, we should first allow CLIENT-IP and QNAME policy
	 * records to be attempted without recursion.
	 */

	/*
	 * Get a mask covering all policy zones that are not subordinate to
	 * other policy zones containing triggers that require that the
	 * qname be resolved before they can be checked.
	 */
	rpzs->have.client_ip = rpzs->have.client_ipv4 | rpzs->have.client_ipv6;
	rpzs->have.ip = rpzs->have.ipv4 | rpzs->have.ipv6;
	rpzs->have.nsip = rpzs->have.nsipv4 | rpzs->have.nsipv6;

	if (rpzs->p.qname_wait_recurse) {
		mask = 0;
	} else {
		dns_rpz_zbits_t zbits_req;
		dns_rpz_zbits_t zbits_notreq;
		dns_rpz_zbits_t mask2;
		dns_rpz_zbits_t req_mask;

		/*
		 * Get the masks of zones with policies that
		 * do/don't require recursion
		 */

		zbits_req = (rpzs->have.ipv4 | rpzs->have.ipv6 |
			     rpzs->have.nsdname | rpzs->have.nsipv4 |
			     rpzs->have.nsipv6);
		zbits_notreq = (rpzs->have.client_ip | rpzs->have.qname);

		if (zbits_req == 0) {
			mask = DNS_RPZ_ALL_ZBITS;
			goto set;
		}

		/*
		 * req_mask is a mask covering used bits in
		 * zbits_req. (For instance, 0b1 => 0b1, 0b101 => 0b111,
		 * 0b11010101 => 0b11111111).
		 */
		req_mask = zbits_req;
		req_mask |= req_mask >> 1;
		req_mask |= req_mask >> 2;
		req_mask |= req_mask >> 4;
		req_mask |= req_mask >> 8;
		req_mask |= req_mask >> 16;
		req_mask |= req_mask >> 32;

		/*
		 * There's no point in skipping recursion for a later
		 * zone if it is required in a previous zone.
		 */
		if ((zbits_notreq & req_mask) == 0) {
			mask = 0;
			goto set;
		}

		/*
		 * This bit arithmetic creates a mask of zones in which
		 * it is okay to skip recursion. After the first zone
		 * that has to wait for recursion, all the others have
		 * to wait as well, so we want to create a mask in which
		 * all the trailing zeroes in zbits_req are are 1, and
		 * more significant bits are 0. (For instance,
		 * 0x0700 => 0x00ff, 0x0007 => 0x0000)
		 */
		mask = ~(zbits_req | ((~zbits_req) + 1));

		/*
		 * As mentioned in (2) above, the zone corresponding to
		 * the least significant zero could have its CLIENT-IP
		 * and QNAME policies checked before recursion, if it
		 * has any of those policies.  So if it does, we
		 * can set its 0 to 1.
		 *
		 * Locate the least significant 0 bit in the mask (for
		 * instance, 0xff => 0x100)...
		 */
		mask2 = (mask << 1) & ~mask;

		/*
		 * Also set the bit for zone 0, because if it's in
		 * zbits_notreq then it's definitely okay to attempt to
		 * skip recursion for zone 0...
		 */
		mask2 |= 1;

		/* Clear any bits *not* in zbits_notreq... */
		mask2 &= zbits_notreq;

		/* And merge the result into the skip-recursion mask */
		mask |= mask2;
	}

set:
	isc_log_write(DNS_LOGCATEGORY_RPZ, DNS_LOGMODULE_RPZ,
		      DNS_RPZ_DEBUG_QUIET,
		      "computed RPZ qname_skip_recurse mask=0x%" PRIx64,
		      (uint64_t)mask);
	rpzs->have.qname_skip_recurse = mask;
}

static void
adj_trigger_cnt(dns_rpz_zone_t *rpz, dns_rpz_type_t rpz_type,
		const dns_rpz_cidr_key_t *tgt_ip, dns_rpz_prefix_t tgt_prefix,
		bool inc) {
	dns_rpz_trigger_counter_t *cnt = NULL;
	dns_rpz_zbits_t *have = NULL;

	switch (rpz_type) {
	case DNS_RPZ_TYPE_CLIENT_IP:
		REQUIRE(tgt_ip != NULL);
		if (KEY_IS_IPV4(tgt_prefix, tgt_ip)) {
			cnt = &rpz->rpzs->triggers[rpz->num].client_ipv4;
			have = &rpz->rpzs->have.client_ipv4;
		} else {
			cnt = &rpz->rpzs->triggers[rpz->num].client_ipv6;
			have = &rpz->rpzs->have.client_ipv6;
		}
		break;
	case DNS_RPZ_TYPE_QNAME:
		cnt = &rpz->rpzs->triggers[rpz->num].qname;
		have = &rpz->rpzs->have.qname;
		break;
	case DNS_RPZ_TYPE_IP:
		REQUIRE(tgt_ip != NULL);
		if (KEY_IS_IPV4(tgt_prefix, tgt_ip)) {
			cnt = &rpz->rpzs->triggers[rpz->num].ipv4;
			have = &rpz->rpzs->have.ipv4;
		} else {
			cnt = &rpz->rpzs->triggers[rpz->num].ipv6;
			have = &rpz->rpzs->have.ipv6;
		}
		break;
	case DNS_RPZ_TYPE_NSDNAME:
		cnt = &rpz->rpzs->triggers[rpz->num].nsdname;
		have = &rpz->rpzs->have.nsdname;
		break;
	case DNS_RPZ_TYPE_NSIP:
		REQUIRE(tgt_ip != NULL);
		if (KEY_IS_IPV4(tgt_prefix, tgt_ip)) {
			cnt = &rpz->rpzs->triggers[rpz->num].nsipv4;
			have = &rpz->rpzs->have.nsipv4;
		} else {
			cnt = &rpz->rpzs->triggers[rpz->num].nsipv6;
			have = &rpz->rpzs->have.nsipv6;
		}
		break;
	default:
		UNREACHABLE();
	}

	if (inc) {
		if (++*cnt == 1U) {
			*have |= DNS_RPZ_ZBIT(rpz->num);
			fix_qname_skip_recurse(rpz->rpzs);
		}
	} else {
		REQUIRE(*cnt != 0U);
		if (--*cnt == 0U) {
			*have &= ~DNS_RPZ_ZBIT(rpz->num);
			fix_qname_skip_recurse(rpz->rpzs);
		}
	}
}

static dns_rpz_cidr_node_t *
new_node(dns_rpz_zones_t *rpzs, const dns_rpz_cidr_key_t *ip,
	 dns_rpz_prefix_t prefix, const dns_rpz_cidr_node_t *child) {
	dns_rpz_cidr_node_t *node = NULL;
	int i, words, wlen;

	node = isc_mem_get(rpzs->mctx, sizeof(*node));
	*node = (dns_rpz_cidr_node_t){
		.prefix = prefix,
	};

	if (child != NULL) {
		node->sum = child->sum;
	}

	words = prefix / DNS_RPZ_CIDR_WORD_BITS;
	wlen = prefix % DNS_RPZ_CIDR_WORD_BITS;
	i = 0;
	while (i < words) {
		node->ip.w[i] = ip->w[i];
		++i;
	}
	if (wlen != 0) {
		node->ip.w[i] = ip->w[i] & DNS_RPZ_WORD_MASK(wlen);
		++i;
	}
	while (i < DNS_RPZ_CIDR_WORDS) {
		node->ip.w[i++] = 0;
	}

	return node;
}

static void
badname(int level, const dns_name_t *name, const char *str1, const char *str2) {
	/*
	 * bin/tests/system/rpz/tests.sh looks for "invalid rpz".
	 */
	if (level < DNS_RPZ_DEBUG_QUIET && isc_log_wouldlog(level)) {
		char namebuf[DNS_NAME_FORMATSIZE];
		dns_name_format(name, namebuf, sizeof(namebuf));
		isc_log_write(DNS_LOGCATEGORY_RPZ, DNS_LOGMODULE_RPZ, level,
			      "invalid rpz IP address \"%s\"%s%s", namebuf,
			      str1, str2);
	}
}

/*
 * Convert an IP address from radix tree binary (host byte order) to
 * to its canonical response policy domain name without the origin of the
 * policy zone.
 *
 * Generate a name for an IPv6 address that fits RFC 5952, except that our
 * reversed format requires that when the length of the consecutive 16-bit
 * 0 fields are equal (e.g., 1.0.0.1.0.0.db8.2001 corresponding to
 * 2001:db8:0:0:1:0:0:1), we shorted the last instead of the first
 * (e.g., 1.0.0.1.zz.db8.2001 corresponding to 2001:db8::1:0:0:1).
 */
static isc_result_t
ip2name(const dns_rpz_cidr_key_t *tgt_ip, dns_rpz_prefix_t tgt_prefix,
	const dns_name_t *base_name, dns_name_t *ip_name) {
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif /* ifndef INET6_ADDRSTRLEN */
	char str[1 + 8 + 1 + INET6_ADDRSTRLEN + 1];
	isc_buffer_t buffer;
	isc_result_t result;
	int len;

	if (KEY_IS_IPV4(tgt_prefix, tgt_ip)) {
		len = snprintf(str, sizeof(str), "%u.%u.%u.%u.%u",
			       tgt_prefix - 96U, tgt_ip->w[3] & 0xffU,
			       (tgt_ip->w[3] >> 8) & 0xffU,
			       (tgt_ip->w[3] >> 16) & 0xffU,
			       (tgt_ip->w[3] >> 24) & 0xffU);
		if (len < 0 || (size_t)len >= sizeof(str)) {
			return ISC_R_FAILURE;
		}
	} else {
		int w[DNS_RPZ_CIDR_WORDS * 2];
		int best_first, best_len, cur_first, cur_len;

		len = snprintf(str, sizeof(str), "%d", tgt_prefix);
		if (len < 0 || (size_t)len >= sizeof(str)) {
			return ISC_R_FAILURE;
		}

		for (int n = 0; n < DNS_RPZ_CIDR_WORDS; n++) {
			w[n * 2 + 1] =
				((tgt_ip->w[DNS_RPZ_CIDR_WORDS - 1 - n] >> 16) &
				 0xffff);
			w[n * 2] = tgt_ip->w[DNS_RPZ_CIDR_WORDS - 1 - n] &
				   0xffff;
		}
		/*
		 * Find the start and length of the first longest sequence
		 * of zeros in the address.
		 */
		best_first = -1;
		best_len = 0;
		cur_first = -1;
		cur_len = 0;
		for (int n = 0; n <= 7; ++n) {
			if (w[n] != 0) {
				cur_len = 0;
				cur_first = -1;
			} else {
				++cur_len;
				if (cur_first < 0) {
					cur_first = n;
				} else if (cur_len >= best_len) {
					best_first = cur_first;
					best_len = cur_len;
				}
			}
		}

		for (int n = 0; n <= 7; ++n) {
			int i;

			INSIST(len > 0 && (size_t)len < sizeof(str));
			if (n == best_first) {
				i = snprintf(str + len, sizeof(str) - len,
					     ".zz");
				n += best_len - 1;
			} else {
				i = snprintf(str + len, sizeof(str) - len,
					     ".%x", w[n]);
			}
			if (i < 0 || (size_t)i >= (size_t)(sizeof(str) - len)) {
				return ISC_R_FAILURE;
			}
			len += i;
		}
	}

	isc_buffer_init(&buffer, str, sizeof(str));
	isc_buffer_add(&buffer, len);
	result = dns_name_fromtext(ip_name, &buffer, base_name, 0);
	return result;
}

/*
 * Determine the type of a name in a response policy zone.
 */
static dns_rpz_type_t
type_from_name(const dns_rpz_zones_t *rpzs, dns_rpz_zone_t *rpz,
	       const dns_name_t *name) {
	if (dns_name_issubdomain(name, &rpz->ip)) {
		return DNS_RPZ_TYPE_IP;
	}

	if (dns_name_issubdomain(name, &rpz->client_ip)) {
		return DNS_RPZ_TYPE_CLIENT_IP;
	}

	if ((rpzs->p.nsip_on & DNS_RPZ_ZBIT(rpz->num)) != 0 &&
	    dns_name_issubdomain(name, &rpz->nsip))
	{
		return DNS_RPZ_TYPE_NSIP;
	}

	if ((rpzs->p.nsdname_on & DNS_RPZ_ZBIT(rpz->num)) != 0 &&
	    dns_name_issubdomain(name, &rpz->nsdname))
	{
		return DNS_RPZ_TYPE_NSDNAME;
	}

	return DNS_RPZ_TYPE_QNAME;
}

/*
 * Convert an IP address from canonical response policy domain name form
 * to radix tree binary (host byte order) for adding or deleting IP or NSIP
 * data.
 */
static isc_result_t
name2ipkey(int log_level, dns_rpz_zone_t *rpz, dns_rpz_type_t rpz_type,
	   const dns_name_t *src_name, dns_rpz_cidr_key_t *tgt_ip,
	   dns_rpz_prefix_t *tgt_prefix, dns_rpz_addr_zbits_t *new_set) {
	char ip_str[DNS_NAME_FORMATSIZE];
	dns_fixedname_t ip_name2f;
	dns_name_t ip_name;
	const char *prefix_str = NULL, *cp = NULL, *end = NULL;
	char *cp2;
	int ip_labels;
	dns_rpz_prefix_t prefix;
	unsigned long prefix_num, l;
	isc_result_t result;
	int i;

	REQUIRE(rpz != NULL);
	REQUIRE(rpz->rpzs != NULL && rpz->num < rpz->rpzs->p.num_zones);

	make_addr_set(new_set, DNS_RPZ_ZBIT(rpz->num), rpz_type);

	ip_labels = dns_name_countlabels(src_name);
	if (rpz_type == DNS_RPZ_TYPE_QNAME) {
		ip_labels -= dns_name_countlabels(&rpz->origin);
	} else {
		ip_labels -= dns_name_countlabels(&rpz->nsdname);
	}
	if (ip_labels < 2) {
		badname(log_level, src_name, "; too short", "");
		return ISC_R_FAILURE;
	}
	dns_name_init(&ip_name);
	dns_name_getlabelsequence(src_name, 0, ip_labels, &ip_name);

	/*
	 * Get text for the IP address
	 */
	dns_name_format(&ip_name, ip_str, sizeof(ip_str));
	end = &ip_str[strlen(ip_str) + 1];
	prefix_str = ip_str;

	prefix_num = strtoul(prefix_str, &cp2, 10);
	if (*cp2 != '.') {
		badname(log_level, src_name, "; invalid leading prefix length",
			"");
		return ISC_R_FAILURE;
	}
	/*
	 * Patch in trailing nul character to print just the length
	 * label (for various cases below).
	 */
	*cp2 = '\0';
	if (prefix_num < 1U || prefix_num > 128U) {
		badname(log_level, src_name, "; invalid prefix length of ",
			prefix_str);
		return ISC_R_FAILURE;
	}
	cp = cp2 + 1;

	if (--ip_labels == 4 && !strchr(cp, 'z')) {
		/*
		 * Convert an IPv4 address
		 * from the form "prefix.z.y.x.w"
		 */
		if (prefix_num > 32U) {
			badname(log_level, src_name,
				"; invalid IPv4 prefix length of ", prefix_str);
			return ISC_R_FAILURE;
		}
		prefix_num += 96;
		*tgt_prefix = (dns_rpz_prefix_t)prefix_num;
		tgt_ip->w[0] = 0;
		tgt_ip->w[1] = 0;
		tgt_ip->w[2] = ADDR_V4MAPPED;
		tgt_ip->w[3] = 0;
		for (i = 0; i < 32; i += 8) {
			l = strtoul(cp, &cp2, 10);
			if (l > 255U || (*cp2 != '.' && *cp2 != '\0')) {
				if (*cp2 == '.') {
					*cp2 = '\0';
				}
				badname(log_level, src_name,
					"; invalid IPv4 octet ", cp);
				return ISC_R_FAILURE;
			}
			tgt_ip->w[3] |= l << i;
			cp = cp2 + 1;
		}
	} else {
		/*
		 * Convert a text IPv6 address.
		 */
		*tgt_prefix = (dns_rpz_prefix_t)prefix_num;
		for (i = 0; ip_labels > 0 && i < DNS_RPZ_CIDR_WORDS * 2;
		     ip_labels--)
		{
			if (cp[0] == 'z' && cp[1] == 'z' &&
			    (cp[2] == '.' || cp[2] == '\0') && i <= 6)
			{
				do {
					if ((i & 1) == 0) {
						tgt_ip->w[3 - i / 2] = 0;
					}
					++i;
				} while (ip_labels + i <= 8);
				cp += 3;
			} else {
				l = strtoul(cp, &cp2, 16);
				if (l > 0xffffu ||
				    (*cp2 != '.' && *cp2 != '\0'))
				{
					if (*cp2 == '.') {
						*cp2 = '\0';
					}
					badname(log_level, src_name,
						"; invalid IPv6 word ", cp);
					return ISC_R_FAILURE;
				}
				if ((i & 1) == 0) {
					tgt_ip->w[3 - i / 2] = l;
				} else {
					tgt_ip->w[3 - i / 2] |= l << 16;
				}
				i++;
				cp = cp2 + 1;
			}
		}
	}
	if (cp != end) {
		badname(log_level, src_name, "", "");
		return ISC_R_FAILURE;
	}

	/*
	 * Check for 1s after the prefix length.
	 */
	prefix = (dns_rpz_prefix_t)prefix_num;
	while (prefix < DNS_RPZ_CIDR_KEY_BITS) {
		dns_rpz_cidr_word_t aword;

		i = prefix % DNS_RPZ_CIDR_WORD_BITS;
		aword = tgt_ip->w[prefix / DNS_RPZ_CIDR_WORD_BITS];
		if ((aword & ~DNS_RPZ_WORD_MASK(i)) != 0) {
			badname(log_level, src_name,
				"; too small prefix length of ", prefix_str);
			return ISC_R_FAILURE;
		}
		prefix -= i;
		prefix += DNS_RPZ_CIDR_WORD_BITS;
	}

	/*
	 * Complain about bad names but be generous and accept them.
	 */
	if (log_level < DNS_RPZ_DEBUG_QUIET && isc_log_wouldlog(log_level)) {
		/*
		 * Convert the address back to a canonical domain name
		 * to ensure that the original name is in canonical form.
		 */
		dns_name_t *ip_name2 = dns_fixedname_initname(&ip_name2f);
		result = ip2name(tgt_ip, (dns_rpz_prefix_t)prefix_num, NULL,
				 ip_name2);
		if (result != ISC_R_SUCCESS ||
		    !dns_name_equal(&ip_name, ip_name2))
		{
			char ip2_str[DNS_NAME_FORMATSIZE];
			dns_name_format(ip_name2, ip2_str, sizeof(ip2_str));
			isc_log_write(DNS_LOGCATEGORY_RPZ, DNS_LOGMODULE_RPZ,
				      log_level,
				      "rpz IP address \"%s\""
				      " is not the canonical \"%s\"",
				      ip_str, ip2_str);
		}
	}

	return ISC_R_SUCCESS;
}

/*
 * Get trigger name and data bits for adding or deleting summary NSDNAME
 * or QNAME data.
 */
static void
name2data(dns_rpz_zone_t *rpz, dns_rpz_type_t rpz_type,
	  const dns_name_t *src_name, dns_name_t *trig_name,
	  nmdata_t *new_data) {
	dns_name_t tmp_name;
	unsigned int prefix_len, n;

	REQUIRE(rpz != NULL);
	REQUIRE(rpz->rpzs != NULL && rpz->num < rpz->rpzs->p.num_zones);

	/*
	 * Handle wildcards by putting only the parent into the
	 * summary database.  The database only causes a check of the
	 * real policy zone where wildcards will be handled.
	 */
	if (dns_name_iswildcard(src_name)) {
		prefix_len = 1;
		memset(&new_data->set, 0, sizeof(new_data->set));
		make_nm_set(&new_data->wild, rpz->num, rpz_type);
	} else {
		prefix_len = 0;
		make_nm_set(&new_data->set, rpz->num, rpz_type);
		memset(&new_data->wild, 0, sizeof(new_data->wild));
	}

	dns_name_init(&tmp_name);
	n = dns_name_countlabels(src_name);
	n -= prefix_len;
	if (rpz_type == DNS_RPZ_TYPE_QNAME) {
		n -= dns_name_countlabels(&rpz->origin);
	} else {
		n -= dns_name_countlabels(&rpz->nsdname);
	}
	dns_name_getlabelsequence(src_name, prefix_len, n, &tmp_name);
	(void)dns_name_concatenate(&tmp_name, dns_rootname, trig_name);
}

#ifndef HAVE_BUILTIN_CLZ
/**
 * \brief Count Leading Zeros: Find the location of the left-most set
 * bit.
 */
static unsigned int
clz(dns_rpz_cidr_word_t w) {
	unsigned int bit;

	bit = DNS_RPZ_CIDR_WORD_BITS - 1;

	if ((w & 0xffff0000) != 0) {
		w >>= 16;
		bit -= 16;
	}

	if ((w & 0xff00) != 0) {
		w >>= 8;
		bit -= 8;
	}

	if ((w & 0xf0) != 0) {
		w >>= 4;
		bit -= 4;
	}

	if ((w & 0xc) != 0) {
		w >>= 2;
		bit -= 2;
	}

	if ((w & 2) != 0) {
		--bit;
	}

	return bit;
}
#endif /* ifndef HAVE_BUILTIN_CLZ */

/*
 * Find the first differing bit in two keys (IP addresses).
 */
static int
diff_keys(const dns_rpz_cidr_key_t *key1, dns_rpz_prefix_t prefix1,
	  const dns_rpz_cidr_key_t *key2, dns_rpz_prefix_t prefix2) {
	dns_rpz_cidr_word_t delta;
	dns_rpz_prefix_t maxbit, bit;
	int i;

	bit = 0;
	maxbit = ISC_MIN(prefix1, prefix2);

	/*
	 * find the first differing words
	 */
	for (i = 0; bit < maxbit; i++, bit += DNS_RPZ_CIDR_WORD_BITS) {
		delta = key1->w[i] ^ key2->w[i];
		if (delta != 0) {
#ifdef HAVE_BUILTIN_CLZ
			bit += __builtin_clz(delta);
#else  /* ifdef HAVE_BUILTIN_CLZ */
			bit += clz(delta);
#endif /* ifdef HAVE_BUILTIN_CLZ */
			break;
		}
	}
	return ISC_MIN(bit, maxbit);
}

/*
 * Given a hit while searching the radix trees,
 * clear all bits for higher numbered zones.
 */
static dns_rpz_zbits_t
trim_zbits(dns_rpz_zbits_t zbits, dns_rpz_zbits_t found) {
	dns_rpz_zbits_t x;

	/*
	 * Isolate the first or smallest numbered hit bit.
	 * Make a mask of that bit and all smaller numbered bits.
	 */
	x = zbits & found;
	x &= (~x + 1);
	x = (x << 1) - 1;
	zbits &= x;
	return zbits;
}

/*
 * Search a radix tree for an IP address for ordinary lookup
 *	or for a CIDR block adding or deleting an entry
 *
 * Return ISC_R_SUCCESS, DNS_R_PARTIALMATCH, ISC_R_NOTFOUND,
 *	    and *found=longest match node
 *	or with create==true, ISC_R_EXISTS
 */
static isc_result_t
search(dns_rpz_zones_t *rpzs, const dns_rpz_cidr_key_t *tgt_ip,
       dns_rpz_prefix_t tgt_prefix, const dns_rpz_addr_zbits_t *tgt_set,
       bool create, dns_rpz_cidr_node_t **found) {
	dns_rpz_cidr_node_t *cur = rpzs->cidr;
	dns_rpz_cidr_node_t *parent = NULL, *child = NULL;
	dns_rpz_cidr_node_t *new_parent = NULL, *sibling = NULL;
	dns_rpz_addr_zbits_t set = *tgt_set;
	int cur_num = 0, child_num;
	isc_result_t find_result = ISC_R_NOTFOUND;

	*found = NULL;
	for (;;) {
		dns_rpz_prefix_t dbit;
		if (cur == NULL) {
			/*
			 * No child so we cannot go down.
			 * Quit with whatever we already found
			 * or add the target as a child of the current parent.
			 */
			if (!create) {
				return find_result;
			}
			child = new_node(rpzs, tgt_ip, tgt_prefix, NULL);
			if (parent == NULL) {
				rpzs->cidr = child;
			} else {
				parent->child[cur_num] = child;
			}
			child->parent = parent;
			child->set.client_ip |= tgt_set->client_ip;
			child->set.ip |= tgt_set->ip;
			child->set.nsip |= tgt_set->nsip;
			set_sum_pair(child);
			*found = child;
			return ISC_R_SUCCESS;
		}

		if ((cur->sum.client_ip & set.client_ip) == 0 &&
		    (cur->sum.ip & set.ip) == 0 &&
		    (cur->sum.nsip & set.nsip) == 0)
		{
			/*
			 * This node has no relevant data
			 * and is in none of the target trees.
			 * Pretend it does not exist if we are not adding.
			 *
			 * If we are adding, continue down to eventually add
			 * a node and mark/put this node in the correct tree.
			 */
			if (!create) {
				return find_result;
			}
		}

		dbit = diff_keys(tgt_ip, tgt_prefix, &cur->ip, cur->prefix);
		/*
		 * dbit <= tgt_prefix and dbit <= cur->prefix always.
		 * We are finished searching if we matched all of the target.
		 */
		if (dbit == tgt_prefix) {
			if (tgt_prefix == cur->prefix) {
				/*
				 * The node's key matches the target exactly.
				 */
				if ((cur->set.client_ip & set.client_ip) != 0 ||
				    (cur->set.ip & set.ip) != 0 ||
				    (cur->set.nsip & set.nsip) != 0)
				{
					/*
					 * It is the answer if it has data.
					 */
					*found = cur;
					if (create) {
						find_result = ISC_R_EXISTS;
					} else {
						find_result = ISC_R_SUCCESS;
					}
				} else if (create) {
					/*
					 * The node lacked relevant data,
					 * but will have it now.
					 */
					cur->set.client_ip |=
						tgt_set->client_ip;
					cur->set.ip |= tgt_set->ip;
					cur->set.nsip |= tgt_set->nsip;
					set_sum_pair(cur);
					*found = cur;
					find_result = ISC_R_SUCCESS;
				}
				return find_result;
			}

			/*
			 * We know tgt_prefix < cur->prefix which means that
			 * the target is shorter than the current node.
			 * Add the target as the current node's parent.
			 */
			if (!create) {
				return find_result;
			}

			new_parent = new_node(rpzs, tgt_ip, tgt_prefix, cur);
			new_parent->parent = parent;
			if (parent == NULL) {
				rpzs->cidr = new_parent;
			} else {
				parent->child[cur_num] = new_parent;
			}
			child_num = DNS_RPZ_IP_BIT(&cur->ip, tgt_prefix);
			new_parent->child[child_num] = cur;
			cur->parent = new_parent;
			new_parent->set = *tgt_set;
			set_sum_pair(new_parent);
			*found = new_parent;
			return ISC_R_SUCCESS;
		}

		if (dbit == cur->prefix) {
			if ((cur->set.client_ip & set.client_ip) != 0 ||
			    (cur->set.ip & set.ip) != 0 ||
			    (cur->set.nsip & set.nsip) != 0)
			{
				/*
				 * We have a partial match between of all of the
				 * current node but only part of the target.
				 * Continue searching for other hits in the
				 * same or lower numbered trees.
				 */
				find_result = DNS_R_PARTIALMATCH;
				*found = cur;
				set.client_ip = trim_zbits(set.client_ip,
							   cur->set.client_ip);
				set.ip = trim_zbits(set.ip, cur->set.ip);
				set.nsip = trim_zbits(set.nsip, cur->set.nsip);
			}
			parent = cur;
			cur_num = DNS_RPZ_IP_BIT(tgt_ip, dbit);
			cur = cur->child[cur_num];
			continue;
		}

		/*
		 * dbit < tgt_prefix and dbit < cur->prefix,
		 * so we failed to match both the target and the current node.
		 * Insert a fork of a parent above the current node and
		 * add the target as a sibling of the current node
		 */
		if (!create) {
			return find_result;
		}

		sibling = new_node(rpzs, tgt_ip, tgt_prefix, NULL);
		new_parent = new_node(rpzs, tgt_ip, dbit, cur);
		new_parent->parent = parent;
		if (parent == NULL) {
			rpzs->cidr = new_parent;
		} else {
			parent->child[cur_num] = new_parent;
		}
		child_num = DNS_RPZ_IP_BIT(tgt_ip, dbit);
		new_parent->child[child_num] = sibling;
		new_parent->child[1 - child_num] = cur;
		cur->parent = new_parent;
		sibling->parent = new_parent;
		sibling->set = *tgt_set;
		set_sum_pair(sibling);
		*found = sibling;
		return ISC_R_SUCCESS;
	}
}

/*
 * Add an IP address to the radix tree.
 */
static isc_result_t
add_cidr(dns_rpz_zone_t *rpz, dns_rpz_type_t rpz_type,
	 const dns_name_t *src_name) {
	dns_rpz_cidr_key_t tgt_ip;
	dns_rpz_prefix_t tgt_prefix;
	dns_rpz_addr_zbits_t set;
	dns_rpz_cidr_node_t *found = NULL;
	isc_result_t result;

	result = name2ipkey(DNS_RPZ_ERROR_LEVEL, rpz, rpz_type, src_name,
			    &tgt_ip, &tgt_prefix, &set);
	/*
	 * Log complaints about bad owner names but let the zone load.
	 */
	if (result != ISC_R_SUCCESS) {
		return ISC_R_SUCCESS;
	}

	RWLOCK(&rpz->rpzs->search_lock, isc_rwlocktype_write);
	result = search(rpz->rpzs, &tgt_ip, tgt_prefix, &set, true, &found);
	if (result != ISC_R_SUCCESS) {
		char namebuf[DNS_NAME_FORMATSIZE];

		/*
		 * Do not worry if the radix tree already exists,
		 * because diff_apply() likes to add nodes before deleting.
		 */
		if (result == ISC_R_EXISTS) {
			result = ISC_R_SUCCESS;
			goto done;
		}

		/*
		 * bin/tests/system/rpz/tests.sh looks for "rpz.*failed".
		 */
		dns_name_format(src_name, namebuf, sizeof(namebuf));
		isc_log_write(DNS_LOGCATEGORY_RPZ, DNS_LOGMODULE_RPZ,
			      DNS_RPZ_ERROR_LEVEL,
			      "rpz add_cidr(%s) failed: %s", namebuf,
			      isc_result_totext(result));
		goto done;
	}

	adj_trigger_cnt(rpz, rpz_type, &tgt_ip, tgt_prefix, true);
done:
	RWUNLOCK(&rpz->rpzs->search_lock, isc_rwlocktype_write);
	return result;
}

static nmdata_t *
new_nmdata(isc_mem_t *mctx, const dns_name_t *name, const nmdata_t *data) {
	nmdata_t *newdata = isc_mem_get(mctx, sizeof(*newdata));
	*newdata = (nmdata_t){
		.set = data->set,
		.wild = data->wild,
		.name = DNS_NAME_INITEMPTY,
		.references = ISC_REFCOUNT_INITIALIZER(1),
	};
	dns_name_dup(name, mctx, &newdata->name);
	isc_mem_attach(mctx, &newdata->mctx);

#ifdef DNS_RPZ_TRACE
	fprintf(stderr, "new_nmdata:%s:%s:%d:%p->references = 1\n", __func__,
		__FILE__, __LINE__ + 1, name);
#endif

	return newdata;
}

static isc_result_t
add_nm(dns_rpz_zones_t *rpzs, dns_name_t *trig_name, const nmdata_t *new_data) {
	isc_result_t result;
	nmdata_t *data = NULL;
	dns_qp_t *qp = NULL;

	dns_qpmulti_write(rpzs->table, &qp);
	result = dns_qp_getname(qp, trig_name, DNS_DBNAMESPACE_NORMAL,
				(void **)&data, NULL);
	if (result != ISC_R_SUCCESS) {
		INSIST(data == NULL);
		data = new_nmdata(rpzs->mctx, trig_name, new_data);
		result = dns_qp_insert(qp, data, 0);
		nmdata_detach(&data);
		goto done;
	}

	/*
	 * Do not count bits that are already present
	 */
	if ((data->set.qname & new_data->set.qname) != 0 ||
	    (data->set.ns & new_data->set.ns) != 0 ||
	    (data->wild.qname & new_data->wild.qname) != 0 ||
	    (data->wild.ns & new_data->wild.ns) != 0)
	{
		result = ISC_R_EXISTS;
	}

	/* copy in the bits from the new data */
	data->set.qname |= new_data->set.qname;
	data->set.ns |= new_data->set.ns;
	data->wild.qname |= new_data->wild.qname;
	data->wild.ns |= new_data->wild.ns;

done:
	dns_qp_compact(qp, DNS_QPGC_MAYBE);
	dns_qpmulti_commit(rpzs->table, &qp);

	return result;
}

static isc_result_t
add_name(dns_rpz_zone_t *rpz, dns_rpz_type_t rpz_type,
	 const dns_name_t *src_name) {
	nmdata_t new_data;
	dns_fixedname_t trig_namef;
	dns_name_t *trig_name = NULL;
	isc_result_t result;

	/*
	 * We need a summary database of names even with 1 policy zone,
	 * because wildcard triggers are handled differently.
	 */

	trig_name = dns_fixedname_initname(&trig_namef);
	name2data(rpz, rpz_type, src_name, trig_name, &new_data);

	result = add_nm(rpz->rpzs, trig_name, &new_data);

	/*
	 * Do not worry if the node already exists,
	 * because diff_apply() likes to add nodes before deleting.
	 */
	if (result == ISC_R_EXISTS) {
		return ISC_R_SUCCESS;
	}
	if (result == ISC_R_SUCCESS) {
		RWLOCK(&rpz->rpzs->search_lock, isc_rwlocktype_write);
		adj_trigger_cnt(rpz, rpz_type, NULL, 0, true);
		RWUNLOCK(&rpz->rpzs->search_lock, isc_rwlocktype_write);
	}
	return result;
}

/*
 * Get ready for a new set of policy zones for a view.
 */
isc_result_t
dns_rpz_new_zones(dns_view_t *view, dns_rpz_zones_t **rpzsp) {
	dns_rpz_zones_t *rpzs = NULL;
	isc_mem_t *mctx = NULL;

	REQUIRE(rpzsp != NULL && *rpzsp == NULL);
	REQUIRE(view != NULL);

	mctx = view->mctx;

	rpzs = isc_mem_get(mctx, sizeof(*rpzs));
	*rpzs = (dns_rpz_zones_t){
		.magic = DNS_RPZ_ZONES_MAGIC,
	};

	isc_rwlock_init(&rpzs->search_lock);
	isc_mutex_init(&rpzs->maint_lock);
	isc_refcount_init(&rpzs->references, 1);

	dns_qpmulti_create(mctx, &qpmethods, view, &rpzs->table);

	isc_mem_attach(mctx, &rpzs->mctx);

	*rpzsp = rpzs;
	return ISC_R_SUCCESS;
}

isc_result_t
dns_rpz_new_zone(dns_rpz_zones_t *rpzs, dns_rpz_zone_t **rpzp) {
	isc_result_t result;
	dns_rpz_zone_t *rpz = NULL;

	REQUIRE(DNS_RPZ_ZONES_VALID(rpzs));
	REQUIRE(rpzp != NULL && *rpzp == NULL);

	if (rpzs->p.num_zones >= DNS_RPZ_MAX_ZONES) {
		return ISC_R_NOSPACE;
	}

	result = dns__rpz_shuttingdown(rpzs);
	if (result != ISC_R_SUCCESS) {
		return result;
	}

	rpz = isc_mem_get(rpzs->mctx, sizeof(*rpz));
	*rpz = (dns_rpz_zone_t){
		.addsoa = true,
		.magic = DNS_RPZ_ZONE_MAGIC,
		.rpzs = rpzs,
	};

	/*
	 * This will never be used, but costs us nothing and
	 * simplifies update_from_db().
	 */

	isc_ht_init(&rpz->nodes, rpzs->mctx, 1, ISC_HT_CASE_SENSITIVE);

	dns_name_init(&rpz->origin);
	dns_name_init(&rpz->client_ip);
	dns_name_init(&rpz->ip);
	dns_name_init(&rpz->nsdname);
	dns_name_init(&rpz->nsip);
	dns_name_init(&rpz->passthru);
	dns_name_init(&rpz->drop);
	dns_name_init(&rpz->tcp_only);
	dns_name_init(&rpz->cname);

	isc_time_settoepoch(&rpz->lastupdated);

	rpz->num = rpzs->p.num_zones++;
	rpzs->zones[rpz->num] = rpz;

	*rpzp = rpz;

	return ISC_R_SUCCESS;
}

isc_result_t
dns_rpz_dbupdate_callback(dns_db_t *db, void *fn_arg) {
	dns_rpz_zone_t *rpz = (dns_rpz_zone_t *)fn_arg;
	isc_result_t result = ISC_R_SUCCESS;

	REQUIRE(DNS_DB_VALID(db));
	REQUIRE(DNS_RPZ_ZONE_VALID(rpz));

	LOCK(&rpz->rpzs->maint_lock);

	if (rpz->rpzs->shuttingdown) {
		result = ISC_R_SHUTTINGDOWN;
		goto unlock;
	}

	/* New zone came as AXFR */
	if (rpz->db != NULL && rpz->db != db) {
		/* We need to clean up the old DB */
		if (rpz->dbversion != NULL) {
			dns_db_closeversion(rpz->db, &rpz->dbversion, false);
		}
		dns_db_updatenotify_unregister(rpz->db,
					       dns_rpz_dbupdate_callback, rpz);
		dns_db_detach(&rpz->db);
	}

	if (rpz->db == NULL) {
		RUNTIME_CHECK(rpz->dbversion == NULL);
		dns_db_attach(db, &rpz->db);
	}

	if (!rpz->updatepending && !rpz->updaterunning) {
		rpz->updatepending = true;

		dns_db_currentversion(rpz->db, &rpz->dbversion);
		dns__rpz_timer_start(rpz);
	} else {
		char dname[DNS_NAME_FORMATSIZE];
		rpz->updatepending = true;

		dns_name_format(&rpz->origin, dname, DNS_NAME_FORMATSIZE);
		isc_log_write(DNS_LOGCATEGORY_GENERAL, DNS_LOGMODULE_RPZ,
			      ISC_LOG_DEBUG(3),
			      "rpz: %s: update already queued or running",
			      dname);
		if (rpz->dbversion != NULL) {
			dns_db_closeversion(rpz->db, &rpz->dbversion, false);
		}
		dns_db_currentversion(rpz->db, &rpz->dbversion);
	}

unlock:
	UNLOCK(&rpz->rpzs->maint_lock);

	return result;
}

void
dns_rpz_dbupdate_unregister(dns_db_t *db, dns_rpz_zone_t *rpz) {
	REQUIRE(DNS_DB_VALID(db));
	REQUIRE(DNS_RPZ_ZONE_VALID(rpz));

	dns_db_updatenotify_unregister(db, dns_rpz_dbupdate_callback, rpz);
}

void
dns_rpz_dbupdate_register(dns_db_t *db, dns_rpz_zone_t *rpz) {
	REQUIRE(DNS_DB_VALID(db));
	REQUIRE(DNS_RPZ_ZONE_VALID(rpz));

	dns_db_updatenotify_register(db, dns_rpz_dbupdate_callback, rpz);
}
static void
dns__rpz_timer_start(dns_rpz_zone_t *rpz) {
	uint64_t tdiff;
	isc_interval_t interval;
	isc_time_t now;

	REQUIRE(DNS_RPZ_ZONE_VALID(rpz));

	now = isc_time_now();
	tdiff = isc_time_microdiff(&now, &rpz->lastupdated) / 1000000;
	if (tdiff < rpz->min_update_interval) {
		uint64_t defer = rpz->min_update_interval - tdiff;
		char dname[DNS_NAME_FORMATSIZE];

		dns_name_format(&rpz->origin, dname, DNS_NAME_FORMATSIZE);
		isc_log_write(DNS_LOGCATEGORY_GENERAL, DNS_LOGMODULE_RPZ,
			      ISC_LOG_INFO,
			      "rpz: %s: new zone version came "
			      "too soon, deferring update for "
			      "%" PRIu64 " seconds",
			      dname, defer);
		isc_interval_set(&interval, (unsigned int)defer, 0);
	} else {
		isc_interval_set(&interval, 0, 0);
	}

	rpz->loop = isc_loop();

	isc_timer_create(rpz->loop, dns__rpz_timer_cb, rpz, &rpz->updatetimer);
	isc_timer_start(rpz->updatetimer, isc_timertype_once, &interval);
}

static void
dns__rpz_timer_stop(void *arg) {
	dns_rpz_zone_t *rpz = arg;
	REQUIRE(DNS_RPZ_ZONE_VALID(rpz));

	isc_timer_stop(rpz->updatetimer);
	isc_timer_destroy(&rpz->updatetimer);
	rpz->loop = NULL;

	dns_rpz_zones_unref(rpz->rpzs);
}

static void
update_rpz_done_cb(void *data) {
	dns_rpz_zone_t *rpz = (dns_rpz_zone_t *)data;
	char dname[DNS_NAME_FORMATSIZE];

	REQUIRE(DNS_RPZ_ZONE_VALID(rpz));

	LOCK(&rpz->rpzs->maint_lock);
	rpz->updaterunning = false;

	dns_name_format(&rpz->origin, dname, DNS_NAME_FORMATSIZE);

	if (rpz->updatepending && !rpz->rpzs->shuttingdown) {
		/* Restart the timer */
		dns__rpz_timer_start(rpz);
	}

	dns_db_closeversion(rpz->updb, &rpz->updbversion, false);
	dns_db_detach(&rpz->updb);

	UNLOCK(&rpz->rpzs->maint_lock);

	isc_log_write(DNS_LOGCATEGORY_GENERAL, DNS_LOGMODULE_RPZ, ISC_LOG_INFO,
		      "rpz: %s: reload done: %s", dname,
		      isc_result_totext(rpz->updateresult));

	dns_rpz_zones_unref(rpz->rpzs);
}

static isc_result_t
update_nodes(dns_rpz_zone_t *rpz, isc_ht_t *newnodes) {
	isc_result_t result;
	dns_dbiterator_t *updbit = NULL;
	dns_name_t *name = NULL;
	dns_fixedname_t fixname;
	char domain[DNS_NAME_FORMATSIZE];

	dns_name_format(&rpz->origin, domain, DNS_NAME_FORMATSIZE);

	name = dns_fixedname_initname(&fixname);

	result = dns_db_createiterator(rpz->updb, DNS_DB_NONSEC3, &updbit);
	if (result != ISC_R_SUCCESS) {
		isc_log_write(DNS_LOGCATEGORY_GENERAL, DNS_LOGMODULE_RPZ,
			      ISC_LOG_ERROR,
			      "rpz: %s: failed to create DB iterator - %s",
			      domain, isc_result_totext(result));
		return result;
	}

	result = dns_dbiterator_first(updbit);
	if (result != ISC_R_SUCCESS && result != ISC_R_NOMORE) {
		isc_log_write(DNS_LOGCATEGORY_GENERAL, DNS_LOGMODULE_RPZ,
			      ISC_LOG_ERROR,
			      "rpz: %s: failed to get db iterator - %s", domain,
			      isc_result_totext(result));
		goto cleanup;
	}

	while (result == ISC_R_SUCCESS) {
		char namebuf[DNS_NAME_FORMATSIZE];
		dns_rdatasetiter_t *rdsiter = NULL;
		dns_dbnode_t *node = NULL;

		result = dns__rpz_shuttingdown(rpz->rpzs);
		if (result != ISC_R_SUCCESS) {
			goto cleanup;
		}

		result = dns_dbiterator_current(updbit, &node, name);
		if (result != ISC_R_SUCCESS) {
			isc_log_write(DNS_LOGCATEGORY_GENERAL,
				      DNS_LOGMODULE_RPZ, ISC_LOG_ERROR,
				      "rpz: %s: failed to get dbiterator - %s",
				      domain, isc_result_totext(result));
			goto cleanup;
		}

		result = dns_dbiterator_pause(updbit);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);

		result = dns_db_allrdatasets(rpz->updb, node, rpz->updbversion,
					     0, 0, &rdsiter);
		if (result != ISC_R_SUCCESS) {
			isc_log_write(DNS_LOGCATEGORY_GENERAL,
				      DNS_LOGMODULE_RPZ, ISC_LOG_ERROR,
				      "rpz: %s: failed to fetch "
				      "rrdatasets - %s",
				      domain, isc_result_totext(result));
			dns_db_detachnode(rpz->updb, &node);
			goto cleanup;
		}

		result = dns_rdatasetiter_first(rdsiter);

		dns_rdatasetiter_destroy(&rdsiter);
		dns_db_detachnode(rpz->updb, &node);

		if (result != ISC_R_SUCCESS) { /* skip empty non-terminal */
			if (result != ISC_R_NOMORE) {
				isc_log_write(
					DNS_LOGCATEGORY_GENERAL,
					DNS_LOGMODULE_RPZ, ISC_LOG_ERROR,
					"rpz: %s: error %s while creating "
					"rdatasetiter",
					domain, isc_result_totext(result));
			}
			goto next;
		}

		dns_name_downcase(name, name);

		/* Add entry to the new nodes table */
		result = isc_ht_add(newnodes, name->ndata, name->length, rpz);
		if (result != ISC_R_SUCCESS) {
			dns_name_format(name, namebuf, sizeof(namebuf));
			isc_log_write(DNS_LOGCATEGORY_GENERAL,
				      DNS_LOGMODULE_RPZ, ISC_LOG_ERROR,
				      "rpz: %s, adding node %s to HT error %s",
				      domain, namebuf,
				      isc_result_totext(result));
			goto next;
		}

		/* Does the entry exist in the old nodes table? */
		result = isc_ht_find(rpz->nodes, name->ndata, name->length,
				     NULL);
		if (result == ISC_R_SUCCESS) { /* found */
			isc_ht_delete(rpz->nodes, name->ndata, name->length);
			goto next;
		}

		/*
		 * Only the single rpz updates are serialized, so we need to
		 * lock here because we can be processing more updates to
		 * different rpz zones at the same time
		 */
		LOCK(&rpz->rpzs->maint_lock);
		result = rpz_add(rpz, name);
		UNLOCK(&rpz->rpzs->maint_lock);

		if (result != ISC_R_SUCCESS) {
			dns_name_format(name, namebuf, sizeof(namebuf));
			isc_log_write(DNS_LOGCATEGORY_GENERAL,
				      DNS_LOGMODULE_RPZ, ISC_LOG_ERROR,
				      "rpz: %s: adding node %s "
				      "to RPZ error %s",
				      domain, namebuf,
				      isc_result_totext(result));
		} else if (isc_log_wouldlog(ISC_LOG_DEBUG(3))) {
			dns_name_format(name, namebuf, sizeof(namebuf));
			isc_log_write(DNS_LOGCATEGORY_GENERAL,
				      DNS_LOGMODULE_RPZ, ISC_LOG_DEBUG(3),
				      "rpz: %s: adding node %s", domain,
				      namebuf);
		}

	next:
		result = dns_dbiterator_next(updbit);
	}
	INSIST(result != ISC_R_SUCCESS);
	if (result == ISC_R_NOMORE) {
		result = ISC_R_SUCCESS;
	}

cleanup:
	dns_dbiterator_destroy(&updbit);

	return result;
}

static isc_result_t
cleanup_nodes(dns_rpz_zone_t *rpz) {
	isc_result_t result;
	isc_ht_iter_t *iter = NULL;
	dns_name_t *name = NULL;
	dns_fixedname_t fixname;

	name = dns_fixedname_initname(&fixname);

	isc_ht_iter_create(rpz->nodes, &iter);

	for (result = isc_ht_iter_first(iter); result == ISC_R_SUCCESS;
	     result = isc_ht_iter_delcurrent_next(iter))
	{
		isc_region_t region;
		unsigned char *key = NULL;
		size_t keysize;

		result = dns__rpz_shuttingdown(rpz->rpzs);
		if (result != ISC_R_SUCCESS) {
			break;
		}

		isc_ht_iter_currentkey(iter, &key, &keysize);
		region.base = key;
		region.length = (unsigned int)keysize;
		dns_name_fromregion(name, &region);

		LOCK(&rpz->rpzs->maint_lock);
		rpz_del(rpz, name);
		UNLOCK(&rpz->rpzs->maint_lock);
	}
	INSIST(result != ISC_R_SUCCESS);
	if (result == ISC_R_NOMORE) {
		result = ISC_R_SUCCESS;
	}

	isc_ht_iter_destroy(&iter);

	return result;
}

static isc_result_t
dns__rpz_shuttingdown(dns_rpz_zones_t *rpzs) {
	bool shuttingdown = false;

	LOCK(&rpzs->maint_lock);
	shuttingdown = rpzs->shuttingdown;
	UNLOCK(&rpzs->maint_lock);

	if (shuttingdown) {
		return ISC_R_SHUTTINGDOWN;
	}

	return ISC_R_SUCCESS;
}

static void
update_rpz_cb(void *data) {
	dns_rpz_zone_t *rpz = (dns_rpz_zone_t *)data;
	isc_result_t result = ISC_R_SUCCESS;
	isc_ht_t *newnodes = NULL;

	REQUIRE(rpz->nodes != NULL);

	result = dns__rpz_shuttingdown(rpz->rpzs);
	if (result != ISC_R_SUCCESS) {
		goto shuttingdown;
	}

	isc_ht_init(&newnodes, rpz->rpzs->mctx, 1, ISC_HT_CASE_SENSITIVE);

	result = update_nodes(rpz, newnodes);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	result = cleanup_nodes(rpz);
	if (result != ISC_R_SUCCESS) {
		goto cleanup;
	}

	/* Finalize the update */
	ISC_SWAP(rpz->nodes, newnodes);

cleanup:
	isc_ht_destroy(&newnodes);

shuttingdown:
	rpz->updateresult = result;
}

static void
dns__rpz_timer_cb(void *arg) {
	char domain[DNS_NAME_FORMATSIZE];
	dns_rpz_zone_t *rpz = (dns_rpz_zone_t *)arg;

	REQUIRE(DNS_RPZ_ZONE_VALID(rpz));
	REQUIRE(DNS_DB_VALID(rpz->db));
	REQUIRE(rpz->updb == NULL);
	REQUIRE(rpz->updbversion == NULL);

	LOCK(&rpz->rpzs->maint_lock);

	if (rpz->rpzs->shuttingdown) {
		goto unlock;
	}

	rpz->updatepending = false;
	rpz->updaterunning = true;
	rpz->updateresult = ISC_R_UNSET;

	dns_db_attach(rpz->db, &rpz->updb);
	INSIST(rpz->dbversion != NULL);
	rpz->updbversion = rpz->dbversion;
	rpz->dbversion = NULL;

	dns_name_format(&rpz->origin, domain, DNS_NAME_FORMATSIZE);
	isc_log_write(DNS_LOGCATEGORY_GENERAL, DNS_LOGMODULE_RPZ, ISC_LOG_INFO,
		      "rpz: %s: reload start", domain);

	dns_rpz_zones_ref(rpz->rpzs);
	isc_work_enqueue(rpz->loop, update_rpz_cb, update_rpz_done_cb, rpz);

	isc_timer_destroy(&rpz->updatetimer);
	rpz->loop = NULL;

	rpz->lastupdated = isc_time_now();
unlock:
	UNLOCK(&rpz->rpzs->maint_lock);
}

/*
 * Free the radix tree of a response policy database.
 */
static void
cidr_free(dns_rpz_zones_t *rpzs) {
	dns_rpz_cidr_node_t *cur = NULL, *child = NULL, *parent = NULL;

	cur = rpzs->cidr;
	while (cur != NULL) {
		/* Depth first. */
		child = cur->child[0];
		if (child != NULL) {
			cur = child;
			continue;
		}
		child = cur->child[1];
		if (child != NULL) {
			cur = child;
			continue;
		}

		/* Delete this leaf and go up. */
		parent = cur->parent;
		if (parent == NULL) {
			rpzs->cidr = NULL;
		} else {
			parent->child[parent->child[1] == cur] = NULL;
		}
		isc_mem_put(rpzs->mctx, cur, sizeof(*cur));
		cur = parent;
	}
}

static void
dns__rpz_shutdown(dns_rpz_zone_t *rpz) {
	/* maint_lock must be locked */
	if (rpz->updatetimer != NULL) {
		/* Don't wait for timer to trigger for shutdown */
		INSIST(rpz->loop != NULL);

		dns_rpz_zones_ref(rpz->rpzs);
		isc_async_run(rpz->loop, dns__rpz_timer_stop, rpz);
	}
}

static void
dns_rpz_zone_destroy(dns_rpz_zone_t **rpzp) {
	dns_rpz_zone_t *rpz = NULL;
	dns_rpz_zones_t *rpzs;

	rpz = *rpzp;
	*rpzp = NULL;

	rpzs = rpz->rpzs;
	rpz->rpzs = NULL;

	if (dns_name_dynamic(&rpz->origin)) {
		dns_name_free(&rpz->origin, rpzs->mctx);
	}
	if (dns_name_dynamic(&rpz->client_ip)) {
		dns_name_free(&rpz->client_ip, rpzs->mctx);
	}
	if (dns_name_dynamic(&rpz->ip)) {
		dns_name_free(&rpz->ip, rpzs->mctx);
	}
	if (dns_name_dynamic(&rpz->nsdname)) {
		dns_name_free(&rpz->nsdname, rpzs->mctx);
	}
	if (dns_name_dynamic(&rpz->nsip)) {
		dns_name_free(&rpz->nsip, rpzs->mctx);
	}
	if (dns_name_dynamic(&rpz->passthru)) {
		dns_name_free(&rpz->passthru, rpzs->mctx);
	}
	if (dns_name_dynamic(&rpz->drop)) {
		dns_name_free(&rpz->drop, rpzs->mctx);
	}
	if (dns_name_dynamic(&rpz->tcp_only)) {
		dns_name_free(&rpz->tcp_only, rpzs->mctx);
	}
	if (dns_name_dynamic(&rpz->cname)) {
		dns_name_free(&rpz->cname, rpzs->mctx);
	}
	if (rpz->db != NULL) {
		if (rpz->dbversion != NULL) {
			dns_db_closeversion(rpz->db, &rpz->dbversion, false);
		}
		dns_db_updatenotify_unregister(rpz->db,
					       dns_rpz_dbupdate_callback, rpz);
		dns_db_detach(&rpz->db);
	}
	INSIST(!rpz->updaterunning);

	isc_ht_destroy(&rpz->nodes);

	isc_mem_put(rpzs->mctx, rpz, sizeof(*rpz));
}

static void
dns__rpz_zones_destroy(dns_rpz_zones_t *rpzs) {
	REQUIRE(rpzs->shuttingdown);

	for (dns_rpz_num_t rpz_num = 0; rpz_num < DNS_RPZ_MAX_ZONES; ++rpz_num)
	{
		if (rpzs->zones[rpz_num] == NULL) {
			continue;
		}

		dns_rpz_zone_destroy(&rpzs->zones[rpz_num]);
	}

	cidr_free(rpzs);
	if (rpzs->table != NULL) {
		dns_qpmulti_destroy(&rpzs->table);
	}

	isc_mutex_destroy(&rpzs->maint_lock);
	isc_rwlock_destroy(&rpzs->search_lock);
	isc_mem_putanddetach(&rpzs->mctx, rpzs, sizeof(*rpzs));
}

void
dns_rpz_zones_shutdown(dns_rpz_zones_t *rpzs) {
	REQUIRE(DNS_RPZ_ZONES_VALID(rpzs));
	/*
	 * Forget the last of the view's rpz machinery when shutting down.
	 */

	LOCK(&rpzs->maint_lock);
	if (rpzs->shuttingdown) {
		UNLOCK(&rpzs->maint_lock);
		return;
	}

	rpzs->shuttingdown = true;

	for (dns_rpz_num_t rpz_num = 0; rpz_num < DNS_RPZ_MAX_ZONES; ++rpz_num)
	{
		if (rpzs->zones[rpz_num] == NULL) {
			continue;
		}

		dns__rpz_shutdown(rpzs->zones[rpz_num]);
	}
	UNLOCK(&rpzs->maint_lock);
}

#ifdef DNS_RPZ_TRACE
ISC_REFCOUNT_TRACE_IMPL(dns_rpz_zones, dns__rpz_zones_destroy);
#else
ISC_REFCOUNT_IMPL(dns_rpz_zones, dns__rpz_zones_destroy);
#endif

/*
 * Add an IP address to the radix tree or a name to the summary database.
 */
static isc_result_t
rpz_add(dns_rpz_zone_t *rpz, const dns_name_t *src_name) {
	dns_rpz_type_t rpz_type;
	isc_result_t result = ISC_R_FAILURE;
	dns_rpz_zones_t *rpzs = NULL;
	dns_rpz_num_t rpz_num;

	REQUIRE(rpz != NULL);

	rpzs = rpz->rpzs;
	rpz_num = rpz->num;

	REQUIRE(rpzs != NULL && rpz_num < rpzs->p.num_zones);

	rpz_type = type_from_name(rpzs, rpz, src_name);
	switch (rpz_type) {
	case DNS_RPZ_TYPE_QNAME:
	case DNS_RPZ_TYPE_NSDNAME:
		result = add_name(rpz, rpz_type, src_name);
		break;
	case DNS_RPZ_TYPE_CLIENT_IP:
	case DNS_RPZ_TYPE_IP:
	case DNS_RPZ_TYPE_NSIP:
		result = add_cidr(rpz, rpz_type, src_name);
		break;
	case DNS_RPZ_TYPE_BAD:
		break;
	}

	return result;
}

/*
 * Remove an IP address from the radix tree.
 */
static void
del_cidr(dns_rpz_zone_t *rpz, dns_rpz_type_t rpz_type,
	 const dns_name_t *src_name) {
	isc_result_t result;
	dns_rpz_cidr_key_t tgt_ip;
	dns_rpz_prefix_t tgt_prefix;
	dns_rpz_addr_zbits_t tgt_set;
	dns_rpz_cidr_node_t *tgt = NULL, *parent = NULL, *child = NULL;

	/*
	 * Do not worry about invalid rpz IP address names.  If we
	 * are here, then something relevant was added and so was
	 * valid.
	 */
	result = name2ipkey(DNS_RPZ_DEBUG_QUIET, rpz, rpz_type, src_name,
			    &tgt_ip, &tgt_prefix, &tgt_set);
	if (result != ISC_R_SUCCESS) {
		return;
	}

	RWLOCK(&rpz->rpzs->search_lock, isc_rwlocktype_write);
	result = search(rpz->rpzs, &tgt_ip, tgt_prefix, &tgt_set, false, &tgt);
	if (result != ISC_R_SUCCESS) {
		goto done;
	}

	/*
	 * Mark the node and its parents to reflect the deleted IP address.
	 */
	tgt_set.client_ip &= tgt->set.client_ip;
	tgt_set.ip &= tgt->set.ip;
	tgt_set.nsip &= tgt->set.nsip;
	tgt->set.client_ip &= ~tgt_set.client_ip;
	tgt->set.ip &= ~tgt_set.ip;
	tgt->set.nsip &= ~tgt_set.nsip;
	set_sum_pair(tgt);

	adj_trigger_cnt(rpz, rpz_type, &tgt_ip, tgt_prefix, false);

	/*
	 * We might need to delete 2 nodes.
	 */
	do {
		/*
		 * The node is now useless if it has no data of its own
		 * and 0 or 1 children.  We are finished if it is not
		 * useless.
		 */
		if ((child = tgt->child[0]) != NULL) {
			if (tgt->child[1] != NULL) {
				break;
			}
		} else {
			child = tgt->child[1];
		}
		if (tgt->set.client_ip != 0 || tgt->set.ip != 0 ||
		    tgt->set.nsip != 0)
		{
			break;
		}

		/*
		 * Replace the pointer to this node in the parent with
		 * the remaining child or NULL.
		 */
		parent = tgt->parent;
		if (parent == NULL) {
			rpz->rpzs->cidr = child;
		} else {
			parent->child[parent->child[1] == tgt] = child;
		}

		/*
		 * If the child exists fix up its parent pointer.
		 */
		if (child != NULL) {
			child->parent = parent;
		}
		isc_mem_put(rpz->rpzs->mctx, tgt, sizeof(*tgt));

		tgt = parent;
	} while (tgt != NULL);

done:
	RWUNLOCK(&rpz->rpzs->search_lock, isc_rwlocktype_write);
}

static void
del_name(dns_rpz_zone_t *rpz, dns_rpz_type_t rpz_type,
	 const dns_name_t *src_name) {
	isc_result_t result;
	char namebuf[DNS_NAME_FORMATSIZE];
	dns_fixedname_t trig_namef;
	dns_name_t *trig_name = NULL;
	dns_rpz_zones_t *rpzs = rpz->rpzs;
	nmdata_t *data = NULL;
	nmdata_t del_data;
	dns_qp_t *qp = NULL;
	bool exists;

	dns_qpmulti_write(rpzs->table, &qp);

	/*
	 * We need a summary database of names even with 1 policy zone,
	 * because wildcard triggers are handled differently.
	 */

	trig_name = dns_fixedname_initname(&trig_namef);
	name2data(rpz, rpz_type, src_name, trig_name, &del_data);

	result = dns_qp_getname(qp, trig_name, DNS_DBNAMESPACE_NORMAL,
				(void **)&data, NULL);
	if (result != ISC_R_SUCCESS) {
		return;
	}

	INSIST(data != NULL);

	del_data.set.qname &= data->set.qname;
	del_data.set.ns &= data->set.ns;
	del_data.wild.qname &= data->wild.qname;
	del_data.wild.ns &= data->wild.ns;

	exists = (del_data.set.qname != 0 || del_data.set.ns != 0 ||
		  del_data.wild.qname != 0 || del_data.wild.ns != 0);

	data->set.qname &= ~del_data.set.qname;
	data->set.ns &= ~del_data.set.ns;
	data->wild.qname &= ~del_data.wild.qname;
	data->wild.ns &= ~del_data.wild.ns;

	if (data->set.qname == 0 && data->set.ns == 0 &&
	    data->wild.qname == 0 && data->wild.ns == 0)
	{
		result = dns_qp_deletename(qp, trig_name,
					   DNS_DBNAMESPACE_NORMAL, NULL, NULL);
		if (result != ISC_R_SUCCESS) {
			/*
			 * bin/tests/system/rpz/tests.sh looks for
			 * "rpz.*failed".
			 */
			dns_name_format(src_name, namebuf, sizeof(namebuf));
			isc_log_write(DNS_LOGCATEGORY_RPZ, DNS_LOGMODULE_RPZ,
				      DNS_RPZ_ERROR_LEVEL,
				      "rpz del_name(%s) node delete "
				      "failed: %s",
				      namebuf, isc_result_totext(result));
		}
	}

	if (exists) {
		RWLOCK(&rpz->rpzs->search_lock, isc_rwlocktype_write);
		adj_trigger_cnt(rpz, rpz_type, NULL, 0, false);
		RWUNLOCK(&rpz->rpzs->search_lock, isc_rwlocktype_write);
	}

	dns_qp_compact(qp, DNS_QPGC_MAYBE);
	dns_qpmulti_commit(rpzs->table, &qp);
}

/*
 * Remove an IP address from the radix tree or a name from the summary database.
 */
static void
rpz_del(dns_rpz_zone_t *rpz, const dns_name_t *src_name) {
	dns_rpz_type_t rpz_type;
	dns_rpz_zones_t *rpzs = NULL;
	dns_rpz_num_t rpz_num;

	REQUIRE(rpz != NULL);

	rpzs = rpz->rpzs;
	rpz_num = rpz->num;

	REQUIRE(rpzs != NULL && rpz_num < rpzs->p.num_zones);

	rpz_type = type_from_name(rpzs, rpz, src_name);
	switch (rpz_type) {
	case DNS_RPZ_TYPE_QNAME:
	case DNS_RPZ_TYPE_NSDNAME:
		del_name(rpz, rpz_type, src_name);
		break;
	case DNS_RPZ_TYPE_CLIENT_IP:
	case DNS_RPZ_TYPE_IP:
	case DNS_RPZ_TYPE_NSIP:
		del_cidr(rpz, rpz_type, src_name);
		break;
	case DNS_RPZ_TYPE_BAD:
		break;
	}
}

/*
 * Search the summary radix tree to get a relative owner name in a
 * policy zone relevant to a triggering IP address.
 *	rpz_type and zbits limit the search for IP address netaddr
 *	return the policy zone's number or DNS_RPZ_INVALID_NUM
 *	ip_name is the relative owner name found and
 *	*prefixp is its prefix length.
 */
dns_rpz_num_t
dns_rpz_find_ip(dns_rpz_zones_t *rpzs, dns_rpz_type_t rpz_type,
		dns_rpz_zbits_t zbits, const isc_netaddr_t *netaddr,
		dns_name_t *ip_name, dns_rpz_prefix_t *prefixp) {
	dns_rpz_cidr_key_t tgt_ip;
	dns_rpz_addr_zbits_t tgt_set;
	dns_rpz_cidr_node_t *found = NULL;
	isc_result_t result;
	dns_rpz_num_t rpz_num = 0;
	dns_rpz_have_t have;
	int i;

	RWLOCK(&rpzs->search_lock, isc_rwlocktype_read);
	have = rpzs->have;
	RWUNLOCK(&rpzs->search_lock, isc_rwlocktype_read);

	/*
	 * Convert IP address to CIDR tree key.
	 */
	if (netaddr->family == AF_INET) {
		tgt_ip.w[0] = 0;
		tgt_ip.w[1] = 0;
		tgt_ip.w[2] = ADDR_V4MAPPED;
		tgt_ip.w[3] = ntohl(netaddr->type.in.s_addr);
		switch (rpz_type) {
		case DNS_RPZ_TYPE_CLIENT_IP:
			zbits &= have.client_ipv4;
			break;
		case DNS_RPZ_TYPE_IP:
			zbits &= have.ipv4;
			break;
		case DNS_RPZ_TYPE_NSIP:
			zbits &= have.nsipv4;
			break;
		default:
			UNREACHABLE();
			break;
		}
	} else if (netaddr->family == AF_INET6) {
		dns_rpz_cidr_key_t src_ip6;

		/*
		 * Given the int aligned struct in_addr member of netaddr->type
		 * one could cast netaddr->type.in6 to dns_rpz_cidr_key_t *,
		 * but some people object.
		 */
		memmove(src_ip6.w, &netaddr->type.in6, sizeof(src_ip6.w));
		for (i = 0; i < 4; i++) {
			tgt_ip.w[i] = ntohl(src_ip6.w[i]);
		}
		switch (rpz_type) {
		case DNS_RPZ_TYPE_CLIENT_IP:
			zbits &= have.client_ipv6;
			break;
		case DNS_RPZ_TYPE_IP:
			zbits &= have.ipv6;
			break;
		case DNS_RPZ_TYPE_NSIP:
			zbits &= have.nsipv6;
			break;
		default:
			UNREACHABLE();
			break;
		}
	} else {
		return DNS_RPZ_INVALID_NUM;
	}

	if (zbits == 0) {
		return DNS_RPZ_INVALID_NUM;
	}
	make_addr_set(&tgt_set, zbits, rpz_type);

	RWLOCK(&rpzs->search_lock, isc_rwlocktype_read);
	result = search(rpzs, &tgt_ip, 128, &tgt_set, false, &found);
	if (result == ISC_R_NOTFOUND) {
		/*
		 * There are no eligible zones for this IP address.
		 */
		RWUNLOCK(&rpzs->search_lock, isc_rwlocktype_read);
		return DNS_RPZ_INVALID_NUM;
	}

	/*
	 * Construct the trigger name for the longest matching trigger
	 * in the first eligible zone with a match.
	 */
	*prefixp = found->prefix;
	switch (rpz_type) {
	case DNS_RPZ_TYPE_CLIENT_IP:
		rpz_num = zbit_to_num(found->set.client_ip & tgt_set.client_ip);
		break;
	case DNS_RPZ_TYPE_IP:
		rpz_num = zbit_to_num(found->set.ip & tgt_set.ip);
		break;
	case DNS_RPZ_TYPE_NSIP:
		rpz_num = zbit_to_num(found->set.nsip & tgt_set.nsip);
		break;
	default:
		UNREACHABLE();
	}
	result = ip2name(&found->ip, found->prefix, dns_rootname, ip_name);
	RWUNLOCK(&rpzs->search_lock, isc_rwlocktype_read);
	if (result != ISC_R_SUCCESS) {
		/*
		 * bin/tests/system/rpz/tests.sh looks for "rpz.*failed".
		 */
		isc_log_write(DNS_LOGCATEGORY_RPZ, DNS_LOGMODULE_RPZ,
			      DNS_RPZ_ERROR_LEVEL, "rpz ip2name() failed: %s",
			      isc_result_totext(result));
		return DNS_RPZ_INVALID_NUM;
	}
	return rpz_num;
}

/*
 * Search the summary radix tree for policy zones with triggers matching
 * a name.
 */
dns_rpz_zbits_t
dns_rpz_find_name(dns_rpz_zones_t *rpzs, dns_rpz_type_t rpz_type,
		  dns_rpz_zbits_t zbits, dns_name_t *trig_name) {
	isc_result_t result;
	char namebuf[DNS_NAME_FORMATSIZE];
	nmdata_t *data = NULL;
	dns_rpz_zbits_t found_zbits = 0;
	dns_qpchain_t chain;
	dns_qpread_t qpr;
	int i;

	if (zbits == 0) {
		return 0;
	}

	dns_qpmulti_query(rpzs->table, &qpr);
	dns_qpchain_init(&qpr, &chain);

	result = dns_qp_lookup(&qpr, trig_name, DNS_DBNAMESPACE_NORMAL, NULL,
			       NULL, &chain, (void **)&data, NULL);
	switch (result) {
	case ISC_R_SUCCESS:
		INSIST(data != NULL);
		if (rpz_type == DNS_RPZ_TYPE_QNAME) {
			found_zbits = data->set.qname;
		} else {
			found_zbits = data->set.ns;
		}
		FALLTHROUGH;

	case DNS_R_PARTIALMATCH:
		i = dns_qpchain_length(&chain);
		while (i-- > 0) {
			dns_qpchain_node(&chain, i, NULL, (void **)&data, NULL);
			INSIST(data != NULL);
			if (rpz_type == DNS_RPZ_TYPE_QNAME) {
				found_zbits |= data->wild.qname;
			} else {
				found_zbits |= data->wild.ns;
			}
		}
		break;

	case ISC_R_NOTFOUND:
		break;

	default:
		/*
		 * bin/tests/system/rpz/tests.sh looks for "rpz.*failed".
		 */
		dns_name_format(trig_name, namebuf, sizeof(namebuf));
		isc_log_write(DNS_LOGCATEGORY_RPZ, DNS_LOGMODULE_RPZ,
			      DNS_RPZ_ERROR_LEVEL,
			      "dns_rpz_find_name(%s) failed: %s", namebuf,
			      isc_result_totext(result));
		break;
	}

	dns_qpread_destroy(rpzs->table, &qpr);
	return zbits & found_zbits;
}

/*
 * Translate CNAME rdata to a QNAME response policy action.
 */
dns_rpz_policy_t
dns_rpz_decode_cname(dns_rpz_zone_t *rpz, dns_rdataset_t *rdataset,
		     dns_name_t *selfname) {
	dns_rdata_t rdata = DNS_RDATA_INIT;
	dns_rdata_cname_t cname;
	isc_result_t result;

	result = dns_rdataset_first(rdataset);
	INSIST(result == ISC_R_SUCCESS);
	dns_rdataset_current(rdataset, &rdata);
	result = dns_rdata_tostruct(&rdata, &cname, NULL);
	INSIST(result == ISC_R_SUCCESS);
	dns_rdata_reset(&rdata);

	/*
	 * CNAME . means NXDOMAIN
	 */
	if (dns_name_equal(&cname.cname, dns_rootname)) {
		return DNS_RPZ_POLICY_NXDOMAIN;
	}

	if (dns_name_iswildcard(&cname.cname)) {
		/*
		 * CNAME *. means NODATA
		 */
		if (dns_name_countlabels(&cname.cname) == 2) {
			return DNS_RPZ_POLICY_NODATA;
		}

		/*
		 * A qname of www.evil.com and a policy of
		 *	*.evil.com    CNAME   *.garden.net
		 * gives a result of
		 *	evil.com    CNAME   evil.com.garden.net
		 */
		if (dns_name_countlabels(&cname.cname) > 2) {
			return DNS_RPZ_POLICY_WILDCNAME;
		}
	}

	/*
	 * CNAME rpz-tcp-only. means "send truncated UDP responses."
	 */
	if (dns_name_equal(&cname.cname, &rpz->tcp_only)) {
		return DNS_RPZ_POLICY_TCP_ONLY;
	}

	/*
	 * CNAME rpz-drop. means "do not respond."
	 */
	if (dns_name_equal(&cname.cname, &rpz->drop)) {
		return DNS_RPZ_POLICY_DROP;
	}

	/*
	 * CNAME rpz-passthru. means "do not rewrite."
	 */
	if (dns_name_equal(&cname.cname, &rpz->passthru)) {
		return DNS_RPZ_POLICY_PASSTHRU;
	}

	/*
	 * 128.1.0.127.rpz-ip CNAME  128.1.0.0.127. is obsolete PASSTHRU
	 */
	if (selfname != NULL && dns_name_equal(&cname.cname, selfname)) {
		return DNS_RPZ_POLICY_PASSTHRU;
	}

	/*
	 * Any other rdata gives a response consisting of the rdata.
	 */
	return DNS_RPZ_POLICY_RECORD;
}

static void
destroy_nmdata(nmdata_t *data) {
	dns_name_free(&data->name, data->mctx);
	isc_mem_putanddetach(&data->mctx, data, sizeof(nmdata_t));
}

#ifdef DNS_RPZ_TRACE
ISC_REFCOUNT_TRACE_IMPL(nmdata, destroy_nmdata);
#else
ISC_REFCOUNT_IMPL(nmdata, destroy_nmdata);
#endif

static void
qp_attach(void *uctx ISC_ATTR_UNUSED, void *pval,
	  uint32_t ival ISC_ATTR_UNUSED) {
	nmdata_t *data = pval;
	nmdata_ref(data);
}

static void
qp_detach(void *uctx ISC_ATTR_UNUSED, void *pval,
	  uint32_t ival ISC_ATTR_UNUSED) {
	nmdata_t *data = pval;
	nmdata_detach(&data);
}

static size_t
qp_makekey(dns_qpkey_t key, void *uctx ISC_ATTR_UNUSED, void *pval,
	   uint32_t ival ISC_ATTR_UNUSED) {
	nmdata_t *data = pval;
	return dns_qpkey_fromname(key, &data->name, DNS_DBNAMESPACE_NORMAL);
}

static void
qp_triename(void *uctx, char *buf, size_t size) {
	dns_view_t *view = uctx;
	snprintf(buf, size, "view %s RPZs", view->name);
}
