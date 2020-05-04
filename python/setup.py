from distutils.core import setup, Extension
from distutils.command.build_ext import build_ext
from distutils.sysconfig import customize_compiler
import os

os.environ["CC"] = "g++"

cpp_args = [
        "-ansi",
        "-fPIC",
        "-std=c++11",
        "-Wall",
        "-Wcast-align",
        "-Wcast-qual",
        "-Wconversion",
        "-Wdisabled-optimization",
        "-Werror",
        "-Wextra",
        "-Wfloat-equal",
        "-Wformat=2",
        "-Wformat-nonliteral",
        "-Wformat-security",
        "-Wformat-y2k",
        "-Wimport",
        "-Winit-self",
        "-Winline",
        "-Winvalid-pch",
        "-Wmissing-field-initializers",
        "-Wmissing-format-attribute",
        "-Wmissing-include-dirs",
        "-Wmissing-noreturn",
        "-Wno-aggregate-return",
        "-Wno-padded",
        "-Wno-switch-enum",
        "-Wno-unused-parameter",
        "-Wpacked",
        "-Wpointer-arith",
        "-Wredundant-decls",
        "-Wshadow",
        "-Wstack-protector",
        "-Wstrict-aliasing=3",
        "-Wswitch",
        "-Wswitch-default",
        "-Wunreachable-code",
        "-Wunused",
        "-Wvariadic-macros",
        "-Wwrite-strings",
        "-Wno-switch-default",
        "-Wconversion"]

modsairedis = Extension('sairedis',
        include_dirs = ["..", "../lib/inc", "../SAI/inc", "../SAI/experimental", "../SAI/meta"],
        library_dirs = ["../meta/.libs"],
        libraries = ['saimeta', 'saimetadata'],
        extra_objects = ['../lib/src/libSaiRedis.a'],
        language='c++',
        extra_compile_args = cpp_args,
        sources = ['sairedis.cpp'])

class my_build_ext(build_ext):
    def build_extensions(self):
        customize_compiler(self.compiler)
        try:
            self.compiler.compiler_so.remove("-Wstrict-prototypes")
        except (AttributeError, ValueError):
            pass
        build_ext.build_extensions(self)

setup ( name = 'SaiRedis',
        version = '1.0',
        description = 'This is a sairedis package',
        cmdclass = {'build_ext': my_build_ext},
        ext_modules = [modsairedis])


