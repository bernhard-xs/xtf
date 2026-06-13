"""Pytest fixtures for GDB-hosted Python/XTF nested-virt tests.

This module runs inside GDB's Python interpreter and provides fixtures to
manage the XTF slave lifecycle: domain creation, gdbsx attachment, GDB
connection, and the high-level test controller interface.
"""

import atexit
import logging
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from typing import Any, Generator, Optional, cast

import pytest

try:
    import gdb
except ImportError:  # pragma: no cover
    gdb = cast(Any, None)

logger = logging.getLogger(__name__)


# Global state for domain/gdbsx lifecycle
_domain_pid: Optional[str] = None
_gdbsx_proc: Optional[subprocess.Popen] = None  # type: ignore[type-arg]
_slave_trap_bp: Optional[Any] = None
_gdb_binary_copy: Optional[str] = None
_domain_name = "test-hvm64-nested-svm-slave"
_gdbsx_port = 9999
_shutdown_requested = False  # Flag to signal clean shutdown on Ctrl-C


def _start_watchdog(
    reason: str, timeout: int
) -> subprocess.Popen:  # type: ignore[type-arg]
    """Start an external watchdog for blocking GDB operations."""
    gdbsx_pid = ""
    if _gdbsx_proc is not None:
        gdbsx_pid = str(_gdbsx_proc.pid)

    env = os.environ.copy()
    env.update(
        {
            "XTF_WATCHDOG_TIMEOUT": str(timeout),
            "XTF_WATCHDOG_REASON": reason,
            "XTF_WATCHDOG_PARENT": str(os.getpid()),
            "XTF_WATCHDOG_DOMAIN": _domain_pid or "",
            "XTF_WATCHDOG_DOMAIN_NAME": _domain_name,
            "XTF_WATCHDOG_GDBSX": gdbsx_pid,
        }
    )

    script = r'''
sleep "$XTF_WATCHDOG_TIMEOUT"
printf '[ERROR] %s timed out after %ss; cleaning up Xen guest and GDB\n' \
    "$XTF_WATCHDOG_REASON" "$XTF_WATCHDOG_TIMEOUT" >&2
if [ -n "$XTF_WATCHDOG_GDBSX" ]; then
    kill -TERM "$XTF_WATCHDOG_GDBSX" >/dev/null 2>&1 || true
fi
if [ -n "$XTF_WATCHDOG_DOMAIN" ]; then
    xl destroy "$XTF_WATCHDOG_DOMAIN" >/dev/null 2>&1 || true
fi
xl destroy "$XTF_WATCHDOG_DOMAIN_NAME" >/dev/null 2>&1 || true
kill -TERM "$XTF_WATCHDOG_PARENT" >/dev/null 2>&1 || true
sleep 2
kill -KILL "$XTF_WATCHDOG_PARENT" >/dev/null 2>&1 || true
'''

    # pylint: disable=consider-using-with
    return subprocess.Popen(
        ["/bin/sh", "-c", script],
        env=env,
        start_new_session=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )


def _cancel_watchdog(proc: subprocess.Popen) -> None:  # type: ignore[type-arg]
    """Cancel a watchdog process that did not fire."""
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=1)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


def _gdb_execute_with_watchdog(command: str, reason: str, timeout: int) -> str:
    """Run a blocking GDB command with hard external cleanup on timeout."""
    watchdog = _start_watchdog(reason, timeout)
    try:
        return gdb.execute(command, to_string=True)
    finally:
        _cancel_watchdog(watchdog)


def _domain_exists() -> bool:
    """Return whether the slave domain is still visible to xl."""
    result = subprocess.run(
        ["xl", "list", _domain_name],
        timeout=5,
        capture_output=True,
        check=False,
    )
    return result.returncode == 0 and _domain_name.encode() in result.stdout


def _ensure_gdbsx_alive() -> None:
    """Fail early if gdbsx has exited before GDB reports a stop."""
    if _gdbsx_proc is None:
        raise RuntimeError("gdbsx has not been started")
    if _gdbsx_proc.poll() is None:
        return

    stdout, stderr = _gdbsx_proc.communicate()
    msg = "gdbsx exited unexpectedly (rc=%s, stdout=%r, stderr=%r)"
    raise RuntimeError(msg % (_gdbsx_proc.returncode, stdout, stderr))


def _stopped_in_slave_trap() -> bool:
    """Return whether GDB is stopped in the slave RPC trap helper."""
    try:
        frame: Any = gdb.newest_frame()
        while frame is not None:
            if frame.name() == "gdb_slave_trap":
                return True
            frame = frame.older()
    except gdb.error:
        return False
    return False


def _stop_location() -> str:
    """Return a short textual description of the current GDB stop."""
    try:
        return gdb.execute("bt 3", to_string=True).strip()
    except gdb.error as exc:
        return f"<unable to inspect stop: {exc}>"


def _install_slave_trap_breakpoint() -> None:
    """Install a silent breakpoint on the RPC trap helper."""
    global _slave_trap_bp

    if _slave_trap_bp is not None:
        return

    breakpoint_obj = gdb.Breakpoint("gdb_slave_trap", internal=True)
    breakpoint_obj.silent = True
    _slave_trap_bp = breakpoint_obj
    logger.debug("Installed silent breakpoint on gdb_slave_trap")


def _continue_until_slave_trap(
    reason: str, timeout: int = 20, attempts: int = 8
) -> str:
    """Continue the guest until it reaches gdb_slave_trap."""
    last_output = ""
    for attempt in range(1, attempts + 1):
        _ensure_gdbsx_alive()
        if not _domain_exists():
            raise RuntimeError("slave domain disappeared before reaching RPC trap")

        logger.debug(
            "Continuing guest for %s (attempt %u/%u)", reason, attempt, attempts
        )
        last_output = _gdb_execute_with_watchdog("continue", reason, timeout)
        logger.debug("Continue output: %s", last_output)

        _ensure_gdbsx_alive()
        if _stopped_in_slave_trap():
            logger.debug("Guest reached gdb_slave_trap for %s", reason)
            return last_output

        logger.debug("Guest stopped before RPC trap: %s", _stop_location())

    msg = "guest did not reach gdb_slave_trap while %s; last stop:\n%s"
    raise RuntimeError(msg % (reason, _stop_location()))


def _handle_sigint(_signum: int, _frame: Any) -> None:
    """Handle Ctrl-C by setting shutdown flag and cleaning up gracefully."""
    global _shutdown_requested
    logger.warning("Received SIGINT, initiating graceful shutdown...")
    _shutdown_requested = True
    _cleanup_domain_and_gdbsx()
    sys.exit(130)  # Standard exit code for SIGINT


def _install_signal_handlers() -> None:
    """Install signal handlers for clean shutdown."""
    signal.signal(signal.SIGINT, _handle_sigint)


def _find_test_binary() -> str:
    """Locate the compiled nested-svm-slave binary."""
    # Try common locations relative to repo root
    search_paths = [
        "build/install/test-hvm64-nested-svm-slave",
        "build/bin/test-hvm64-nested-svm-slave",
        "tests/nested-svm-slave/test-hvm64-nested-svm-slave",
    ]

    for path in search_paths:
        if os.path.exists(path):
            logger.debug("Found test binary: %s", path)
            return path

    msg = "Could not find nested-svm-slave binary. Searched: %s"
    raise FileNotFoundError(msg, search_paths)


def _prepare_gdb_binary(binary: str) -> str:
    """Create a temporary GDB symbol copy without the noisy .note section."""
    global _gdb_binary_copy

    if _gdb_binary_copy is not None:
        return _gdb_binary_copy

    fd, temp_path = tempfile.mkstemp(
        prefix="xtf-gdb-", suffix=f"-{os.path.basename(binary)}"
    )
    os.close(fd)
    shutil.copy2(binary, temp_path)

    try:
        subprocess.run(
            ["objcopy", "--remove-section", ".note", temp_path],
            timeout=30,
            capture_output=True,
            text=True,
            check=True,
        )
    except FileNotFoundError:
        logger.warning(
            "objcopy not found; loading the original binary may print BFD warnings"
        )
        os.unlink(temp_path)
        return binary
    except subprocess.CalledProcessError as exc:
        logger.warning(
            "Could not strip .note from %s: %s",
            binary,
            exc.stderr.strip() if exc.stderr else exc,
        )
        os.unlink(temp_path)
        return binary

    _gdb_binary_copy = temp_path
    logger.debug("Prepared sanitized GDB symbol file: %s", temp_path)
    return temp_path


def _find_test_config() -> str:
    """Locate the domain config file for nested-svm-slave."""
    search_paths = [
        "tests/nested-svm-slave/test-hvm64-nested-svm-slave.cfg",
    ]

    for path in search_paths:
        if os.path.exists(path):
            logger.info("Found test config: %s", path)
            return path

    msg = "Could not find nested-svm-slave config. Searched: %s"
    raise FileNotFoundError(msg, search_paths)


def _create_domain() -> str:
    """Create a paused XTF slave domain using xl create.

    Returns the domain ID (domid) as a string.
    """
    logger.debug("Creating paused domain: %s", _domain_name)

    config = _find_test_config()
    logger.debug("Using config: %s", config)

    # Use xl create with config file
    cmd = ["xl", "create", "-p", config]
    logger.debug("Command: %s", " ".join(cmd))

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
        logger.debug("xl create stdout: %s", result.stdout)
        logger.debug("xl create stderr: %s", result.stderr)

        if result.returncode != 0:
            msg = "xl create failed: %s"
            raise RuntimeError(msg % result.stderr)

        # xl create doesn't always output the domain ID, so use xl list to find it
        logger.debug("Looking up domain by name: %s", _domain_name)
        result = subprocess.run(
            ["xl", "list", _domain_name],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )

        if result.returncode != 0:
            msg = "xl list failed: %s"
            raise RuntimeError(msg % result.stderr)

        lines = result.stdout.strip().split("\n")
        if len(lines) >= 2:
            # First line is header, second line has the domain info
            parts = lines[1].split()
            domid = parts[1]  # Domain ID is second column
            logger.debug("Domain created with ID: %s", domid)
            return domid

        raise RuntimeError("Could not determine domain ID after creation")

    except subprocess.TimeoutExpired as exc:
        raise RuntimeError("xl create timed out") from exc


def _start_gdbsx(domid: str) -> subprocess.Popen:  # type: ignore[type-arg]
    """Start gdbsx server for the given domain.

    Returns the gdbsx process object.
    """
    logger.debug("Starting gdbsx server for domain %s on port %s", domid, _gdbsx_port)

    cmd = ["gdbsx", "-a", domid, "64", str(_gdbsx_port)]
    logger.debug("Command: %s", " ".join(cmd))

    try:
        # pylint: disable=consider-using-with
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        logger.debug("gdbsx started with PID %s", proc.pid)

        # Give gdbsx time to bind the port
        time.sleep(0.5)

        # Check if process is still alive
        if proc.poll() is not None:
            stdout, stderr = proc.communicate()
            logger.error("gdbsx exited immediately")
            logger.error("stdout: %s", stdout)
            logger.error("stderr: %s", stderr)
            msg = "gdbsx failed to start: %s"
            raise RuntimeError(msg % stderr)

        logger.debug("gdbsx is listening on port %s", _gdbsx_port)
        return proc

    except FileNotFoundError as exc:
        msg = "gdbsx not found. Install Xen debugging tools or set PATH correctly."
        raise RuntimeError(msg) from exc


def _cleanup_domain_and_gdbsx() -> None:
    """Clean up domain and gdbsx on exit."""
    global _gdbsx_proc, _domain_pid, _gdb_binary_copy

    if _gdbsx_proc:
        logger.info("Terminating gdbsx (PID %s)", _gdbsx_proc.pid)
        try:
            _gdbsx_proc.terminate()
            _gdbsx_proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            logger.warning("gdbsx did not terminate, killing")
            _gdbsx_proc.kill()
            _gdbsx_proc.wait()
        _gdbsx_proc = None

    if _domain_pid:
        logger.info("Destroying domain %s", _domain_pid)
        try:
            subprocess.run(
                ["xl", "destroy", str(_domain_pid)],
                timeout=10,
                capture_output=True,
                check=False,
            )
        except subprocess.TimeoutExpired:
            logger.warning("xl destroy timed out")
        except Exception as exc:  # pylint: disable=broad-except
            logger.warning("Error destroying domain: %s", exc)
        _domain_pid = None

    # Also try to destroy by name in case PID wasn't captured
    try:
        subprocess.run(
            ["xl", "destroy", _domain_name],
            timeout=10,
            capture_output=True,
            check=False,
        )
    except Exception as exc:  # pylint: disable=broad-except
        logger.debug("Could not destroy by name: %s", exc)

    if _gdb_binary_copy:
        try:
            os.unlink(_gdb_binary_copy)
        except OSError as exc:
            logger.debug(
                "Could not remove sanitized GDB binary %s: %s",
                _gdb_binary_copy,
                exc,
            )
        _gdb_binary_copy = None


class VMCBWrapper:
    """Expose property-style reads and writes to DWARF-resolved VMCB fields."""

    def __init__(self, symbol_name: str = "l1_vmcb") -> None:
        self._symbol = symbol_name
        logger.debug("Initialized VMCBWrapper for symbol: %s", symbol_name)

    def _find_field_expr(self, field: str) -> str:
        """Find the correct GDB expression for accessing a VMCB field.

        Tries multiple patterns to match pointer vs struct notation and
        nested structure access paths.
        """
        patterns = (
            f"{self._symbol}->{field}",
            f"{self._symbol}->control.{field}",
            f"{self._symbol}->save.{field}",
            f"{self._symbol}.{field}",
            f"{self._symbol}.control.{field}",
            f"{self._symbol}.save.{field}",
        )
        for pattern in patterns:
            try:
                gdb.parse_and_eval(pattern)
                logger.debug("Found field expression for %s: %s", field, pattern)
                return pattern
            except gdb.error:
                continue
        msg = "Field '%s' not found in VMCB (tried: %s)"
        raise AttributeError(msg % (field, patterns))

    def __getattr__(self, name: str) -> int:
        expr = self._find_field_expr(name)
        value = int(gdb.parse_and_eval(expr))
        logger.debug("Read %s = 0x%x", name, value)
        return value

    def __setattr__(self, name: str, value: Any) -> None:
        if name.startswith("_"):
            super().__setattr__(name, value)
            return
        expr = self._find_field_expr(name)
        logger.debug("Writing %s = 0x%x via %s", name, value, expr)
        gdb.parse_and_eval(f"{expr} = {value}")


class L1GuestController:
    """Drive commands on the passive XTF nested-SVM slave."""

    def __init__(self, vmcb_symbol: str = "l1_vmcb") -> None:
        self.vmcb = VMCBWrapper(vmcb_symbol)

    def run_command(self, instruction: str, arg: int = 0) -> int:
        """Execute a privileged instruction on the slave and return status.

        Args:
            instruction: Command name (CLGI, STGI, VMRUN, etc.)
            arg: Optional argument for the command

        Returns:
            Status code from the guest
        """
        global _shutdown_requested

        if _shutdown_requested:
            raise RuntimeError("Shutdown requested, aborting command")

        commands = {"CLGI": 1, "STGI": 2, "VMRUN": 3}
        if instruction not in commands:
            raise ValueError(f"Unknown command: {instruction}")

        logger.debug("Executing command: %s (arg=%s)", instruction, arg)

        try:
            gdb.execute(f"set current_cmd = {commands[instruction]}", to_string=True)
            gdb.execute(f"set cmd_arg = {arg}", to_string=True)

            logger.debug("Resuming guest for %s", instruction)
            _continue_until_slave_trap(f"{instruction} command", timeout=10, attempts=4)

            status = int(gdb.parse_and_eval("cmd_status"))
            logger.debug("Command %s returned status: %s", instruction, status)
            return status

        except KeyboardInterrupt:
            logger.warning("Command interrupted by user")
            _shutdown_requested = True
            raise

    @property
    def vgif_state(self) -> int:
        """Return the vGIF state (bit 25 of vintr field)."""
        vintr = self.vmcb.vintr
        state = (vintr >> 25) & 1
        logger.debug("Read vGIF state: %s (vintr=0x%x)", state, vintr)
        return state


@pytest.fixture(scope="session")
def xtf_slave_session() -> Generator[L1GuestController, None, None]:
    """Boot the XTF nested-SVM slave and attach GDB for the session.

    This fixture is session-scoped because booting a domain is expensive.
    The slave remains paused and ready for commands throughout the test run.
    """
    global _domain_pid, _gdbsx_proc, _shutdown_requested

    if gdb is None:
        pytest.fail("These tests must run inside GDB's Python interpreter")

    # Install signal handlers for clean Ctrl-C handling
    _install_signal_handlers()

    logger.debug("=" * 70)
    logger.debug("STARTING XTF SLAVE SESSION")
    logger.debug("=" * 70)

    try:
        # Ensure cleanup on process exit
        atexit.register(_cleanup_domain_and_gdbsx)

        # Clean up any stale domains first
        logger.debug("Cleaning up stale domains")
        result = subprocess.run(
            ["xl", "destroy", _domain_name],
            timeout=10,
            capture_output=True,
            check=False,
        )
        if result.returncode == 0:
            logger.info("Destroyed stale domain: %s", _domain_name)
        else:
            logger.debug("No stale domain found: %s", _domain_name)

        # Create domain
        logger.debug("PHASE 1: Domain creation")
        domid = _create_domain()
        _domain_pid = domid

        # Start gdbsx
        logger.debug("PHASE 2: Starting gdbsx")
        _gdbsx_proc = _start_gdbsx(domid)

        # Attach GDB
        logger.debug("PHASE 3: Attaching GDB")

        # Load binary symbols for DWARF access
        binary = _find_test_binary()
        logger.debug("Loading symbols from: %s", binary)
        gdb_binary = _prepare_gdb_binary(binary)
        if gdb_binary != binary:
            logger.debug("Using sanitized GDB symbol file: %s", gdb_binary)
        gdb.execute(f"file {gdb_binary}")
        logger.debug("Symbols loaded")

        logger.debug("Connecting to localhost:%s", _gdbsx_port)
        gdb.execute(f"target remote localhost:{_gdbsx_port}")
        logger.debug("GDB target connected")
        gdb.execute("set pagination off", to_string=True)
        gdb.execute("set confirm off", to_string=True)
        gdb.execute("set remotetimeout 5", to_string=True)
        _install_slave_trap_breakpoint()

        # Unpause the domain so it starts executing
        logger.debug("Unpausing domain for execution")
        result = subprocess.run(
            ["xl", "unpause", _domain_name],
            timeout=5,
            capture_output=True,
            check=False,
        )
        if result.returncode == 0:
            logger.debug("Domain unpaused")
        else:
            logger.debug(
                "Unpause attempt: %s", result.stderr.decode("utf-8", errors="ignore")
            )

        # Continue to first breakpoint (int3 in test_main)
        logger.debug("Continuing to first breakpoint")
        output = _continue_until_slave_trap("initial slave startup", timeout=20)
        logger.debug("Continue output: %s", output)
        logger.debug("Slave is now paused and ready for commands")

        logger.debug("=" * 70)
        logger.debug("XTF SLAVE SESSION READY")
        logger.debug("=" * 70)

        yield L1GuestController()

    except KeyboardInterrupt:
        logger.warning("Session interrupted by user")
        _shutdown_requested = True
        _cleanup_domain_and_gdbsx()
        raise

    except Exception as exc:  # pylint: disable=broad-except
        logger.error("Failed to initialize slave session: %s", exc, exc_info=True)
        _cleanup_domain_and_gdbsx()
        raise

    finally:
        logger.info("Cleaning up slave session")
        try:
            gdb.execute("disconnect", to_string=True)
        except Exception as exc:  # pylint: disable=broad-except
            logger.warning("Error disconnecting GDB: %s", exc)
        _cleanup_domain_and_gdbsx()


@pytest.fixture(scope="function")
def xtf_fast_rewind(
    xtf_slave_session: L1GuestController,  # pylint: disable=redefined-outer-name
) -> Generator[L1GuestController, None, None]:
    """Restore the L1 VMCB and command state between tests.

    This function-scoped fixture snapshots the VMCB at the start of the test
    and restores it afterward, allowing fast tests to reuse the same slave
    without rebooting the domain.
    """
    controller = xtf_slave_session
    inferior = gdb.selected_inferior()

    logger.debug("Snapshotting L1 VMCB for test reset")
    vmcb_addr = int(gdb.parse_and_eval("&l1_vmcb"))
    pristine_vmcb = inferior.read_memory(vmcb_addr, 4096)

    yield controller

    logger.debug("Restoring L1 VMCB and command state")
    inferior.write_memory(vmcb_addr, pristine_vmcb.tobytes())
    gdb.execute("set current_cmd = 0", to_string=True)
    gdb.execute("set cmd_arg = 0", to_string=True)
    gdb.execute("set cmd_status = 0", to_string=True)
