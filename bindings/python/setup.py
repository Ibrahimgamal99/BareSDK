from setuptools import setup, Distribution
from wheel.bdist_wheel import bdist_wheel


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


setup(distclass=BinaryDistribution, cmdclass={"bdist_wheel": BinaryWheel})
