# Copyright (C) Internet Systems Consortium, Inc. ("ISC")
#
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0.  If a copy of the MPL was not distributed with this
# file, you can obtain one at https://mozilla.org/MPL/2.0/.
#
# See the COPYRIGHT file distributed with this work for additional
# information regarding copyright ownership.

import os

import pytest

import isctest

pytestmark = pytest.mark.extra_artifacts(
    [
        "tempzone",
    ]
)


@pytest.mark.parametrize(
    "option,expected_result",
    [
        ("-C", ["HS", "CH", "IN"]),
        (
            "-T",
            [
                "A",
                "A6",
                "AAAA",
                "AFSDB",
                "AMTRELAY",
                "APL",
                "ATMA",
                "AVC",
                "CAA",
                "CDNSKEY",
                "CDS",
                "CERT",
                "CNAME",
                "CSYNC",
                "DHCID",
                "DLV",
                "DNAME",
                "DNSKEY",
                "DOA",
                "DS",
                "EID",
                "EUI48",
                "EUI64",
                "GID",
                "GPOS",
                "HINFO",
                "HIP",
                "HTTPS",
                "IPSECKEY",
                "ISDN",
                "KEY",
                "KX",
                "L32",
                "L64",
                "LOC",
                "LP",
                "MB",
                "MD",
                "MF",
                "MG",
                "MINFO",
                "MR",
                "MX",
                "NAPTR",
                "NID",
                "NIMLOC",
                "NINFO",
                "NS",
                "NSAP",
                "NSAP-PTR",
                "NSEC",
                "NSEC3",
                "NSEC3PARAM",
                "NULL",
                "NXT",
                "OPENPGPKEY",
                "PTR",
                "PX",
                "RESINFO",
                "RKEY",
                "RP",
                "RRSIG",
                "RT",
                "SIG",
                "SINK",
                "SMIMEA",
                "SOA",
                "SPF",
                "SRV",
                "SSHFP",
                "SVCB",
                "TA",
                "TALINK",
                "TLSA",
                "TXT",
                "UID",
                "UINFO",
                "UNSPEC",
                "URI",
                "WALLET",
                "WKS",
                "X25",
                "ZONEMD",
            ],
        ),
        ("-P", []),
    ],
)
def test_rrchecker_list_standard_names(option, expected_result):
    stdout = isctest.run.cmd([os.environ["RRCHECKER"], option]).stdout.decode("utf-8")
    values = [line for line in stdout.split("\n") if line.strip()]

    assert sorted(values) == sorted(expected_result)


def run_rrchecker(option, rr_class, rr_type, rr_rest):
    rrchecker_output = (
        isctest.run.cmd(
            [os.environ["RRCHECKER"], option],
            input_text=f"{rr_class} {rr_type} {rr_rest}".encode("utf-8"),
        )
        .stdout.decode("utf-8")
        .strip()
    )
    return rrchecker_output.split()


@pytest.mark.parametrize(
    "option",
    ["-p", "-u", "multi-line at class", " multi-line at type", "multi-line at data"],
)
def test_rrchecker_conversions(option):
    tempzone_file = "tempzone"
    with open(tempzone_file, "w", encoding="utf-8") as file:
        isctest.run.cmd(
            [
                os.environ["SHELL"],
                os.environ["TOP_SRCDIR"] + "/bin/tests/system/genzone.sh",
                "0",
            ],
            stdout=file,
        )
    checkzone_output = isctest.run.cmd(
        [
            os.environ["CHECKZONE"],
            "-D",
            "-q",
            ".",
            tempzone_file,
        ],
    ).stdout.decode("utf-8")
    checkzone_output = [
        line for line in checkzone_output.splitlines() if not line.startswith(";")
    ]

    for rr in checkzone_output:
        rr_parts_orig = rr.split()
        assert len(rr_parts_orig) >= 4, f"invalid rr: {rr}"
        rr_class_orig, rr_type_orig, rr_rest_orig = (
            rr_parts_orig[2],
            rr_parts_orig[3],
            " ".join(rr_parts_orig[4:]),
        )
        rr_class, rr_type, rr_rest = rr_class_orig, rr_type_orig, rr_rest_orig
        if option == "-u":
            rr_class, rr_type, *rr_rest = run_rrchecker(
                "-u", rr_class_orig, rr_type_orig, rr_rest_orig
            )
            rr_rest = " ".join(rr_rest)
        elif option == "multi-line at class":
            rr_class = "(" + rr_class
            rr_rest = rr_rest + ")"
        elif option == "multi-line at type":
            rr_type = "(" + rr_type
            rr_rest = rr_rest + ")"
        elif option == "multi-line at data":
            rr_rest = "(" + rr_rest
            rr_rest = rr_rest + ")"

        rr_class, rr_type, *rr_rest = run_rrchecker("-p", rr_class, rr_type, rr_rest)

        assert rr_class_orig == rr_class
        assert rr_type_orig == rr_type
        assert rr_rest_orig == " ".join(rr_rest)


def test_rrchecker_seconds():
    # check the acceptance of seconds as RRSIG inception and expiration
    intext = "NSEC 8 0 86400 1390003200 1389394800 33655 .  NYWjZYBV1b+h4j0yu/SmPOOylR8P4IXKDzHX3NwEmU1SUp27aJ91dP+i+UBcnPmBib0hck4DrFVvpflCEpCnVQd2DexcN0GX+3PM7XobxhtDlmnU X1L47zJlbdHNwTqHuPaMM6Xy9HGMXps7O5JVyfggVhTz2C+G5OVxBdb2rOo="
    expected = "NSEC 8 0 86400 20140118000000 20140110230000 33655 . NYWjZYBV1b+h4j0yu/SmPOOylR8P4IXKDzHX3NwEmU1SUp27aJ91dP+i +UBcnPmBib0hck4DrFVvpflCEpCnVQd2DexcN0GX+3PM7XobxhtDlmnU X1L47zJlbdHNwTqHuPaMM6Xy9HGMXps7O5JVyfggVhTz2C+G5OVxBdb2 rOo="
    _, _, *rr_rest = run_rrchecker("-p", "IN", "RRSIG", intext)
    assert " ".join(rr_rest) == expected
