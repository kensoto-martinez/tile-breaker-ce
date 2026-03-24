# ----------------------------
# Makefile Options
# ----------------------------

NAME = Tilebrkr
DESCRIPTION = "Break tiles using upgradable balls!"
COMPRESSED = NO

CFLAGS = -Wall -Wextra -Oz
CXXFLAGS = -Wall -Wextra -Oz

# ----------------------------

include $(shell cedev-config --makefile)
