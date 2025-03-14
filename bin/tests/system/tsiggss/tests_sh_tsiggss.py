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

import pytest

pytestmark = pytest.mark.extra_artifacts(
    [
        "authsock.pid",
        "nsupdate.out*",
        "ns1/K*",
        "ns1/_default.tsigkeys",
        "ns1/auth.sock",
        "ns1/example.nil.db",
        "ns1/example.nil.db.jnl",
        "ns1/update.txt",
    ]
)


def test_tsiggss(run_tests_sh):
    run_tests_sh()
