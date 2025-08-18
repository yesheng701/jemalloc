###
 # @Author: zhangjuemin 1748011755@qq.com
 # @Date: 2023-06-05 15:47:04
 # @LastEditors: zhangjuemin 1748011755@qq.com
 # @LastEditTime: 2023-06-08 10:54:22
 # @FilePath: /pos/devops/build_template/build.sh
 # @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
### 

echo -e '\e[9;7;31m 脚本 [build.sh] 已过时，Conan化后不在受到支持。 \e[0m'
echo -e "\e[4;33m 请使用最新Conan分支,并将 devops/build_template/* 加入到当前项目中 \e[0m"
sleep 1

# export PROJECT_NAME=ICU30
export PROJECT_ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")"; pwd)
args="$@"
RUN=${PROJECT_ROOT_DIR}/devops/build_past.sh

if [ -f "${RUN}" ];then
    source ${RUN} $args
    exit $?
else 
    echo -e "\033[1;5;40;31m not support operation for $(basename ${BASH_SOURCE[0]}) \033[0m"
    exit 1
fi
