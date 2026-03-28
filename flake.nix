{
  description = "Development environment for the gem5 simulator";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachSystem [ "x86_64-linux" "aarch64-linux" ] (system:
      let
        pkgs = import nixpkgs { inherit system; };

        # Python environment used both as the gem5 embedded interpreter and
        # (via PYTHON_CONFIG) for SCons to locate headers and libraries.
        # scons itself is a standalone package below; the two share the same
        # Python version so there are no interpreter mismatches.
        pythonEnv = pkgs.python3.withPackages (ps: with ps; [
          pydot   # used by gem5 config scripts for dot-graph output
          mypy    # static type checking for development
        ]);
      in
      {
        devShells.default = pkgs.mkShell {
          name = "gem5";

          packages = with pkgs; [
            # ── Compiler & core build tools ─────────────────────────────────
            gcc
            m4
            pkg-config
            scons       # build system (standalone; shares Python via PYTHON_CONFIG)

            # Python environment for gem5 embedding + config scripts
            pythonEnv

            # ── Required libraries ───────────────────────────────────────────
            zlib        # compression; build fails without this

            # ── Optional: trace capture / playback ──────────────────────────
            # Enables HAVE_PROTOBUF; required for TraceCPU, elastic traces,
            # traffic generator traces, and memory probes.
            protobuf    # libprotobuf + protoc compiler
            grpc        # gRPC protobuf support (GrpcProtoBuf builder)

            # ── Optional: ~12 % runtime performance improvement ──────────────
            # SCons checks for tcmalloc_minimal / tcmalloc via CheckLib.
            gperftools

            # ── Optional: HDF5 statistics backend ───────────────────────────
            # Enables HAVE_HDF5; needed for src/base/stats/hdf5.cc.
            hdf5        # built with C++ bindings in nixpkgs

            # ── Optional: Capstone disassembly ──────────────────────────────
            # Enables USE_CAPSTONE; used by ARM and generic disassemblers.
            capstone

            # ── Optional: PNG image output ───────────────────────────────────
            # Enables HAVE_PNG; used by framebuffer / display devices.
            libpng

            # ── Optional: ELF parsing ────────────────────────────────────────
            libelf

            # ── Optional: Boost ──────────────────────────────────────────────
            boost

            # ── Development utilities ────────────────────────────────────────
            git
            cmake   # needed by some ext/ subprojects
            doxygen
          ];

          shellHook = ''
            # Point gem5's SConstruct at the Nix-managed Python so header /
            # library detection and the embedded interpreter are consistent.
            export PYTHON_CONFIG="${pythonEnv}/bin/python3-config"

            echo ""
            echo "gem5 dev shell — optional feature status:"
            check() {
              if command -v "$2" &>/dev/null || pkg-config --exists "$3" 2>/dev/null; then
                echo "  [x] $1"
              else
                echo "  [ ] $1  <-- NOT found; feature will be disabled by SCons"
              fi
            }
            check "protobuf (TraceCPU, elastic traces)"  protoc        "protobuf"
            check "grpc     (GrpcProtoBuf builder)"      grpc_cpp_plugin "grpc"
            check "tcmalloc (12% perf improvement)"      ""            "libtcmalloc_minimal"
            check "HDF5     (stats backend)"             ""            "hdf5"
            check "Capstone (disassembly)"               ""            "capstone"
            check "libpng   (framebuffer output)"        ""            "libpng"
            check "libelf   (ELF parsing)"               ""            "libelf"
            echo ""
            echo "Build with: scons build/<ISA>/gem5.<opt|debug|fast> -j\$(nproc)"
            echo "  ISA options: X86  RISCV  ARM  ALL  (and more in build_opts/)"
            echo ""
          '';
        };
      }
    );
}
