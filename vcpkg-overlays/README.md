ABM AV Player - targeted libjuice vcpkg overlay

This overlay replaces only the libjuice port with upstream Microsoft vcpkg's
libjuice 1.7.4 port definition. It does not change builtin-baseline or the
other dependency versions.

Install:
1. Close any build or vcpkg install currently running.
2. Back up any existing vcpkg-configuration.json before replacing it.
   If one already exists, merge the overlay-ports entry instead of overwriting it.
3. Extract the contents of this archive into the ABMAVPlayer repository root.
   The resulting paths must be:
     vcpkg-configuration.json
     vcpkg-overlays\libjuice\vcpkg.json
     vcpkg-overlays\libjuice\portfile.cmake
     vcpkg-overlays\libjuice\dependencies.diff
4. From a Visual Studio Developer Command Prompt, run:
   vcpkg install --debug
   or use the full Visual Studio vcpkg.exe path already used for this project.

The port uses the SHA512 published in Microsoft's current vcpkg libjuice port.
It intentionally does not accept the unexpected SHA512 reported for libjuice
1.7.1. The overlay is project-local; it leaves vcpkg.json's builtin-baseline
unchanged.

Note: libjuice 1.7.4 declares MPL-2.0 in the current vcpkg port, whereas the
old 1.7.1 port declared LGPL-2.1-only. Review the licence change before
redistributing binaries. This overlay is a dependency-build workaround, not
a legal determination.
