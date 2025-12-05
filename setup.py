# setup.py
# 这是一个传统的Python项目配置文件。如果您的构建工具链不完全支持pyproject.toml，
# 或者您更熟悉setup.py，可以使用此文件。请将其放置在您项目的根目录下。
# 注意：推荐使用现代化的pyproject.toml文件。

import os
import sys
import subprocess
from pathlib import Path

from setuptools import setup, Extension, find_packages
from setuptools.command.build_ext import build_ext


class CMakeExtension(Extension):
    def __init__(self, name: str, sourcedir: str = "") -> None:
        super().__init__(name, sources=[])
        self.sourcedir = os.fspath(Path(sourcedir).resolve())


class CMakeBuild(build_ext):
    def build_extension(self, ext: CMakeExtension) -> None:
        # 确定Python可执行文件以传递给CMake
        python_executable = sys.executable

        # 为扩展模块确定输出目录
        extdir = Path(self.get_ext_fullpath(ext.name)).parent.resolve()

        # CMake配置参数
        cmake_args = [
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={extdir}",
            f"-DPYTHON_EXECUTABLE={python_executable}",
            "-DCMAKE_BUILD_TYPE=Release",
        ]

        # 构建参数
        build_args = ["--config", "Release"]

        # 在Windows上，为MSVC添加额外的构建参数
        if sys.platform == "win32":
            cmake_args += ["-DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=TRUE"]
        else:
            # 为非Windows平台（Linux, macOS）设置C++23标准
            cmake_args += ["-DCMAKE_CXX_STANDARD=23"]

        # 创建构建目录
        build_temp = Path(self.build_temp) / ext.name
        if not build_temp.exists():
            build_temp.mkdir(parents=True)

        # 运行CMake配置和构建命令
        subprocess.run(
            ["cmake", ext.sourcedir, *cmake_args],
            cwd=build_temp,
            check=True,
        )
        subprocess.run(
            ["cmake", "--build", ".", *build_args],
            cwd=build_temp,
            check=True,
        )


setup(
    name="5dchess_engine",
    version="1.0.0",
    author="SuZero-5DChess",
    description="A 5D Chess engine written in C++ with Python bindings",
    long_description=Path("README.md").read_text(),
    long_description_content_type="text/markdown",
    
    # 关键修复：使用 find_packages() 自动发现并包含 5dchess_engine 目录作为 Python 包
    # 这样，编译好的 5dchess_engine.so 就会被安装到这个包的根目录下
    packages=find_packages(), 
    
    ext_modules=[CMakeExtension("5dchess_engine")],
    cmdclass={"build_ext": CMakeBuild},
    zip_safe=False,
)
