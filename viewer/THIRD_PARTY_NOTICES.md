# Third-Party Notices

## wirestead-python

The web gateway (`packet-probe-web`) uses wirestead-python as its IPC transport
dependency. wirestead-python binds the wirestead C++ communication library and its
wheels include native extension modules and runtime libraries. It is not vendored in
this repository; it is installed as an external runtime dependency.

The gateway otherwise uses only the Python standard library, and the browser UI
(`packet_probe_viewer/web/index.html`) uses no third-party scripts, styles, or fonts.

## Packaging note

If you build a standalone distribution that bundles wirestead-python, review and
include the license notices required by wirestead and its dependencies (Boost,
spdlog, fmt) for that distribution format.
