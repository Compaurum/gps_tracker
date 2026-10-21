# PlatformIO post-script for the USB-UART bridge envs.
#
# The espressif32 builder honours board_upload.before_reset/after_reset for `upload`, but its
# `erase` target passes only --chip and --port, so esptool falls back to toggling DTR/RTS.
# On the ESP32-C3 bridge that resets the C3 itself into its own download mode, and esptool then
# fails with "This chip is ESP32-C3, not ESP32". Make erase leave the lines alone too; the
# T-Call is put into download mode by hand (BOOT + RST).
Import("env")  # noqa: F821

env.Append(ERASEFLAGS=["--before", "no_reset", "--after", "no_reset"])  # noqa: F821
