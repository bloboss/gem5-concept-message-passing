/*
 * Copyright (c) 2025 The gem5 Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file gem5_extension.cc
 *
 * Extension module entry point for building _m5 as an importable Python
 * shared library (e.g. _m5.so / _m5.pyd).
 *
 * CONVENTIONAL (embedded) MODEL
 * ==============================
 * src/sim/main.cc starts a pybind11 embedded interpreter, installs the
 * compressed-bytecode importer, and calls m5.main().  The _m5 pybind11
 * module is pre-registered via PyImport_AppendInittab() so it is
 * available as a built-in once Py_Initialize() runs.
 *
 * LIBRARY MODEL (this file)
 * ==========================
 * Python drives execution.  The user's script does:
 *
 *   import _m5               # loads _m5.so, calls PyInit__m5 here
 *   import m5
 *   import gem5
 *   ...
 *   root = Root(full_system=False, system=board)
 *   gem5.run(root)           # instantiate C++ objects and simulate
 *
 * PyInit__m5 is the standard C-level entry point that Python's import
 * machinery looks for in a shared library.  On return, all _m5
 * submodules are available:
 *
 *   _m5.core      – curTick, setClockFrequency, serializeAll, ...
 *   _m5.drain     – DrainManager, DrainState
 *   _m5.event     – simulate(), exitSimLoop(), PyEvent, ...
 *   _m5.stats     – updateEvents, ...
 *   _m5.port      – port connection helpers
 *   _m5.param_*   – one submodule per SimObject type (generated at build
 *                   time by the SCons param generation step)
 *
 * BUILD INSTRUCTIONS
 * ==================
 * Compile the gem5 C++ tree with -DGEM5_EXTENSION_MODULE.  This flag
 * suppresses the conflicting GEM5_PYBIND_MODULE_INIT(_m5, ...) static
 * initialiser in src/sim/init.cc (which targets the embedded build) and
 * compiles this file instead of src/sim/main.cc.
 *
 * The SCons invocation would look roughly like:
 *
 *   scons build/ALL/gem5_extension.so    \
 *         EXTRAS=src/sim/gem5_extension.cc   \
 *         GEM5_EXTENSION_MODULE=1
 *
 * See pyproject.toml at the repo root for the Python packaging side.
 */

#include "pybind11/pybind11.h"

#include "sim/init.hh"
#include "sim/init_signals.hh"

namespace py = pybind11;

/**
 * PyInit__m5 – standard Python extension entry point.
 *
 * Python calls this function when the user writes `import _m5`.
 * The PYBIND11_MODULE macro expands to define PyInit__m5 with
 * extern "C" linkage so the dynamic linker can find it.
 *
 * Responsibilities that main.cc normally handles are moved here:
 *   1. Signal handler setup (SIGSEGV → panic, SIGFPE → panic, etc.)
 *   2. Registration of all pybind11 submodules under the _m5 namespace.
 *
 * The embedded-bytecode importer (importer.cc / embedded.cc) is NOT
 * loaded here.  In the library model, m5/ and gem5/ are regular Python
 * packages on the interpreter's sys.path, importable without any custom
 * machinery.
 */
PYBIND11_MODULE(_m5, m)
{
    // Signal handlers that were previously registered in main.cc.
    // Without these, a segfault inside the simulator produces no
    // diagnostic output instead of the usual gem5 panic message.
    gem5::initSignals();

    // Register every pybind11 submodule under _m5.
    //
    // initAll() calls pybind_init_core(), pybind_init_debug(),
    // pybind_init_event(), pybind_init_stats(), pybind_init_port(), and
    // then drains the EmbeddedPyBind pending queue.  The pending queue
    // holds all _m5.param_<Type> submodules that SimObject C++ translation
    // units registered at static-initialisation time via EmbeddedPyBind
    // constructors.  They are initialised in dependency order here.
    gem5::EmbeddedPyBind::initAll(m);
}
