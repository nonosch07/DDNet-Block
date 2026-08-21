#!/bin/bash

commands=$(env | grep '^TW_' | sed 's/=/ /g; s/^TW_//')

echo "${commands}" | tee env_generated.cfg

if ! grep -qFx "exec env_generated.cfg" autoexec_server.cfg; then
	echo "exec env_generated.cfg" >> autoexec_server.cfg
fi

sleep 2

if [ -n "${DEBUG+x}" ]; then
	echo "Starting blockworlds in debug mode..."
	gdb -ex run -ex bt --batch --args ./blockworlds_d -f autoexec_server.cfg
else
	echo "Starting blockworlds in release mode..."
	./blockworlds -f autoexec_server.cfg
fi
