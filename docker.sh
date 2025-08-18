#!/usr/bin/env bash

TOP_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")"; pwd)
export PROJECT_ROOT_DIR=${TOP_DIR}

run_shell=${TOP_DIR}/devops/$(basename ${BASH_SOURCE[0]})
if [ -f ${run_shell} ];then
    source ${run_shell} "$@"
else 
    echo -e "\033[1;5;40;31m not support operation for $(basename ${BASH_SOURCE[0]}) \033[0m"
fi

