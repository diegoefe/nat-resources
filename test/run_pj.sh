#!/usr/bin/env bash
. common.sh
# echo "Using stun server ip($st1_name) port($st1_port)"
echo "Using stun server ip($stun1)"
./tpj -s ${stun1} --log-file tpj_${now}.log


# 	--stun-srv ${st1_name}:${st1_port} \
#	--comp-cnt 2
#