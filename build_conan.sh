###
 # @Author: zhangjuemin 1748011755@qq.com
 # @Date: 2023-06-05 15:47:04
 # @LastEditors: zhangjuemin 1748011755@qq.com
 # @LastEditTime: 2023-06-13 17:09:30
 # @FilePath: /pos/devops/build_template/build.sh
 # @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
### 

# export PROJECT_NAME=ICU30
export PROJECT_ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")"; pwd)
args="$@"
RUN=${PROJECT_ROOT_DIR}/devops/build_conan.sh

if [ -f "${RUN}" ];then
    source ${RUN} $args
    exit $?
else 
    echo -e "\033[1;5;40;31m not support operation for $(basename ${BASH_SOURCE[0]}) \033[0m"
    exit 1
fi
