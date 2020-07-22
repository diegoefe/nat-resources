#!/usr/bin/env bash
st1_name=stun1.l.google.com
st1_ip=74.125.143.127
st1_port=19302

st2_name=stunserver.org
st2_ip=67.227.226.240
st2_port=3478

#stun1="stun.l.google.com:19302"
stun1="${st1_ip}:${st1_port}"

#st_ip=104.236.76.169
#st_port=3478

now=$(date +"%Y%m%d_%H%M%S")
