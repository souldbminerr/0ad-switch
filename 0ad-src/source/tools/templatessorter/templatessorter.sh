#!/bin/sh
# check arguments count
if [ $# -ne 1 ]; then
	echo usage: "$0" directory
	exit 1
fi
# assign arguments to variables with readable names
input_directory=$1
# perform work
find "$input_directory" -name \*.xml -exec xsltproc -o {} templatessorter.xsl {} \;
