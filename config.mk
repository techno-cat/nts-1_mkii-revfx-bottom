##############################################################################
# Configuration for Makefile
#

PROJECT := revfx_bottom
PROJECT_TYPE := revfx

##############################################################################
# Sources
#

# C sources
UCSRC = $(wildcard ./user/*.c) $(wildcard ./user/lib/*.c)

# C++ sources
UCXXSRC = ./user/unit.cc

# List ASM source files here
UASMSRC = 

UASMXSRC = 

##############################################################################
# Include Paths
#

UINCDIR  = ./user/lib

##############################################################################
# Library Paths
#

ULIBDIR = 

##############################################################################
# Libraries
#

ULIBS  = -lm

##############################################################################
# Macros
#

UDEFS = 

