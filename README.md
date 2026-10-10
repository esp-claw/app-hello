# app-hello

Example esp-claw WASM app: echo, benchmark, key-value demo and a runaway-protection test.

A minimal app that shows how to register tools, return results and use the per-app key-value store. Its `loop` tool runs forever on purpose, to test that the runtime aborts runaway calls.

## Build

```sh
git clone --recursive https://github.com/esp-claw/app-hello.git
cd app-hello && ./sdk/build-app.sh .
```

This needs clang with the wasm32 target and wasm-ld (on Debian/Ubuntu: `apt install clang lld`). The output is `build/app.wasm` and `build/manifest.json`. CI builds every push and attaches both files to releases.

## Install

Copy both files to `/apps/hello/` on the device, for example over WebDAV (`dav://<device>/dav/apps/hello/`). Then send `/apps reload hello` in Telegram, or restart the device.

**Status:** works with runtime v1 in [esp-claw/esp-claw](https://github.com/esp-claw/esp-claw). Packages are not signed yet; signing arrives with the installer.

Part of [esp-claw (community)](https://github.com/esp-claw), an unofficial community fork of espressif/esp-claw that is not affiliated with Espressif.

## License

Apache-2.0
