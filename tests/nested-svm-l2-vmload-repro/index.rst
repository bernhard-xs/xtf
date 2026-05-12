Nested SVM L2 VMLOAD Reproducer
===============================

This is a minimized Xen nested-SVM reproducer, not a normal regression test.

Why This Exists
---------------

While developing L2 negative coverage for nested SVM, Xen was observed to kill
the guest instead of cleanly reflecting guest-visible faults when an L2 guest
executed SVM instructions.  This reproducer keeps only the smallest practical
subset of that setup so the hypervisor-side behaviour can be debugged in
isolation.

What It Does
------------

L1 enables SVM, builds a minimal long-mode L2 VMCB, enters L2 once, and has
L2 execute a single ``VMLOAD`` using an aligned operand in ``RAX``.

If Xen handles the nested path correctly, L2 reaches the instruction after
``VMLOAD``, records that progress in shared memory, halts, and L1 reports
success.

If Xen still exhibits the observed bug, the domain may crash or Xen may log
diagnostics such as failed MMIO emulation on the ``VMLOAD`` opcode.

How To Use It
-------------

Run the test directly by name when debugging Xen nested-SVM behaviour.  It is
kept in the ``special`` category so it stays out of the default test sweep.

Correlate the guest outcome with ``xl dmesg`` output immediately afterwards to
inspect Xen's nested-SVM diagnostics.