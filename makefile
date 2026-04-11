# ----------------------------
# Makefile Options
# ----------------------------

NAME = tilebrkr
DESCRIPTION = "Break some tiles!"
ICON = icon.png
COMPRESSED = YES
COMPRESSED_MODE = zx0
ARCHIVED = YES
PREFER_OS_CRT = YES
LTO = NO

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# ----------------------------

include $(shell cedev-config --makefile)
