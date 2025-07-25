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
from pathlib import Path
import subprocess
import time
from typing import List, Optional

import isctest.log


def cmd(
    args,
    cwd=None,
    timeout=60,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    log_stdout=True,
    log_stderr=True,
    input_text: Optional[bytes] = None,
    raise_on_exception=True,
    env: Optional[dict] = None,
):
    """Execute a command with given args as subprocess."""
    isctest.log.debug(f"isctest.run.cmd(): {' '.join(args)}")

    def print_debug_logs(procdata):
        if procdata:
            if log_stdout and procdata.stdout:
                isctest.log.debug(
                    f"isctest.run.cmd(): (stdout)\n{procdata.stdout.decode('utf-8')}"
                )
            if log_stderr and procdata.stderr:
                isctest.log.debug(
                    f"isctest.run.cmd(): (stderr)\n{procdata.stderr.decode('utf-8')}"
                )

    if env is None:
        env = dict(os.environ)

    try:
        proc = subprocess.run(
            args,
            stdout=stdout,
            stderr=stderr,
            input=input_text,
            check=True,
            cwd=cwd,
            timeout=timeout,
            env=env,
        )
        print_debug_logs(proc)
        return proc
    except subprocess.CalledProcessError as exc:
        print_debug_logs(exc)
        isctest.log.debug(f"isctest.run.cmd(): (return code) {exc.returncode}")
        if raise_on_exception:
            raise exc
        return exc


def _run_script(
    interpreter: str,
    script: str,
    args: Optional[List[str]] = None,
):
    if args is None:
        args = []
    path = Path(script)
    script = str(path)
    cwd = os.getcwd()
    if not path.exists():
        raise FileNotFoundError(f"script {script} not found in {cwd}")
    isctest.log.debug("running script: %s %s %s", interpreter, script, " ".join(args))
    isctest.log.debug("  workdir: %s", cwd)
    returncode = 1

    command = [interpreter, script] + args
    with subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=1,
        universal_newlines=True,
        errors="backslashreplace",
    ) as proc:
        if proc.stdout:
            for line in proc.stdout:
                isctest.log.info("    %s", line.rstrip("\n"))
        proc.communicate()
        returncode = proc.returncode
        if returncode:
            raise subprocess.CalledProcessError(returncode, command)
        isctest.log.debug("  exited with %d", returncode)


class Dig:
    def __init__(self, base_params: str = ""):
        self.base_params = base_params

    def __call__(self, params: str) -> str:
        """Run the dig command with the given parameters and return the decoded output."""
        return cmd(
            [os.environ.get("DIG")] + f"{self.base_params} {params}".split(),
        ).stdout.decode("utf-8")


def shell(script: str, args: Optional[List[str]] = None) -> None:
    """Run a given script with system's shell interpreter."""
    _run_script(os.environ["SHELL"], script, args)


def perl(script: str, args: Optional[List[str]] = None) -> None:
    """Run a given script with system's perl interpreter."""
    _run_script(os.environ["PERL"], script, args)


def retry_with_timeout(func, timeout, delay=1, msg=None):
    start_time = time.monotonic()
    exc_msg = None
    while time.monotonic() < start_time + timeout:
        exc_msg = None
        try:
            if func():
                return
        except AssertionError as exc:
            exc_msg = str(exc)
        time.sleep(delay)
    if exc_msg is not None:
        isctest.log.error(exc_msg)
    if msg is None:
        if exc_msg is not None:
            msg = exc_msg
        else:
            msg = f"{func.__module__}.{func.__qualname__} timed out after {timeout} s"
    assert False, msg


def get_named_cmdline(cfg_dir, cfg_file="named.conf"):
    cfg_dir = os.path.join(os.getcwd(), cfg_dir)
    assert os.path.isdir(cfg_dir)

    cfg_file = os.path.join(cfg_dir, cfg_file)
    assert os.path.isfile(cfg_file)

    named = os.getenv("NAMED")
    assert named is not None

    named_cmdline = [named, "-c", cfg_file, "-d", "99", "-g"]

    return named_cmdline
