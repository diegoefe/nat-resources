#!/usr/bin/env bash

# Stop on assertions:
# export G_DEBUG=fatal_warnings
export G_DEBUG=fatal_criticals

# Logging from all glib based libraries
export G_MESSAGES_DEBUG=all

# Check memory allocation 
export G_SLICE=debug-blocks

./tnice "$@" 2>&1 | tee tnice.log

