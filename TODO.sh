#! /bin/bash

find src/ -regex ".*/.*\.\(h*\(c\)*\)" | \
	xargs grep -l -E -i -n --color=auto "(XXX)|(TODO)"
