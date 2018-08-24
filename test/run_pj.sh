#!/usr/bin/env bash
. common.sh
./tpj \
	--stun-srv ${st_ip}m:${st_port} \
	--log-file tpj_${now}.log

