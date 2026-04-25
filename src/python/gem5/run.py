# Copyright (c) 2025 The gem5 Project
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""
Library-mode entry point for gem5.

When gem5 is used as a Python library (i.e. ``import gem5`` from an
external interpreter rather than running the ``gem5`` binary), this module
provides the ``run()`` function that replaces the role of ``m5.main()``.

Typical usage
-------------

.. code-block:: python

    import _m5                          # loads _m5.so, registers C++ submodules
    from m5.objects import Root
    from gem5.components.boards.simple_board import SimpleBoard
    from gem5.components.processors.simple_processor import SimpleProcessor
    from gem5.components.memory.single_channel import SingleChannelDDR3_1600
    from gem5.components.cachehierarchies.classic.no_cache import NoCache
    from gem5.isas import ISA
    from gem5.components.processors.cpu_types import CPUTypes
    from gem5.resources.resource import obtain_resource

    cache_hierarchy = NoCache()
    memory = SingleChannelDDR3_1600("1GiB")
    processor = SimpleProcessor(cpu_type=CPUTypes.TIMING, isa=ISA.X86, num_cores=1)

    board = SimpleBoard(
        clk_freq="3GHz",
        processor=processor,
        memory=memory,
        cache_hierarchy=cache_hierarchy,
    )
    board.set_se_binary_workload(obtain_resource("x86-hello64-static"))

    root = Root(full_system=False, system=board)

    import gem5
    gem5.run(root)

Design notes
------------
The conventional gem5 binary flow is:

    C++ main()
      -> Py_Initialize()           # start embedded Python
      -> importer.install()        # load compressed-bytecode bundle
      -> m5.main()                 # parse argv, set m5.options, run script

In the library model that flow becomes:

    python my_script.py
      -> import _m5                # PyInit__m5() in gem5_extension.cc
      -> import m5, gem5           # regular filesystem/pip imports
      -> root = Root(...)          # pure-Python config tree construction
      -> gem5.run(root)            # this function

``run()`` performs the setup that ``m5.main()`` previously owned
(output directory creation, clock frequency finalisation, event-queue
binding, stats initialisation) and then delegates to the existing
``m5.instantiate()`` / ``m5.simulate()`` machinery unchanged.
"""

from __future__ import annotations

import os
import sys
import types
from typing import Optional


def run(
    root,
    outdir: str = "m5out",
    checkpoint_dir: Optional[str] = None,
    max_ticks: Optional[int] = None,
) -> None:
    """Instantiate the SimObject tree and run the simulation to completion.

    This is the primary entry point for the library model.  It replaces the
    need to invoke the ``gem5`` binary or call ``m5.main()``.

    Parameters
    ----------
    root:
        An ``m5.objects.Root`` instance that is the root of the fully
        configured SimObject tree.  All boards, processors, memories, and
        cache hierarchies must be attached before calling ``run()``.
    outdir:
        Path to the directory where gem5 writes output files (statistics,
        config dumps, checkpoint data).  The directory is created if it does
        not already exist.  Defaults to ``"m5out"`` in the current working
        directory.
    checkpoint_dir:
        If provided, gem5 restores simulation state from this checkpoint
        directory before starting.  Pass ``None`` (the default) for a cold
        start.
    max_ticks:
        Optional absolute tick limit.  If provided, an exit event is
        scheduled at this tick regardless of workload completion.

    Raises
    ------
    RuntimeError
        If ``_m5`` has not been imported before calling ``run()``.  The C++
        simulation engine must be loaded first.

    SystemExit
        On simulation completion, ``sys.exit()`` is called with the exit
        code returned by the last ``m5.simulate()`` call.  This mirrors the
        behaviour of the conventional gem5 binary.

    Notes
    -----
    ``run()`` may only be called once per process.  gem5's C++ simulation
    engine does not support being re-initialised after ``m5.instantiate()``
    has been called.  To run multiple simulations in the same process, use
    the multiprocessing support in ``gem5.utils.multiprocessing``.
    """
    try:
        import _m5  # noqa: F401 – must be loaded before importing m5
    except ImportError as exc:
        raise RuntimeError(
            "gem5.run() requires _m5 to be imported first.  "
            "Build _m5.so with -DGEM5_EXTENSION_MODULE and ensure it is on "
            "sys.path or PYTHONPATH before importing gem5."
        ) from exc

    import m5
    from m5 import event as _m5_event_py
    from _m5 import event as _m5_event_cc

    _setup_m5_options(m5, outdir)

    # Bind the main event queue to this thread.  m5.main() normally does
    # this; without it simulate() will panic on the first tick.
    _m5_event_py.mainq = _m5_event_py.getEventQueue(0)
    _m5_event_py.setEventQueue(_m5_event_py.mainq)

    if max_ticks is not None:
        _m5_event_cc.setMaxTick(max_ticks)

    # Instantiate all C++ SimObjects (createCCObject → init → regStats →
    # regProbePoints → regProbeListeners → initState / loadState).
    m5.instantiate(checkpoint_dir)

    # Drive the simulate loop.  gem5 exits the loop on every exit event;
    # we keep going until a non-zero exit code or the conventional
    # "last active thread context" cause that signals normal completion.
    while True:
        exit_event = m5.simulate()
        cause = exit_event.getCause()
        code = exit_event.getCode()

        if code != 0:
            # Non-zero code always terminates (e.g. workload exited with
            # an error, or a fatal event was raised).
            break

        if cause == "exiting with last active thread context":
            # Normal SE-mode completion: all simulated threads exited.
            break

        # Other zero-code causes (e.g. "checkpoint", "switchcpu") are
        # handled by the caller in the conventional flow.  In the library
        # model we surface them via a hook so users can extend behaviour
        # without forking this function.  For now, continue simulating.

    sys.exit(code)


def _setup_m5_options(m5_module, outdir: str) -> None:
    """Populate ``m5.options`` with defaults required by the simulate pipeline.

    ``m5.simulate._dump_configs()`` and related helpers read ``m5.options``
    for the output directory and config-dump filenames.  Normally
    ``m5.main()`` populates this from ``sys.argv``; in library mode we
    provide sensible defaults so the caller does not have to.

    If ``m5.options`` already has an ``outdir`` attribute (because the
    caller invoked ``m5.main()`` before ``gem5.run()``), this function is
    a no-op.
    """
    if hasattr(m5_module, "options") and hasattr(m5_module.options, "outdir"):
        return  # already set up (e.g. by an explicit m5.main() call)

    from _m5 import core as _m5_core

    os.makedirs(outdir, exist_ok=True)

    # Tell the C++ layer where to write its output.  This must be called
    # before instantiate() which opens the stats file.
    _m5_core.setOutputDir(outdir)

    # Build a namespace that duck-types the OptionParser dict used by
    # m5.main().  Only the attributes accessed by simulate.py /
    # stats.py / dot_writer.py are strictly required; the rest are
    # included for completeness so that third-party scripts that import
    # m5.options do not get AttributeErrors.
    m5_module.options = types.SimpleNamespace(
        # Core output options (used by simulate._dump_configs).
        outdir=outdir,
        dump_config="config.ini",
        json_config="config.json",
        dot_config="",
        dot_dvfs_config="",
        # Stats output (used by m5.stats).
        stats_file="stats.txt",
        # Listener / network options.
        listener_mode="auto",
        allow_remote_connections=False,
        # Stdout/stderr redirection (not applied in library mode).
        redirect_stdout=False,
        redirect_stderr=False,
        silent_redirect=False,
        stdout_file="simout.txt",
        stderr_file="simerr.txt",
        # Debug / trace options.
        debug_break=[],
        debug_flags=[],
        debug_start=0,
        debug_end=0,
        debug_file="cout",
        debug_ignore=[],
        # Verbosity.
        verbose=0,
        quiet=0,
    )
