# ----------------------------
# Makefile Options
# ----------------------------

NAME = Tilebrkr
DESCRIPTION = "Break some tiles!"
COMPRESSED = NO

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# ----------------------------

include $(shell cedev-config --makefile)
