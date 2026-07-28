"""
Make each build environment flash into its own app partition, and keep the
uploader from hijacking the boot register while it does so.

Two things are fixed up here.

1. The flash offset. PlatformIO derives it from the partition table by taking
   the *first* app partition (here: factory @ 0x10000). That is right for the
   launcher and wrong for everything else. Set `custom_app_offset` in an env and
   this script points the uploader there.

2. otadata. The Arduino platform unconditionally flashes `boot_app0.bin` to
   0xe000 on every upload. That blob is an otadata image with ota_seq = 1, which
   tells the bootloader to run ota_0 -- so uploading *any* app would leave the
   device booting the CSI slot. We drop it, and for the launcher env write a
   blank otadata instead: erased otadata sends the bootloader to `factory`,
   which is exactly where the launcher lives.

   Net effect: `pio run -e menu -t upload` lands you in the launcher, and
   uploading an app leaves the boot register alone so you stay wherever you were.
"""

import os

Import("env")  # noqa: F821  (injected by SCons)

OTADATA_OFFSET = "0xe000"
OTADATA_SIZE = 8192


def _blank_otadata(build_dir):
    """An erased otadata partition -- makes the bootloader choose `factory`."""
    path = os.path.join(build_dir, "otadata_blank.bin")
    if not os.path.isfile(path) or os.path.getsize(path) != OTADATA_SIZE:
        with open(path, "wb") as fh:
            fh.write(b"\xff" * OTADATA_SIZE)
    return path


def _strip_boot_app0(flags):
    """Remove the `0xe000 boot_app0.bin` pair from the esptool argument list."""
    out = []
    for flag in flags:
        if "boot_app0.bin" in str(flag):
            if out and str(out[-1]) == OTADATA_OFFSET:
                out.pop()  # drop the offset that preceded it
            continue
        out.append(flag)
    return out


offset = env.GetProjectOption("custom_app_offset", "").strip()
reset_otadata = env.GetProjectOption("custom_reset_otadata", "").strip().lower()

if offset:
    previous = str(env.get("ESP32_APP_OFFSET", ""))
    env.Replace(ESP32_APP_OFFSET=offset)

    flags = env.get("UPLOADERFLAGS")
    if flags:
        # Belt and braces: some platform versions bake the offset straight into
        # UPLOADERFLAGS rather than leaving it as a variable in UPLOADCMD.
        if previous:
            flags = [offset if str(f) == previous else f for f in flags]

        flags = _strip_boot_app0(flags)

        if reset_otadata in ("1", "yes", "true"):
            flags += [OTADATA_OFFSET, _blank_otadata(env.subst("$BUILD_DIR"))]

        env.Replace(UPLOADERFLAGS=flags)

    print(
        "app_offset: %s -> flashing to %s, otadata %s"
        % (
            env["PIOENV"],
            offset,
            "reset to factory" if reset_otadata in ("1", "yes", "true") else "untouched",
        )
    )
