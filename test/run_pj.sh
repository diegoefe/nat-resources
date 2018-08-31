#!/usr/bin/env bash
. common.sh
echo "Using stun server ip($st1_name) port($st1_port)"
./tpj \
	--stun-srv ${st1_name}:${st1_port} \
	--log-file tpj_${now}.log \
   --comp-cnt 2
