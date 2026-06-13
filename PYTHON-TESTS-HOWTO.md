"""Running the Python/GDB Nested-Virt Tests

The fixtures now include comprehensive logging and actual domain/gdbsx lifecycle management.

## Prerequisites

You need a Dom0 or other Xen host with:
- Python 3.x with GDB Python bindings
- gdbsx installed (Xen debugging tools)
- xl command available
- Compiled test-hvm64-nested-svm-slave binary

## Building the Slave

First, ensure the tests are built:

    make

This will compile the RPC loop binary that GDB will control.

## Running the Tests

Launch pytest inside GDB's Python interpreter:

    gdb -q -batch \\
      -ex "python import sys, pytest; sys.exit(pytest.main(['-v', '-s', '-k', 'clgi_stgi', 'python-tests']))"

The `-s` flag disables output capture so you see both pytest and logging output.
The `-k` flag selects specific tests (e.g., 'clgi_stgi' for the CLGI/STGI tests).

## What Happens

When you run the tests, the verbose logging will show:

1. **PHASE 1: Domain creation**
   - Finds the compiled test-hvm64-nested-svm-slave binary
   - Runs "xl create -p" to create a paused domain
   - Extracts the domain ID

2. **PHASE 2: Starting gdbsx**
   - Starts gdbsx server on localhost:9999
   - Waits 0.5s for the server to bind
   - Verifies gdbsx is still alive

3. **PHASE 3: Attaching GDB**
   - Connects GDB to localhost:9999
   - Runs "continue" to reach the first int3 breakpoint
   - Signals readiness for test commands

4. **Test execution**
   - Each test snapshots the L1 VMCB
   - Executes commands (CLGI, STGI) via the RPC loop
   - Reads VMCB fields via DWARF
   - Restores VMCB state after each test

5. **Cleanup**
   - Disconnects GDB
   - Terminates gdbsx
   - Destroys the domain

## Troubleshooting

If gdbsx fails to connect:

- **"gdbsx not found"**: Install Xen debugging tools or set PATH
- **"xl create failed"**: Check that the binary exists and the hypervisor is working
- **GDB connection timeout**: Verify gdbsx is running and listening on :9999
- **Domain not paused**: Check that xl create -p works on your system

The DEBUG-level logging will show all subprocess commands and their output.
"""
