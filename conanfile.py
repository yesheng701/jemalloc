import os
import json
import subprocess
from typing import Dict

from conan.tools.files import save, load
from conans import ConanFile, CMake
from conans.client.profile_loader import read_profile
from conans.model.conanfile_interface import ConanFileInterface
from conans.paths import get_conan_user_home
from conans.tools import cpu_count


required_conan_version = "<=1.51.0"
COOK_TEMP_NAME = 'sdk-other'  # 区分SDK和业务模块，conanfile更新时，用于区分不同的 conanfile
COOK_TEMP_VERSION = '0.2.0'  # cook内部版本号，conanfile有更新时，当前版本号比较旧。如果版本号一致，不会触发更新(./devops/cl.sh conan_update)

"""
# Conanfile环境变量
# Conan相关 CONAN_CPU_COUNT
# 平台信息 LANG LC_ALL PLATFORM TARGET_PLATFORM TARGET_OS TARGET_ARCH TARGET_ARCH_TYPE BUILD_TOOLS CROSS_COMPILING
# 环境信息 PATH X64_PROTOBUF_ROOT LD_LIBRARY_PATH INSTALL_PREFIX ARM64_ROOT PAVARO_ROOT TARGET_SYSROOT TI_GCC_DIR X3J3_GCC_DIR BUILD_TOOLS_ROOT_DIR OUTPUT_NAME
# 环境信息 QNX_TARGET QNX_HOST QNX_CONFIGURATION QNX_CONFIGURATION_EXCLUSIVE MAKEFLAGS PYTHONDONTWRITEBYTECODE
# 编译参数 DCMAKE_TOOLCHAIN_FILE DCMAKE_EXE_LINKER_FLAGS MAKE_COMPILE_CONFIG LD AS AR CXX CC HOST F77
# 获取环境变量方法：
#   PLATFORM = os.environ.get("PLATFORM")
#   TARGET_OS = os.environ.get("TARGET_OS", f"{self.settings.os}")
#   TARGET_ARCH_TYPE = os.environ.get("TARGET_ARCH_TYPE", f"{self.settings.arch}")
#   TARGET_PLATFORM = os.environ.get("TARGET_PLATFORM")
#   CONAN_CPU_COUNT = cpu_count()  # 可设置CONAN_CPU_COUNT的值优先获取，默认 multiprocessing.cpu_count()获取
"""


class ConanProfile(ConanFileInterface):
    """加载profile文件，读取相关配置信息"""

    def __init__(self, profile_path):
        profiles_path = os.path.join(os.path.join(get_conan_user_home(), ".conan"), "profiles")
        super(ConanProfile, self).__init__(read_profile(profile_path, os.getcwd(), profiles_path))
        self.inherited_profile, _ = self._conanfile
        self.tool_requires = self._tool_requires

    @property
    def settings(self) -> Dict:
        return dict(self.inherited_profile.settings)

    @property
    def _tool_requires(self) -> list:
        """return ['protobuf_x86/3.11.4@3rd/stable', ...]"""
        _build_requires_reference = []
        if not self.inherited_profile: return _build_requires_reference
        for item in list(dict(self.inherited_profile.build_requires).values()):
            for i in item:
                _build_requires_reference.append(i.__str__())
        return _build_requires_reference

    def get_ref_build_requires_packages(self, name):
        """根据包name获取完整Conan包REF name=protobuf_x86; return protobuf_x86/3.11.4@3rd/stable"""
        for ref in self.tool_requires:
            if name in ref.split('/'): return ref

    def get_ref_tool_requires_names(self):
        """返回包name ; return [protobuf_x86 ...]"""
        return [ref.split('/')[0] for ref in self.tool_requires]
    pass


class ColconBuild(object):
    build_folder = None
    package_folder = None

    def init(self, app) -> None:
        self.app = app
        self.profile = ConanProfile(os.getenv('CONAN_PROFILE_PATH'))
        self.PROJECT_ROOT_DIR = os.getenv('PROJECT_ROOT_DIR')
        self.CATKIN_BUILD_ROOT = os.getenv('CATKIN_BUILD_ROOT')
        self.parallel_workers = os.getenv('PARALLEL', '4')
        # self.parallel_workers = os.getenv('CORES_CPU', '4')
        self.build_base = os.path.join(f'{self.CATKIN_BUILD_ROOT}', 'build')
        self.install_base = os.path.join(f'{self.CATKIN_BUILD_ROOT}', 'install')
        self.base_paths = os.getenv('CATKIN_SRC_ROOT')
        self.cmake_args = os.getenv('CMAKE_COMPILE_OPTIONS')

        self.build_folder = self.app.build_folder
        self.package_folder = self.app.package_folder
        self.cache_conan_remove = f"{self.CATKIN_BUILD_ROOT}/cache_conan.json"
        self.load_cache()
        self.set_version_info()
        pass

    def load_cache(self):
        """生成缓存数据，提供给import时使用"""
        def cache(cache_conan_remove):
            """cache_conan_remove文件不存在时创建，package_folder元素在import时使用"""
            context = json.dumps({
                'build_folder': self.build_folder,
                'package_folder': self.package_folder
            })
            save(self.app, cache_conan_remove, context)
            self.app.output.highlight(f'Create cahce file {cache_conan_remove}. context {context}')
            pass

        if (not (self.build_folder and self.package_folder)) and os.path.exists(self.cache_conan_remove):
            """build_folder|package_folder为空时，读取文件获取"""
            folder = json.loads(load(self.app, self.cache_conan_remove))
            self.build_folder = folder.get('build_folder')
            self.package_folder = folder.get('package_folder')
            self.app.output.highlight(f'Load cahce file {self.cache_conan_remove}. context {folder}')
        elif self.build_folder and self.package_folder:
            """cache_conan_remove文件不存在时创建，package_folder元素在import时使用"""
            cache(self.cache_conan_remove)
            pass
        pass

    def open_commitid_read(self):
        """
        记录当前模块commitid及其submoduls
        命名：commitid_{self.name}_v{self.version}.txt
        """
        path_commitid = f"{self.PROJECT_ROOT_DIR}/output/.commitid"
        if not os.path.exists(path_commitid): os.makedirs(path_commitid)
        self.app.run(
            f'cd {self.PROJECT_ROOT_DIR} && \
            echo " $(git rev-parse HEAD) {self.app.name} (Conan package v{self.app.version})" > {self.PROJECT_ROOT_DIR}/output/.commitid/commitid_{self.app.name}_v{self.app.version}.txt && \
            echo "$(git submodule status)" >> {path_commitid}/commitid_{self.app.name}_v{self.app.version}.txt'
        )

    def set_toolchain_version(self):
        """记录当前模块或集成库中使用的toolchain或bsp软件包ref"""
        # TODO: 需要指定 修订版本
        path_version = f"{self.PROJECT_ROOT_DIR}/output/.conan"
        if not os.path.exists(path_version): os.makedirs(path_version)
        qnx_ref_packages = self.profile.get_ref_build_requires_packages('qnx710')
        bsp_ref_packages = self.profile.get_ref_build_requires_packages('bsp')
        if qnx_ref_packages: self.app.run(f'echo "{qnx_ref_packages}" > {path_version}/qnx_toolchain.version')
        if bsp_ref_packages: self.app.run(f'echo "{bsp_ref_packages}" > {path_version}/bsp.version')
        pass

    def run(self, cmd):
        return subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE).stdout.read().decode().splitlines()

    def set_version_info(self):
        """记录版本信息"""
        context = json.dumps({
            "name": self.app.name,
            "commitid": "".join(self.run(f"cd {self.PROJECT_ROOT_DIR} && git rev-parse HEAD")),
            "branch": "".join(self.run(f"cd {self.PROJECT_ROOT_DIR} && git rev-parse --abbrev-ref HEAD")),
            "repository": "".join(self.run(f"cd {self.PROJECT_ROOT_DIR} && git remote get-url --push origin")),
            "version": self.app.version,
            "dependency": [],
            "submodule": []
        }, indent = 4)
        save(self.app, f"{self.package_folder}/info.json", context)


class HaomoConan(ConanFile):
    name = 'jemalloc'
    version = '5.3.0.3'
    license = '<Put the package license here>'
    author = '<Put your name here> <And your email here>'
    url = 'git@codeup.aliyun.com:5f02dcd86a575d7f23661142/opensource/jemalloc-5.3.0.git'
    description = 'description'

    settings = "os", "compiler", "build_type", "arch", "toolchain"
    options = {"shared": [True, False]}
    default_options = {"shared": True}
    generators = "cmake"
    colcon_build = ColconBuild()

    exports_sources = ['*', '!thirdparty', '!output', '!build', '!tmp', '!.vscode',]

    # 第三方库也是有依赖的，直接获取最新devops/conan/profile/
    requires = []

    def package(self):
        self.copy("*", src=os.path.join(os.getenv("PROJECT_ROOT_DIR"), "output"), dst=os.path.join(self.package_folder)+'/', symlinks=True)
        self.copy("*", src=os.path.join(self.package_folder)+'/', dst=os.path.join(os.getenv("PROJECT_ROOT_DIR"), "output"), symlinks=True)

    def build(self):
        """
        E.g.:
            build_args()            初始化工具链等编译环境
            DCMAKE_TOOLCHAIN_FILE   工具链cmake路径
            DCMAKE_EXE_LINKER_FLAGS 未知
            DCMAKE_INSTALL_PREFIX   安装路径
            DCMAKE_COMPILE_OPTIONS_LIST 包含 DCMAKE_TOOLCHAIN_FILE, DCMAKE_EXE_LINKER_FLAGS 部分参数
        """
        self.colcon_build.init(self)
        self.colcon_build.open_commitid_read()
        self.colcon_build.set_toolchain_version()

        PLATFORM = os.environ.get("PLATFORM", "linux_x86_default")
        TARGET_ARCH_TYPE = os.environ.get("TARGET_ARCH_TYPE", f"{self.settings.arch}")
        CONAN_CPU_COUNT = cpu_count()  # bash 可设置CONAN_CPU_COUNT的值优先获取，默认 multiprocessing.cpu_count()获取
        # cmake args DCMAKE_COMPILE_OPTIONS_LIST 包含 DCMAKE_TOOLCHAIN_FILE, DCMAKE_EXE_LINKER_FLAGS 部分参数
        PROJECT_ROOT_DIR, INSTALL_PREFIX, DCMAKE_COMPILE_OPTIONS_LIST, MAKE_COMPILE_CONFIG = self._build_args()

        """
        # os.environ.get() 获取环境变量值
        # self.run() 运行bash脚本

        PLATFORM = os.environ.get("PLATFORM", "linux_x86_default")

        if PLATFORM != 'linux_x86_default':
            self.run("echo 'Linux x86'")
        elif PLATFORM == 'qnx71_aarch64_qualcomm':
            self.run("echo 'Qnx aarch64'")
        else:
            self.run("echo '其他'")

        self.run(f'cd {PROJECT_ROOT_DIR} && ./configure {MAKE_COMPILE_CONFIG} --enable-sparse --prefix={INSTALL_PREFIX} && make && make install')
        """

        TARGET_OS = os.environ.get('TARGET_OS')
        AIB_SET="abi=sysv"
        architecture="arm"

        if TARGET_ARCH_TYPE == "x86": architecture="x86"
        if TARGET_ARCH_TYPE == 'aarch64': AIB_SET="abi=aapcs"

        if TARGET_OS == 'qnx710':
            self.run(f"./autogen.sh --enable-cxx --enable-prof --disable-prof-libgcc --disable-prof-gcc {MAKE_COMPILE_CONFIG} CPPFLAGS=-D_QNX_SOURCE LDFLAGS=-lc++ --prefix={INSTALL_PREFIX} && make -j{CONAN_CPU_COUNT} && make install")
        else:
            if PLATFORM == 'linux_x86_default':
                self.run(f"./autogen.sh && ./configure {MAKE_COMPILE_CONFIG} --enable-shared --prefix={INSTALL_PREFIX} && make -j${CONAN_CPU_COUNT} && make install")
            else:
                self.run(f"./autogen.sh --enable-cxx {MAKE_COMPILE_CONFIG} --prefix={INSTALL_PREFIX} && make -j${CONAN_CPU_COUNT} && make install")
            pass

    def _build_args(self):
        """
        编译环境初始化
        :return:
            PROJECT_ROOT_DIR:               Conan编译工程路径
            INSTALL_PREFIX:                 模块的安装路径
            CMAKE_TOOLCHAIN:                工具链cmake路径
            TARGET_OS:                      系统
            TARGET_ARCH_TYPE:               架构
            DCMAKE_TOOLCHAIN_FILE:          工具链
            DCMAKE_EXE_LINKER_FLAGS:        全局链接器标志
            DCMAKE_COMPILE_OPTIONS_LIST:    cmake编译参数
        """
        INSTALL_PREFIX = f"{self.package_path}"
        PROJECT_ROOT_DIR = f"{self.build_path}"  # os.path.dirname(os.path.abspath(__file__))
        BUILD_TOOLS_ROOT_DIR = os.path.join(PROJECT_ROOT_DIR, 'build_tools')

        os.environ['OUTPUT_NAME'] = INSTALL_PREFIX
        os.environ['PROJECT_ROOT_DIR'] = PROJECT_ROOT_DIR
        os.environ['BUILD_TOOLS_ROOT_DIR'] = BUILD_TOOLS_ROOT_DIR

        # cmake 编译参数
        MAKE_COMPILE_CONFIG = os.environ.get('MAKE_COMPILE_CONFIG')
        DCMAKE_TOOLCHAIN_FILE = os.environ.get('DCMAKE_TOOLCHAIN_FILE')
        DCMAKE_EXE_LINKER_FLAGS = os.environ.get('DCMAKE_EXE_LINKER_FLAGS')

        if not DCMAKE_TOOLCHAIN_FILE: ConanException("Error while DCMAKE_TOOLCHAIN_FILE options. %s" % str(DCMAKE_TOOLCHAIN_FILE))
        if not DCMAKE_EXE_LINKER_FLAGS: ConanException("Error while DCMAKE_EXE_LINKER_FLAGS options. %s" % str(DCMAKE_EXE_LINKER_FLAGS))

        DCMAKE_COMPILE_OPTIONS_LIST = [DCMAKE_TOOLCHAIN_FILE, DCMAKE_EXE_LINKER_FLAGS]
        return PROJECT_ROOT_DIR, INSTALL_PREFIX, DCMAKE_COMPILE_OPTIONS_LIST, MAKE_COMPILE_CONFIG
