#*********************************************************************************
#  patch_tft_espi_s3_dma.py  -  Fixes a stock TFT_eSPI 2.5.43 bug on ESP32-S3.
#
#  WHAT AND WHY.  TFT_eSPI::initDMA() registers dma_end_callback() as the SPI
#  transaction's post_cb, so it runs after every DMA push - i.e. after every
#  LvglPort::flush() on this board.  The stock ESP32-S3 processor file does:
#
#      WRITE_PERI_REG(SPI_DMA_CONF_REG(spi_host), 0);
#
#  SPI_DMA_CONF_REG(i) assumes classic ESP32's per-peripheral-integrated DMA
#  register layout.  The ESP32-S3 uses a separate GDMA controller instead, so
#  that macro/register pairing does not address what the stock code intends -
#  the callback writes to the wrong place on every completed DMA frame.
#
#  The vendor's own kit ships a patched Processors/TFT_eSPI_ESP32_S3.c fixing
#  exactly this (cyd-dev MCP: cyd_gotchas board=esp32s3-es3c28p topic=display,
#  "use the patched files if you hit init glitches").  Confirmed by diffing our
#  installed copy against the vendor's: the ONLY functional difference in the
#  whole 899-line file is this one callback.  Also confirmed against a working
#  non-DMA reference project on the same physical board (which never exercises
#  this path and never hit the bug), while our firmware - which deliberately
#  uses DMA for the overlapped flush LvglPort.cpp documents - went blank white.
#
#  WHY A SCRIPT AND NOT A HAND EDIT.  bodmer/TFT_eSPI is pinned in lib_deps and
#  lives in .pio/libdeps, which is regenerated on a clean checkout and is NOT
#  committed - a hand edit there would vanish the moment someone else clones
#  this repo.  This script re-applies the one-line fix every time the library
#  is (re)fetched, so `pio run -e esp32s3_es3c28p` is correct on a machine that
#  has never built this project before, not just on this one.
#
#  WHY post: AND NOT pre:.  This project already hit the opposite mistake once
#  - see the platformio.ini comment on why the old copy_user_setup.py pre:
#  script is gone: it ran BEFORE PlatformIO's library manager had fetched the
#  library on a clean checkout, so the target file did not exist yet and the
#  script silently did nothing.  Patch logic here runs at plain top-level
#  (import) scope in a post: script instead, which executes after lib_deps for
#  this environment have been resolved and downloaded.
#
#  FAILS LOUDLY, ON PURPOSE.  If the target file or the exact buggy line ever
#  goes missing (a TFT_eSPI upgrade, a different install layout) and the fix is
#  not already applied either, this script prints why and aborts the build via
#  env.Exit() rather than continuing - a silently-unpatched build would boot a
#  board with a blank screen and no clue why, which is a far worse failure than
#  a build stopping next to this explanation.  Same philosophy as the board
#  contract in include/Pins.h.  (Deliberately NOT a bare `raise`/`sys.exit()`:
#  PlatformIO's extra_scripts loader has been observed to swallow SystemExit
#  from a script silently - see the `fail()` helper below.)
#*********************************************************************************
Import("env")

import os

TARGET_RELATIVE = os.path.join("TFT_eSPI", "Processors", "TFT_eSPI_ESP32_S3.c")

BUGGY_LINE = "  WRITE_PERI_REG(SPI_DMA_CONF_REG(spi_host), 0);\n"

FIXED_LINES = (
    "  //WRITE_PERI_REG(SPI_DMA_CONF_REG(spi_host), 0);\n"
    "  WRITE_PERI_REG( SPI_DMA_CONF_REG(SPI_DMA_CH_AUTO), 0b11);\n"
)

#  NOT bare "SPI_DMA_CH_AUTO" - that identifier already appears, unpatched, a
#  few lines above the bug (`#define DMA_CHANNEL SPI_DMA_CH_AUTO`), so using it
#  alone as the "already fixed" marker false-positived on a completely stock
#  file and made this script silently skip every time. This exact call is only
#  ever produced by the fix.
ALREADY_FIXED_MARKER = "SPI_DMA_CONF_REG(SPI_DMA_CH_AUTO)"


def fail(message):
    #  Do not use a bare `raise` / `sys.exit()` here: PlatformIO's extra_scripts
    #  loader has been observed to swallow SystemExit from a script silently,
    #  which is exactly the "looks like it built, quietly did not apply the
    #  patch" failure this script exists to prevent. env.Exit() is PlatformIO's
    #  own API for actually aborting the build with a visible, non-zero result.
    print("patch_tft_espi_s3_dma.py: " + message)
    env.Exit(1)


def patch_tft_espi_s3_dma():
    libdeps_dir = env.subst("$PROJECT_LIBDEPS_DIR")
    env_name = env["PIOENV"]
    target_path = os.path.join(libdeps_dir, env_name, TARGET_RELATIVE)

    if not os.path.isfile(target_path):
        fail(
            "expected to find %s (TFT_eSPI not fetched yet for env '%s'?) - "
            "this script must run as a post: extra_script, after lib_deps are "
            "resolved, not pre:." % (target_path, env_name)
        )
        return

    with open(target_path, "r") as f:
        content = f.read()

    if ALREADY_FIXED_MARKER in content:
        return  # already patched by a previous run - nothing to do

    if BUGGY_LINE not in content:
        fail(
            "neither the known-buggy line nor the fix marker was found in %s. "
            "TFT_eSPI may have changed this file - re-diff against the "
            "vendor's patched copy (cyd-dev MCP: cyd_get_file 'arduino/"
            "Replaced files/TFT_eSPI_ESP32_S3.c') and update "
            "BUGGY_LINE/FIXED_LINES here." % target_path
        )
        return

    content = content.replace(BUGGY_LINE, FIXED_LINES)

    with open(target_path, "w") as f:
        f.write(content)

    print("patch_tft_espi_s3_dma.py: patched ESP32-S3 DMA end-callback bug in %s" % target_path)


patch_tft_espi_s3_dma()
