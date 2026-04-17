# ----------------------------
# Makefile Options
# ----------------------------

NAME = tilebrkr
DESCRIPTION = "Break some tiles!"
ARCHIVED = YES
ICON = icon.png
# COMPRESSED = YES
# COMPRESSED_MODE = zx0
# PREFER_OS_CRT = YES
# LTO = NO

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# ----------------------------

include $(shell cedev-config --makefile)
