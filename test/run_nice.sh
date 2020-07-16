#!/usr/bin/env bash
. common.sh

# Stop on assertions:
#export G_DEBUG=fatal_warnings
#export G_DEBUG=fatal_criticals

# Logging from all glib based libraries
export G_MESSAGES_DEBUG=all

# Check memory allocation 
#export G_SLICE=debug-blocks

if [ -z "$1" ]; then
	echo "Uso: $0 [1|0]"
else
	echo "Using stun server ip($st1_ip) port($st1_port)"
	LD_LIBRARY_PATH=. ./tnice $1 ${st1_ip} ${st1_port} 2>&1 | tee tnice_${now}.log
fi

