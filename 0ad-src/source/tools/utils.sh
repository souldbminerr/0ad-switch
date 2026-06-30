#!/bin/sh

# Collection of sh utilities

# Return number of online cpu or 1 if it can't be determined.
utils_num_online_cpu()
{
	getconf _NPROCESSORS_ONLN 2>/dev/null && return
	getconf NPROCESSORS_ONLN 2>/dev/null && return
	nproc 2>/dev/null && return
	sysctl -m hw.nproc 2>/dev/null && return
	echo 1
}
