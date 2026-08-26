import sys
from setuptools import setup, Distribution
from wheel.bdist_wheel import bdist_wheel

if sys.platform == "win32":
    native_lib = ["echosdk.dll"]
elif sys.platform == "darwin":
    native_lib = ["echosdk.dylib"]
else:
    native_lib = ["echosdk.so"]


class BinaryDistribution(Distribution):
    def has_ext_modules(self):
        return True  # forces setuptools to use platlib, not purelib


class BinaryWheel(bdist_wheel):
    def finalize_options(self):
        super().finalize_options()
        self.root_is_pure = False

    def get_tag(self):
        _, _, plat = super().get_tag()
        return "py3", "none", plat  # any Python 3, no ABI, native platform


setup(
    distclass=BinaryDistribution,
    cmdclass={"bdist_wheel": BinaryWheel},
    package_data={"echo_sdk": native_lib + ["_echosdk_clean.h"]},
)
