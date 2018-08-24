#!/usr/bin/env bash
. common.sh

# Stop on assertions:
# export G_DEBUG=fatal_warnings
export G_DEBUG=fatal_criticals

# Logging from all glib based libraries
export G_MESSAGES_DEBUG=all

# Check memory allocation 
export G_SLICE=debug-blocks

LD_LIBRARY_PATH=. ./tnice "$@" ${st_ip}:${st_port} 2>&1 | tee tnice_${now}.log

