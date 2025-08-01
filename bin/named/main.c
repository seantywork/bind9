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

#include <ctype.h>
#include <inttypes.h>
#include <locale.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_DNSTAP
#include <protobuf-c/protobuf-c.h>
#endif

#include <isc/attributes.h>
#include <isc/backtrace.h>
#include <isc/commandline.h>
#include <isc/crypto.h>
#include <isc/dir.h>
#include <isc/file.h>
#include <isc/hash.h>
#include <isc/httpd.h>
#include <isc/lib.h>
#include <isc/managers.h>
#include <isc/netmgr.h>
#include <isc/os.h>
#include <isc/result.h>
#include <isc/signal.h>
#include <isc/stdio.h>
#include <isc/string.h>
#include <isc/timer.h>
#include <isc/util.h>
#include <isc/uv.h>
#include <isc/xml.h>

#include <dns/dispatch.h>
#include <dns/dyndb.h>
#include <dns/lib.h>
#include <dns/name.h>
#include <dns/resolver.h>
#include <dns/view.h>

#include <dlz/dlz_dlopen_driver.h>

#ifdef HAVE_GPERFTOOLS_PROFILER
#include <gperftools/profiler.h>
#endif /* ifdef HAVE_GPERFTOOLS_PROFILER */

#ifdef HAVE_JSON_C
#include <json_c_version.h>
#endif /* HAVE_JSON_C */

#ifdef HAVE_GEOIP2
#include <maxminddb.h>
#endif /* ifdef HAVE_GEOIP2 */

/*
 * Defining NAMED_MAIN provides storage declarations (rather than extern)
 * for variables in named/globals.h.
 */
#define NAMED_MAIN 1

#include <ns/interfacemgr.h>

#include <named/builtin.h>
#include <named/config.h>
#include <named/control.h>
#include <named/fuzz.h>
#include <named/globals.h> /* Explicit, though named/log.h includes it. */
#include <named/log.h>
#include <named/main.h>
#include <named/os.h>
#include <named/server.h>
#ifdef HAVE_LIBSCF
#include <named/smf_globals.h>
#endif /* ifdef HAVE_LIBSCF */

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/opensslv.h>
#ifdef HAVE_LIBXML2
#include <libxml/parser.h>
#include <libxml/xmlversion.h>
#endif /* ifdef HAVE_LIBXML2 */
#ifdef HAVE_ZLIB
#include <zlib.h>
#endif /* ifdef HAVE_ZLIB */
#ifdef HAVE_LIBNGHTTP2
#include <nghttp2/nghttp2.h>
#endif

/* On DragonFly BSD the header does not provide jemalloc API */
#if defined(HAVE_MALLOC_NP_H) && !defined(__DragonFly__)
#include <malloc_np.h>
#include <sys/malloc.h> /* For M_VERSION */
#elif defined(HAVE_JEMALLOC)
#include <jemalloc/jemalloc.h>
#endif

/*
 * Include header files for database drivers here.
 */
/* #include "xxdb.h" */

extern unsigned int dns_zone_mkey_hour;
extern unsigned int dns_zone_mkey_day;
extern unsigned int dns_zone_mkey_month;

static bool want_stats = false;
static char absolute_conffile[PATH_MAX];
static char saved_command_line[4096] = { 0 };
static char ellipsis[5] = { 0 };
static char version[512];
static int maxudp = 0;

/*
 * -T options:
 */
static bool cookiealwaysvalid = false;
static bool dropedns = false;
static bool ednsformerr = false;
static bool ednsnotimp = false;
static bool ednsrefused = false;
static bool fixedlocal = false;
static bool noaa = false;
static bool noedns = false;
static bool nonearest = false;
static bool nosoa = false;
static bool notcp = false;
static bool sigvalinsecs = false;
static bool transferinsecs = false;
static bool transferslowly = false;
static bool transferstuck = false;

/*
 * -4 and -6
 */
static bool disable6 = false;
static bool disable4 = false;

void
named_main_earlywarning(const char *format, ...) {
	va_list args;

	va_start(args, format);
	isc_log_vwrite(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		       ISC_LOG_WARNING, format, args);
	va_end(args);
}

void
named_main_earlyfatal(const char *format, ...) {
	va_list args;

	va_start(args, format);
	isc_log_vwrite(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		       ISC_LOG_CRITICAL, format, args);
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_CRITICAL, "exiting (due to early fatal error)");
	va_end(args);

	_exit(EXIT_FAILURE);
}

ISC_NORETURN static void
assertion_failed(const char *file, int line, isc_assertiontype_t type,
		 const char *cond);

static void
assertion_failed(const char *file, int line, isc_assertiontype_t type,
		 const char *cond) {
	/*
	 * Handle assertion failures.
	 */

	/*
	 * Reset the assertion callback in case it is the log
	 * routines causing the assertion.
	 */
	isc_assertion_setcallback(NULL);

	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_CRITICAL, "%s:%d: %s(%s) failed", file, line,
		      isc_assertion_typetotext(type), cond);
	isc_backtrace_log(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
			  ISC_LOG_CRITICAL);
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_CRITICAL, "exiting (due to assertion failure)");

	if (named_g_coreok) {
		abort();
	}
	_exit(EXIT_FAILURE);
}

ISC_NORETURN static void
library_fatal_error(const char *file, int line, const char *func,
		    const char *format, va_list args) ISC_FORMAT_PRINTF(3, 0);

static void
library_fatal_error(const char *file, int line, const char *func,
		    const char *format, va_list args) {
	/*
	 * Handle isc_error_fatal() calls from our libraries.
	 */

	/*
	 * Reset the error callback in case it is the log
	 * routines causing the assertion.
	 */
	isc_error_setfatal(NULL);

	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_CRITICAL, "%s:%d:%s(): fatal error: ", file, line,
		      func);
	isc_log_vwrite(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		       ISC_LOG_CRITICAL, format, args);
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_CRITICAL,
		      "exiting (due to fatal error in library)");

	if (named_g_coreok) {
		abort();
	}
	_exit(EXIT_FAILURE);
}

static void
library_unexpected_error(const char *file, int line, const char *func,
			 const char *format, va_list args)
	ISC_FORMAT_PRINTF(3, 0);

static void
library_unexpected_error(const char *file, int line, const char *func,
			 const char *format, va_list args) {
	/*
	 * Handle isc_error_unexpected() calls from our libraries.
	 */

	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_ERROR, "%s:%d:%s(): unexpected error: ", file,
		      line, func);
	isc_log_vwrite(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		       ISC_LOG_ERROR, format, args);
}

static void
usage(void) {
	fprintf(stderr, "usage: named [-4|-6] [-c conffile] [-d debuglevel] "
			"[-D comment]\n"
			"             [-f|-g] [-L logfile] [-n number_of_cpus] "
			"[-p port] [-s]\n"
			"             [-S sockets] [-t chrootdir] [-u "
			"username] [-U listeners]\n"
			"             [-m "
			"{usage|trace|record}]\n"
			"             [-M fill|nofill]\n"
			"usage: named [-v|-V|-C]\n");
}

static void
save_command_line(int argc, char *argv[]) {
	int i;
	char *dst = saved_command_line;
	char *eob = saved_command_line + sizeof(saved_command_line) - 1;
	char *rollback = dst;

	for (i = 1; i < argc && dst < eob; i++) {
		char *src = argv[i];
		bool quoted = false;

		rollback = dst;
		*dst++ = ' ';

		while (*src != '\0' && dst < eob) {
			if (isalnum(*(unsigned char *)src) || *src == ',' ||
			    *src == '-' || *src == '_' || *src == '.' ||
			    *src == '/')
			{
				*dst++ = *src++;
			} else if (isprint(*(unsigned char *)src)) {
				if (dst + 2 >= eob) {
					goto add_ellipsis;
				}
				*dst++ = '\\';
				*dst++ = *src++;
			} else {
				/*
				 * Control character found in the input,
				 * quote the whole arg and restart
				 */
				if (!quoted) {
					dst = rollback;
					src = argv[i];

					if (dst + 3 >= eob) {
						goto add_ellipsis;
					}

					*dst++ = ' ';
					*dst++ = '$';
					*dst++ = '\'';

					quoted = true;
					continue;
				} else {
					char tmp[5];
					int c = snprintf(tmp, sizeof(tmp),
							 "\\%03o", *src++);
					if (dst + c >= eob) {
						goto add_ellipsis;
					}
					memmove(dst, tmp, c);
					dst += c;
				}
			}
		}
		if (quoted) {
			if (dst == eob) {
				goto add_ellipsis;
			}
			*dst++ = '\'';
		}
	}

	if (dst < eob) {
		return;
	}
add_ellipsis:
	dst = rollback;
	*dst = '\0';
	strlcpy(ellipsis, " ...", sizeof(ellipsis));
}

static int
parse_int(char *arg, const char *desc) {
	char *endp;
	int tmp;
	long int ltmp;

	ltmp = strtol(arg, &endp, 10);
	tmp = (int)ltmp;
	if (*endp != '\0') {
		named_main_earlyfatal("%s '%s' must be numeric", desc, arg);
	}
	if (tmp < 0 || tmp != ltmp) {
		named_main_earlyfatal("%s '%s' out of range", desc, arg);
	}
	return tmp;
}

static struct flag_def {
	const char *name;
	unsigned int value;
	bool negate;
} mem_debug_flags[] = { { "none", 0, false },
			{ "trace", ISC_MEM_DEBUGTRACE, false },
			{ "record", ISC_MEM_DEBUGRECORD, false },
			{ "usage", ISC_MEM_DEBUGUSAGE, false },
			{ NULL, 0, false } },
  mem_context_flags[] = { { "fill", ISC_MEMFLAG_FILL, false },
			  { "nofill", ISC_MEMFLAG_FILL, true },
			  { NULL, 0, false } };

static void
set_flags(const char *arg, struct flag_def *defs, unsigned int *ret) {
	bool clear = false;

	for (;;) {
		const struct flag_def *def;
		const char *end = strchr(arg, ',');
		int arglen;
		if (end == NULL) {
			end = arg + strlen(arg);
		}
		arglen = (int)(end - arg);
		for (def = defs; def->name != NULL; def++) {
			if (arglen == (int)strlen(def->name) &&
			    memcmp(arg, def->name, arglen) == 0)
			{
				if (def->value == 0) {
					clear = true;
				}
				if (def->negate) {
					*ret &= ~(def->value);
				} else {
					*ret |= def->value;
				}
				goto found;
			}
		}
		named_main_earlyfatal("unrecognized flag '%.*s'", arglen, arg);
	found:
		if (clear || (*end == '\0')) {
			break;
		}
		arg = end + 1;
	}

	if (clear) {
		*ret = 0;
	}
}

static void
list_dnssec_algorithms(isc_buffer_t *b) {
	for (dst_algorithm_t i = DST_ALG_UNKNOWN; i < DST_MAX_ALGS; i++) {
		if (i == DST_ALG_GSSAPI ||
		    (i >= DST_ALG_HMAC_FIRST && i <= DST_ALG_HMAC_LAST))
		{
			continue;
		}
		if (dst_algorithm_supported(i)) {
			isc_buffer_putstr(b, " ");
			dst_algorithm_totext(i, b);
		}
	}
}

static void
list_ds_algorithms(isc_buffer_t *b) {
	for (size_t i = 0; i < 256; i++) {
		if (dst_ds_digest_supported(i)) {
			isc_buffer_putstr(b, " ");
			(void)dns_dsdigest_totext(i, b);
		}
	}
}

static void
list_hmac_algorithms(isc_buffer_t *b) {
	isc_buffer_t sb = *b;
	for (dst_algorithm_t i = DST_ALG_HMAC_FIRST; i <= DST_ALG_HMAC_LAST;
	     i++)
	{
		if (i == DST_ALG_GSSAPI) {
			continue;
		}
		if (dst_algorithm_supported(i)) {
			isc_buffer_putstr(b, " ");
			isc_buffer_putstr(b, dst_hmac_algorithm_totext(i));
		}
	}
	for (unsigned char *s = isc_buffer_used(&sb); s != isc_buffer_used(b);
	     s++)
	{
		*s = toupper(*s);
	}
}

static void
logit(isc_buffer_t *b) {
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "%.*s", (int)isc_buffer_usedlength(b),
		      (char *)isc_buffer_base(b));
}

static void
printit(isc_buffer_t *b) {
	printf("%.*s\n", (int)isc_buffer_usedlength(b),
	       (char *)isc_buffer_base(b));
}

static void
format_supported_algorithms(void (*emit)(isc_buffer_t *b)) {
	isc_buffer_t b;
	char buf[512];

	isc_buffer_init(&b, buf, sizeof(buf));
	isc_buffer_putstr(&b, "DNSSEC algorithms:");
	list_dnssec_algorithms(&b);
	(*emit)(&b);

	isc_buffer_init(&b, buf, sizeof(buf));
	isc_buffer_putstr(&b, "DS algorithms:");
	list_ds_algorithms(&b);
	(*emit)(&b);

	isc_buffer_init(&b, buf, sizeof(buf));
	isc_buffer_putstr(&b, "HMAC algorithms:");
	list_hmac_algorithms(&b);
	(*emit)(&b);

	isc_buffer_init(&b, buf, sizeof(buf));
	isc_buffer_printf(&b, "TKEY mode 2 support (Diffie-Hellman): %s", "no");
	(*emit)(&b);

	isc_buffer_init(&b, buf, sizeof(buf));
	isc_buffer_printf(&b, "TKEY mode 3 support (GSS-API): %s",
			  dst_algorithm_supported(DST_ALG_GSSAPI) ? "yes"
								  : "no");
	(*emit)(&b);
}

static void
printversion(bool verbose) {
	char rndcconf[PATH_MAX], *dot = NULL;
	isc_buffer_t b;
	char buf[512];
#if defined(HAVE_GEOIP2)
	cfg_parser_t *parser = NULL;
	cfg_obj_t *config = NULL;
	const cfg_obj_t *defaults = NULL, *obj = NULL;
#endif /* if defined(HAVE_GEOIP2) */

	printf("%s (%s) <id:%s>\n", PACKAGE_STRING, PACKAGE_DESCRIPTION,
	       PACKAGE_SRCID);

	if (!verbose) {
		return;
	}

	printf("running on %s\n", named_os_uname());
	printf("built by %s with %s\n", PACKAGE_BUILDER, PACKAGE_CONFIGARGS);
#ifdef __clang__
	printf("compiled by CLANG %s\n", __VERSION__);
#else /* ifdef __clang__ */
#if defined(__ICC) || defined(__INTEL_COMPILER)
	printf("compiled by ICC %s\n", __VERSION__);
#else /* if defined(__ICC) || defined(__INTEL_COMPILER) */
#ifdef __GNUC__
	printf("compiled by GCC %s\n", __VERSION__);
#endif /* ifdef __GNUC__ */
#endif /* if defined(__ICC) || defined(__INTEL_COMPILER) */
#endif /* ifdef __clang__ */
#ifdef __SUNPRO_C
	printf("compiled by Solaris Studio %x\n", __SUNPRO_C);
#endif /* ifdef __SUNPRO_C */
	printf("compiled with OpenSSL version: %s\n", OPENSSL_VERSION_TEXT);
	printf("linked to OpenSSL version: %s\n",
	       OpenSSL_version(OPENSSL_VERSION));
	printf("compiled with libuv version: %d.%d.%d\n", UV_VERSION_MAJOR,
	       UV_VERSION_MINOR, UV_VERSION_PATCH);
	printf("linked to libuv version: %s\n", uv_version_string());
	printf("compiled with %s version: %s\n", RCU_FLAVOR, RCU_VERSION);
#if defined(JEMALLOC_VERSION)
	printf("compiled with jemalloc version: %u.%u.%u\n",
	       JEMALLOC_VERSION_MAJOR, JEMALLOC_VERSION_MINOR,
	       JEMALLOC_VERSION_BUGFIX);
#elif defined(M_VERSION)
	printf("compiled with system jemalloc version: %u\n", M_VERSION);
#endif
#if HAVE_LIBNGHTTP2
	nghttp2_info *nginfo = NULL;
	printf("compiled with libnghttp2 version: %s\n", NGHTTP2_VERSION);
	nginfo = nghttp2_version(1);
	printf("linked to libnghttp2 version: %s\n", nginfo->version_str);
#endif
#ifdef HAVE_LIBXML2
	printf("compiled with libxml2 version: %s\n", LIBXML_DOTTED_VERSION);
	printf("linked to libxml2 version: %s\n", xmlParserVersion);
#endif /* ifdef HAVE_LIBXML2 */
#if defined(HAVE_JSON_C)
	printf("compiled with json-c version: %s\n", JSON_C_VERSION);
	printf("linked to json-c version: %s\n", json_c_version());
#endif /* if defined(HAVE_JSON_C) */
#if defined(HAVE_ZLIB) && defined(ZLIB_VERSION)
	printf("compiled with zlib version: %s\n", ZLIB_VERSION);
	printf("linked to zlib version: %s\n", zlibVersion());
#endif /* if defined(HAVE_ZLIB) && defined(ZLIB_VERSION) */
#if defined(HAVE_GEOIP2)
	/* Unfortunately, no version define on link time */
	printf("linked to maxminddb version: %s\n", MMDB_lib_version());
#endif /* if defined(HAVE_GEOIP2) */
#if defined(HAVE_DNSTAP)
	printf("compiled with protobuf-c version: %s\n", PROTOBUF_C_VERSION);
	printf("linked to protobuf-c version: %s\n", protobuf_c_version());
#endif /* if defined(HAVE_DNSTAP) */
	printf("threads support is enabled\n");

	isc_buffer_init(&b, buf, sizeof(buf));
	format_supported_algorithms(printit);
	printf("\n");

	/*
	 * The default rndc.conf and rndc.key paths are in the same
	 * directory, but named only has rndc.key defined internally.
	 * We construct the rndc.conf path from it.
	 */
	strlcpy(rndcconf, named_g_keyfile, sizeof(rndcconf));
	dot = strrchr(rndcconf, '.');
	if (dot != NULL) {
		size_t len = dot - rndcconf + 1;
		snprintf(dot + 1, PATH_MAX - len, "conf");
	}

	/*
	 * Print default configuration paths.
	 */
	printf("default paths:\n");
	printf("  named configuration:  %s\n", named_g_conffile);
	printf("  rndc configuration:   %s\n", rndcconf);
	printf("  nsupdate session key: %s\n", named_g_defaultsessionkeyfile);
	printf("  named PID file:       %s\n", named_g_defaultpidfile);
#if defined(HAVE_GEOIP2)
#define RTC(x) RUNTIME_CHECK((x) == ISC_R_SUCCESS)
	isc_mem_t *mctx = NULL;
	isc_mem_create("geoip", &mctx);
	RTC(cfg_parser_create(mctx, &parser));
	RTC(named_config_parsedefaults(parser, &config));
	RTC(cfg_map_get(config, "options", &defaults));
	RTC(cfg_map_get(defaults, "geoip-directory", &obj));
	if (cfg_obj_isstring(obj)) {
		printf("  geoip-directory:      %s\n", cfg_obj_asstring(obj));
	}
	cfg_obj_destroy(parser, &config);
	cfg_parser_destroy(&parser);
	isc_mem_detach(&mctx);
#endif /* HAVE_GEOIP2 */
}

static void
parse_fuzz_arg(void) {
	if (!strncmp(isc_commandline_argument, "client:", 7)) {
		named_g_fuzz_addr = isc_commandline_argument + 7;
		named_g_fuzz_type = isc_fuzz_client;
	} else if (!strncmp(isc_commandline_argument, "tcp:", 4)) {
		named_g_fuzz_addr = isc_commandline_argument + 4;
		named_g_fuzz_type = isc_fuzz_tcpclient;
	} else if (!strncmp(isc_commandline_argument, "resolver:", 9)) {
		named_g_fuzz_addr = isc_commandline_argument + 9;
		named_g_fuzz_type = isc_fuzz_resolver;
	} else if (!strncmp(isc_commandline_argument, "http:", 5)) {
		named_g_fuzz_addr = isc_commandline_argument + 5;
		named_g_fuzz_type = isc_fuzz_http;
	} else if (!strncmp(isc_commandline_argument, "rndc:", 5)) {
		named_g_fuzz_addr = isc_commandline_argument + 5;
		named_g_fuzz_type = isc_fuzz_rndc;
	} else {
		named_main_earlyfatal("unknown fuzzing type '%s'",
				      isc_commandline_argument);
	}
}

static void
parse_T_opt(char *option) {
	const char *p;
	char *last = NULL;
	/*
	 * force the server to behave (or misbehave) in
	 * specified ways for testing purposes.
	 */
	if (!strcmp(option, "cookiealwaysvalid")) {
		cookiealwaysvalid = true;
	} else if (!strcmp(option, "dropedns")) {
		dropedns = true;
	} else if (!strcmp(option, "ednsformerr")) {
		ednsformerr = true;
	} else if (!strcmp(option, "ednsnotimp")) {
		ednsnotimp = true;
	} else if (!strcmp(option, "ednsrefused")) {
		ednsrefused = true;
	} else if (!strcmp(option, "fixedlocal")) {
		fixedlocal = true;
	} else if (!strcmp(option, "keepstderr")) {
		named_g_keepstderr = true;
	} else if (!strcmp(option, "noaa")) {
		noaa = true;
	} else if (!strcmp(option, "noedns")) {
		noedns = true;
	} else if (!strcmp(option, "nonearest")) {
		nonearest = true;
	} else if (!strcmp(option, "nosoa")) {
		nosoa = true;
	} else if (!strcmp(option, "nosyslog")) {
		named_g_nosyslog = true;
	} else if (!strcmp(option, "notcp")) {
		notcp = true;
	} else if (!strncmp(option, "maxcachesize=", 13)) {
		named_g_maxcachesize = atoi(option + 13);
	} else if (!strcmp(option, "maxudp512")) {
		maxudp = 512;
	} else if (!strcmp(option, "maxudp1460")) {
		maxudp = 1460;
	} else if (!strncmp(option, "maxudp=", 7)) {
		maxudp = atoi(option + 7);
		if (maxudp <= 0) {
			named_main_earlyfatal("bad maxudp");
		}
	} else if (!strncmp(option, "mkeytimers=", 11)) {
		p = strtok_r(option + 11, "/", &last);
		if (p == NULL) {
			named_main_earlyfatal("bad mkeytimer");
		}

		dns_zone_mkey_hour = atoi(p);
		if (dns_zone_mkey_hour == 0) {
			named_main_earlyfatal("bad mkeytimer");
		}

		p = strtok_r(NULL, "/", &last);
		if (p == NULL) {
			dns_zone_mkey_day = (24 * dns_zone_mkey_hour);
			dns_zone_mkey_month = (30 * dns_zone_mkey_day);
			return;
		}

		dns_zone_mkey_day = atoi(p);
		if (dns_zone_mkey_day < dns_zone_mkey_hour) {
			named_main_earlyfatal("bad mkeytimer");
		}

		p = strtok_r(NULL, "/", &last);
		if (p == NULL) {
			dns_zone_mkey_month = (30 * dns_zone_mkey_day);
			return;
		}

		dns_zone_mkey_month = atoi(p);
		if (dns_zone_mkey_month < dns_zone_mkey_day) {
			named_main_earlyfatal("bad mkeytimer");
		}
	} else if (!strcmp(option, "sigvalinsecs")) {
		sigvalinsecs = true;
	} else if (!strcmp(option, "transferinsecs")) {
		transferinsecs = true;
	} else if (!strcmp(option, "transferslowly")) {
		transferslowly = true;
	} else if (!strcmp(option, "transferstuck")) {
		transferstuck = true;
	} else if (!strncmp(option, "tat=", 4)) {
		named_g_tat_interval = atoi(option + 4);
	} else {
		fprintf(stderr, "unknown -T flag '%s'\n", option);
	}
}

static void
parse_port(char *arg) {
	enum { DNSPORT, TLSPORT, HTTPSPORT, HTTPPORT } ptype = DNSPORT;
	char *value = arg;
	int port;

	if (strncmp(arg, "dns=", 4) == 0) {
		value = arg + 4;
	} else if (strncmp(arg, "tls=", 4) == 0) {
		value = arg + 4;
		ptype = TLSPORT;
	} else if (strncmp(arg, "https=", 6) == 0) {
		value = arg + 6;
		ptype = HTTPSPORT;
	} else if (strncmp(arg, "http=", 5) == 0) {
		value = arg + 6;
		ptype = HTTPPORT;
	}

	port = parse_int(value, "port");
	if (port < 1 || port > 65535) {
		named_main_earlyfatal("port '%s' out of range", value);
	}

	switch (ptype) {
	case DNSPORT:
		named_g_port = port;
		break;
	case TLSPORT:
		named_g_tlsport = port;
		break;
	case HTTPSPORT:
		named_g_httpsport = port;
		break;
	case HTTPPORT:
		named_g_httpport = port;
		break;
	default:
		UNREACHABLE();
	}
}

static void
parse_command_line(int argc, char *argv[]) {
	int ch;
	const char *p;

	save_command_line(argc, argv);

	/*
	 * NAMED_MAIN_ARGS is defined in main.h, so that it can be used
	 * both by named and by ntservice hooks.
	 */
	isc_commandline_errprint = false;
	while ((ch = isc_commandline_parse(argc, argv, NAMED_MAIN_ARGS)) != -1)
	{
		switch (ch) {
		case '4':
			if (disable4) {
				named_main_earlyfatal("cannot specify "
						      "-4 and -6");
			}
			if (isc_net_probeipv4() != ISC_R_SUCCESS) {
				named_main_earlyfatal("IPv4 not supported "
						      "by OS");
			}
			isc_net_disableipv6();
			disable6 = true;
			break;
		case '6':
			if (disable6) {
				named_main_earlyfatal("cannot specify "
						      "-4 and -6");
			}
			if (isc_net_probeipv6() != ISC_R_SUCCESS) {
				named_main_earlyfatal("IPv6 not supported "
						      "by OS");
			}
			isc_net_disableipv4();
			disable4 = true;
			break;
		case 'A':
			parse_fuzz_arg();
			break;
		case 'c':
			named_g_conffile = isc_commandline_argument;
			named_g_conffileset = true;
			break;
		case 'C':
			printf("# Built-in default values. "
			       "This is NOT the run-time configuration!\n");
			printf("%s", named_config_getdefault());
			exit(EXIT_SUCCESS);
		case 'd':
			named_g_debuglevel = parse_int(isc_commandline_argument,
						       "debug "
						       "level");
			break;
		case 'D':
			/* Descriptive comment for 'ps'. */
			break;
		case 'E':
			named_main_earlyfatal(
				"%s", isc_result_totext(DST_R_NOENGINE));
			break;
		case 'f':
			named_g_foreground = true;
			break;
		case 'g':
			named_g_foreground = true;
			named_g_logstderr = true;
			named_g_logflags = ISC_LOG_PRINTTIME | ISC_LOG_ISO8601 |
					   ISC_LOG_TZINFO;
			break;
		case 'L':
			named_g_logfile = isc_commandline_argument;
			break;
		case 'M':
			set_flags(isc_commandline_argument, mem_context_flags,
				  &isc_mem_defaultflags);
			break;
		case 'm':
			set_flags(isc_commandline_argument, mem_debug_flags,
				  &isc_mem_debugging);
			break;
		case 'N': /* Deprecated. */
		case 'n':
			named_g_cpus = parse_int(isc_commandline_argument,
						 "number of cpus");
			if (named_g_cpus == 0) {
				named_g_cpus = 1;
			}
			break;
		case 'p':
			parse_port(isc_commandline_argument);
			break;
		case 's':
			/* XXXRTH temporary syntax */
			want_stats = true;
			break;
		case 'S':
			/* Formerly maxsocks */
			break;
		case 't':
			/* XXXJAB should we make a copy? */
			named_g_chrootdir = isc_commandline_argument;
			break;
		case 'T': /* NOT DOCUMENTED */
			parse_T_opt(isc_commandline_argument);
			break;
		case 'U':
			/* Obsolete.  No longer in use.  Ignore. */
			named_main_earlywarning("option '-U' has been removed");
			break;
		case 'u':
			named_g_username = isc_commandline_argument;
			break;
		case 'v':
			printversion(false);
			exit(EXIT_SUCCESS);
		case 'V':
			printversion(true);
			exit(EXIT_SUCCESS);
		case 'x':
			/* Obsolete. No longer in use. Ignore. */
			break;
		case 'X':
			/* Obsolete. No longer in use. Abort. */
			named_main_earlyfatal("option '-X' has been removed");
			break;
		case 'F':
			if (isc_crypto_fips_enable() != ISC_R_SUCCESS) {
				named_main_earlyfatal(
					"setting FIPS mode failed");
			}
			break;
		case '?':
			usage();
			if (isc_commandline_option == '?') {
				exit(EXIT_SUCCESS);
			}
			p = strchr(NAMED_MAIN_ARGS, isc_commandline_option);
			if (p == NULL || *++p != ':') {
				named_main_earlyfatal("unknown option '-%c'",
						      isc_commandline_option);
			} else {
				named_main_earlyfatal("option '-%c' requires "
						      "an argument",
						      isc_commandline_option);
			}
			FALLTHROUGH;
		default:
			named_main_earlyfatal("parsing options returned %d",
					      ch);
		}
	}

	argc -= isc_commandline_index;
	argv += isc_commandline_index;
	POST(argv);

	if (argc > 0) {
		usage();
		named_main_earlyfatal("extra command line arguments");
	}
}

static isc_result_t
create_managers(void) {
	bool capped = false;

	/*
	 * Set the default named_g_cpus if it was not set from the command line
	 */
	INSIST(named_g_cpus_detected > 0);
	if (named_g_cpus == 0) {
		named_g_cpus = named_g_cpus_detected;
	}

	if (named_g_cpus > ISC_TID_MAX) {
		capped = true;
		named_g_cpus = ISC_TID_MAX;
	}

	isc_log_write(
		NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_SERVER, ISC_LOG_INFO,
		"found %u CPU%s, using %u worker thread%s%s",
		named_g_cpus_detected, named_g_cpus_detected == 1 ? "" : "s",
		named_g_cpus, named_g_cpus == 1 ? "" : "s",
		capped ? " (recompile with -DISC_TID_MAX=<n> to raise the "
			 "thread count limit)"
		       : "");

	isc_managers_create(&named_g_mctx, named_g_cpus);

	isc_nm_maxudp(maxudp);

	return ISC_R_SUCCESS;
}

static void
setup(void) {
	isc_result_t result;
	ns_server_t *sctx;
#ifdef HAVE_LIBSCF
	char *instance = NULL;
#endif /* ifdef HAVE_LIBSCF */

	/*
	 * Get the user and group information before changing the root
	 * directory, so the administrator does not need to keep a copy
	 * of the user and group databases in the chroot'ed environment.
	 */
	named_os_inituserinfo(named_g_username);

	/*
	 * Initialize time conversion information
	 */
	named_os_tzset();

	named_os_opendevnull();

#ifdef HAVE_LIBSCF
	/* Check if named is under smf control, before chroot. */
	result = named_smf_get_instance(&instance, 0, named_g_mctx);
	/* We don't care about instance, just check if we got one. */
	if (result == ISC_R_SUCCESS) {
		named_smf_got_instance = 1;
	} else {
		named_smf_got_instance = 0;
	}
	if (instance != NULL) {
		isc_mem_free(named_g_mctx, instance);
	}
#endif /* HAVE_LIBSCF */

	/*
	 * Check for the number of cpu's before named_os_chroot().
	 */
	named_g_cpus_detected = isc_os_ncpus();

	named_os_chroot(named_g_chrootdir);

	/*
	 * For operating systems which have a capability mechanism, now
	 * is the time to switch to minimal privs and change our user id.
	 * On traditional UNIX systems, this call will be a no-op, and we
	 * will change the user ID after reading the config file the first
	 * time.  (We need to read the config file to know which possibly
	 * privileged ports to bind() to.)
	 */
	named_os_minprivs();

	result = named_log_init(named_g_username != NULL);
	if (result != ISC_R_SUCCESS) {
		named_main_earlyfatal("named_log_init() failed: %s",
				      isc_result_totext(result));
	}

	/*
	 * Now is the time to daemonize (if we're not running in the
	 * foreground).  We waited until now because we wanted to get
	 * a valid logging context setup.  We cannot daemonize any later,
	 * because calling create_managers() will create threads, which
	 * would be lost after fork().
	 */
	if (!named_g_foreground) {
		named_os_daemonize();
	}

	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "starting %s (%s) <id:%s>",
		      PACKAGE_STRING, PACKAGE_DESCRIPTION, PACKAGE_SRCID);

	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "running on %s", named_os_uname());

	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "built with %s", PACKAGE_CONFIGARGS);

	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "running as: %s%s%s",
		      isc_commandline_progname, saved_command_line, ellipsis);
#ifdef __clang__
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "compiled by CLANG %s", __VERSION__);
#else /* ifdef __clang__ */
#if defined(__ICC) || defined(__INTEL_COMPILER)
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "compiled by ICC %s", __VERSION__);
#else /* if defined(__ICC) || defined(__INTEL_COMPILER) */
#ifdef __GNUC__
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "compiled by GCC %s", __VERSION__);
#endif /* ifdef __GNUC__ */
#endif /* if defined(__ICC) || defined(__INTEL_COMPILER) */
#endif /* ifdef __clang__ */
#ifdef __SUNPRO_C
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "compiled by Solaris Studio %x",
		      __SUNPRO_C);
#endif /* ifdef __SUNPRO_C */
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "compiled with OpenSSL version: %s",
		      OPENSSL_VERSION_TEXT);
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "linked to OpenSSL version: %s",
		      OpenSSL_version(OPENSSL_VERSION));
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "compiled with libuv version: %d.%d.%d",
		      UV_VERSION_MAJOR, UV_VERSION_MINOR, UV_VERSION_PATCH);
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "linked to libuv version: %s",
		      uv_version_string());
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "compiled with %s version: %s",
		      RCU_FLAVOR, RCU_VERSION);
#if defined(JEMALLOC_VERSION)
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE,
		      "compiled with jemalloc version: %u.%u.%u",
		      JEMALLOC_VERSION_MAJOR, JEMALLOC_VERSION_MINOR,
		      JEMALLOC_VERSION_BUGFIX);
#elif defined(M_VERSION)
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE,
		      "compiled with system jemalloc version: %u", M_VERSION);
#endif
#if HAVE_LIBNGHTTP2
	nghttp2_info *nginfo = NULL;
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "compiled with libnghttp2 version: %s",
		      NGHTTP2_VERSION);
	nginfo = nghttp2_version(1);
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "linked to libnghttp2 version: %s",
		      nginfo->version_str);
#endif
#ifdef HAVE_LIBXML2
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "compiled with libxml2 version: %s",
		      LIBXML_DOTTED_VERSION);
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "linked to libxml2 version: %s",
		      xmlParserVersion);
#endif /* ifdef HAVE_LIBXML2 */
#if defined(HAVE_JSON_C)
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "compiled with json-c version: %s",
		      JSON_C_VERSION);
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "linked to json-c version: %s",
		      json_c_version());
#endif /* if defined(HAVE_JSON_C) */
#if defined(HAVE_ZLIB) && defined(ZLIB_VERSION)
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "compiled with zlib version: %s",
		      ZLIB_VERSION);
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "linked to zlib version: %s",
		      zlibVersion());
#endif /* if defined(HAVE_ZLIB) && defined(ZLIB_VERSION) */
#if defined(HAVE_GEOIP2)
	/* Unfortunately, no version define on link time */
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "linked to maxminddb version: %s",
		      MMDB_lib_version());
#endif /* if defined(HAVE_GEOIP2) */
#if defined(HAVE_DNSTAP)
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "compiled with protobuf-c version: %s",
		      PROTOBUF_C_VERSION);
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "linked to protobuf-c version: %s",
		      protobuf_c_version());
#endif /* if defined(HAVE_DNSTAP) */
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE,
		      "----------------------------------------------------");
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE,
		      "BIND 9 is maintained by Internet Systems Consortium,");
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE,
		      "Inc. (ISC), a non-profit 501(c)(3) public-benefit ");
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE,
		      "corporation.  Support and training for BIND 9 are ");
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE,
		      "available at https://www.isc.org/support");
	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE,
		      "----------------------------------------------------");

	/*
	 * System resources cannot effectively be tuned on some systems.
	 * Raise the limit in such cases for safety.
	 */
	named_os_adjustnofile();

	/*
	 * If the named configuration filename is relative, prepend the current
	 * directory's name before possibly changing to another directory.
	 */
	if (!isc_file_isabsolute(named_g_conffile)) {
		result = isc_file_absolutepath(named_g_conffile,
					       absolute_conffile,
					       sizeof(absolute_conffile));
		if (result != ISC_R_SUCCESS) {
			named_main_earlyfatal("could not construct "
					      "absolute path "
					      "of configuration file: %s",
					      isc_result_totext(result));
		}
		named_g_conffile = absolute_conffile;
	}

	/*
	 * Record the server's startup time.
	 */
	named_g_boottime = isc_time_now();

	result = create_managers();
	if (result != ISC_R_SUCCESS) {
		named_main_earlyfatal("create_managers() failed: %s",
				      isc_result_totext(result));
	}

	named_builtin_init();

	/*
	 * Add calls to register sdb drivers here.
	 */
	/* xxdb_init(); */

	/*
	 * Register the DLZ "dlopen" driver.
	 */
	result = dlz_dlopen_init(named_g_mctx);
	if (result != ISC_R_SUCCESS) {
		named_main_earlyfatal("dlz_dlopen_init() failed: %s",
				      isc_result_totext(result));
	}

	named_server_create(named_g_mctx, &named_g_server);
	ENSURE(named_g_server != NULL);
	sctx = named_g_server->sctx;

	format_supported_algorithms(logit);

	/*
	 * Modify server context according to command line options
	 */
	if (cookiealwaysvalid) {
		ns_server_setoption(sctx, NS_SERVER_COOKIEALWAYSVALID, true);
	}
	if (disable4) {
		ns_server_setoption(sctx, NS_SERVER_DISABLE4, true);
	}
	if (disable6) {
		ns_server_setoption(sctx, NS_SERVER_DISABLE6, true);
	}
	if (dropedns) {
		ns_server_setoption(sctx, NS_SERVER_DROPEDNS, true);
	}
	if (ednsformerr) { /* STD13 server */
		ns_server_setoption(sctx, NS_SERVER_EDNSFORMERR, true);
	}
	if (ednsnotimp) {
		ns_server_setoption(sctx, NS_SERVER_EDNSNOTIMP, true);
	}
	if (ednsrefused) {
		ns_server_setoption(sctx, NS_SERVER_EDNSREFUSED, true);
	}
	if (fixedlocal) {
		ns_server_setoption(sctx, NS_SERVER_FIXEDLOCAL, true);
	}
	if (noaa) {
		ns_server_setoption(sctx, NS_SERVER_NOAA, true);
	}
	if (noedns) {
		ns_server_setoption(sctx, NS_SERVER_NOEDNS, true);
	}
	if (nonearest) {
		ns_server_setoption(sctx, NS_SERVER_NONEAREST, true);
	}
	if (nosoa) {
		ns_server_setoption(sctx, NS_SERVER_NOSOA, true);
	}
	if (notcp) {
		ns_server_setoption(sctx, NS_SERVER_NOTCP, true);
	}
	if (sigvalinsecs) {
		ns_server_setoption(sctx, NS_SERVER_SIGVALINSECS, true);
	}
	if (transferinsecs) {
		ns_server_setoption(sctx, NS_SERVER_TRANSFERINSECS, true);
	}
	if (transferslowly) {
		ns_server_setoption(sctx, NS_SERVER_TRANSFERSLOWLY, true);
	}
	if (transferstuck) {
		ns_server_setoption(sctx, NS_SERVER_TRANSFERSTUCK, true);
	}
}

static void
cleanup(void) {
	if (named_g_mapped != NULL) {
		dns_acl_detach(&named_g_mapped);
	}

	named_server_destroy(&named_g_server);

	named_builtin_deinit();

	/*
	 * Add calls to unregister sdb drivers here.
	 */
	/* xxdb_clear(); */

	/*
	 * Unregister "dlopen" DLZ driver.
	 */
	dlz_dlopen_clear();

	isc_log_write(NAMED_LOGCATEGORY_GENERAL, NAMED_LOGMODULE_MAIN,
		      ISC_LOG_NOTICE, "exiting");
}

static char *memstats = NULL;

void
named_main_setmemstats(const char *filename) {
	/*
	 * Caller has to ensure locking.
	 */

	if (memstats != NULL) {
		free(memstats);
		memstats = NULL;
	}

	if (filename == NULL) {
		return;
	}

	memstats = strdup(filename);
}

#ifdef HAVE_LIBSCF
/*
 * Get FMRI for the named process.
 */
isc_result_t
named_smf_get_instance(char **ins_name, int debug, isc_mem_t *mctx) {
	scf_handle_t *h = NULL;
	int namelen;
	char *instance;

	REQUIRE(ins_name != NULL && *ins_name == NULL);

	if ((h = scf_handle_create(SCF_VERSION)) == NULL) {
		if (debug) {
			UNEXPECTED_ERROR("scf_handle_create() failed: %s",
					 scf_strerror(scf_error()));
		}
		return ISC_R_FAILURE;
	}

	if (scf_handle_bind(h) == -1) {
		if (debug) {
			UNEXPECTED_ERROR("scf_handle_bind() failed: %s",
					 scf_strerror(scf_error()));
		}
		scf_handle_destroy(h);
		return ISC_R_FAILURE;
	}

	if ((namelen = scf_myname(h, NULL, 0)) == -1) {
		if (debug) {
			UNEXPECTED_ERROR("scf_myname() failed: %s",
					 scf_strerror(scf_error()));
		}
		scf_handle_destroy(h);
		return ISC_R_FAILURE;
	}

	instance = isc_mem_allocate(mctx, namelen + 1);
	if (scf_myname(h, instance, namelen + 1) == -1) {
		if (debug) {
			UNEXPECTED_ERROR("scf_myname() failed: %s",
					 scf_strerror(scf_error()));
		}
		scf_handle_destroy(h);
		isc_mem_free(mctx, instance);
		return ISC_R_FAILURE;
	}

	scf_handle_destroy(h);
	*ins_name = instance;
	return ISC_R_SUCCESS;
}
#endif /* HAVE_LIBSCF */

/* main entry point, possibly hooked */

int
main(int argc, char *argv[]) {
	isc_result_t result;
#ifdef HAVE_LIBSCF
	char *instance = NULL;
#endif /* ifdef HAVE_LIBSCF */

#ifdef HAVE_GPERFTOOLS_PROFILER
	(void)ProfilerStart(NULL);
#endif /* ifdef HAVE_GPERFTOOLS_PROFILER */

	/*
	 * Technically, this call is superfluous because on startup of the main
	 * program, the portable "C" locale is selected by default.  This
	 * explicit call here is for a reference that the BIND 9 code base is
	 * not locale aware and the locale MUST be set to "C" (or "POSIX") when
	 * calling any BIND 9 library code.  If you are calling external
	 * libraries that use locale, such calls must be wrapped into
	 * setlocale(LC_ALL, ""); before the call and setlocale(LC_ALL, "C");
	 * after the call, and no BIND 9 library calls must be made in between.
	 */
	setlocale(LC_ALL, "C");

	/*
	 * Record version in core image.
	 * strings named.core | grep "named version:"
	 */
	strlcat(version,
#if defined(NO_VERSION_DATE) || !defined(__DATE__)
		"named version: BIND " PACKAGE_VERSION " <" PACKAGE_SRCID ">",
#else
		"named version: BIND " PACKAGE_VERSION " <" PACKAGE_SRCID
		"> (" __DATE__ ")",
#endif
		sizeof(version));

	isc_commandline_init(argc, argv);

	isc_assertion_setcallback(assertion_failed);
	isc_error_setfatal(library_fatal_error);
	isc_error_setunexpected(library_unexpected_error);

	named_os_init(isc_commandline_progname);

	parse_command_line(argc, argv);

#ifdef ENABLE_AFL
	if (named_g_fuzz_type != isc_fuzz_none) {
		named_fuzz_setup();
	}

	if (named_g_fuzz_type == isc_fuzz_resolver) {
		dns_resolver_setfuzzing();
	} else if (named_g_fuzz_type == isc_fuzz_http) {
		isc_httpd_setfinishhook(named_fuzz_notify);
	}
#endif /* ifdef ENABLE_AFL */
	/*
	 * Warn about common configuration error.
	 */
	if (named_g_chrootdir != NULL) {
		int len = strlen(named_g_chrootdir);
		if (strncmp(named_g_chrootdir, named_g_conffile, len) == 0 &&
		    (named_g_conffile[len] == '/' ||
		     named_g_conffile[len] == '\\'))
		{
			named_main_earlywarning("config filename (-c %s) "
						"contains chroot path (-t %s)",
						named_g_conffile,
						named_g_chrootdir);
		}
	}

	setup();
	INSIST(named_g_server != NULL);

	/*
	 * Start things running
	 */
	isc_signal_start(named_g_server->sighup);
	isc_signal_start(named_g_server->sigusr1);

	/*
	 * Pause the loop manager in fatal.
	 */
	named_g_loopmgr_running = true;
	isc_loopmgr_run();

#ifdef HAVE_LIBSCF
	if (named_smf_want_disable == 1) {
		result = named_smf_get_instance(&instance, 1, named_g_mctx);
		if (result == ISC_R_SUCCESS && instance != NULL) {
			if (smf_disable_instance(instance, 0) != 0) {
				UNEXPECTED_ERROR("smf_disable_instance() "
						 "failed for %s : %s",
						 instance,
						 scf_strerror(scf_error()));
			}
		}
		if (instance != NULL) {
			isc_mem_free(named_g_mctx, instance);
		}
	}
#endif /* HAVE_LIBSCF */

	cleanup();

	if (want_stats) {
		isc_mem_stats(named_g_mctx, stdout);
	}

	if (named_g_memstatistics && memstats != NULL) {
		FILE *fp = NULL;
		result = isc_stdio_open(memstats, "w", &fp);
		if (result == ISC_R_SUCCESS) {
			isc_mem_stats(named_g_mctx, fp);
			(void)isc_stdio_close(fp);
		}
	}

	isc_managers_destroy(&named_g_mctx);

#if ENABLE_LEAK_DETECTION
	isc__crypto_setdestroycheck(true);
	isc__uv_setdestroycheck(true);
	isc__xml_setdestroycheck(true);
#endif

	isc_mem_checkdestroyed(stderr);

	named_main_setmemstats(NULL);

	named_os_closedevnull();

	named_os_shutdown();

#ifdef HAVE_GPERFTOOLS_PROFILER
	ProfilerStop();
#endif /* ifdef HAVE_GPERFTOOLS_PROFILER */

	return 0;
}
