# ETQW compiled scripts

Kept from the reconstruction; not built. The Meson build has no compiled-script target: OpenETQW interprets the scripts from the retail paks, where retail 1.5 ran them as compiled-script modules.

- `scripts/` matches the retail 1.5 script tree, apart from the `scripts/` include prefix in 7 files (`docs/provenance.md` §2.2 in the engine repository).
- `scripts/events.script` is the reference for `tools/ci/check_event_handlers.py`, which checks every declaration against the game's native event definitions without retail data.
- The CMake project and `tools/CompiledScriptCodegen.cpp` are jmarshall's code generator for the compiled-script modules; they are not used.
