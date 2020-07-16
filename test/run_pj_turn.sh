#!/usr/bin/env bash
. common.sh
# echo "Using stun server ip($st1_name) port($st1_port)"
echo "Using stun server ip($stun1)"
# -s ${stun1}
# -s ${turns} \

# turns="turn.ubiguard.com:3478"
turns="turn.ubiguard.com:5349"

./tpj \
    --log-file tpj_${now}.log \
    -T -t ${turns} -u nubitest -p nubicam2020 -F

# 	--stun-srv ${st1_name}:${st1_port} \
#	--comp-cnt 2
#