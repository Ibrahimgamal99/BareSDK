from setuptools import setup
from wheel.bdist_wheel import bdist_wheel


class BinaryWheel(bdist_wheel):
    """Produce a platform-specific wheel that works on any Python 3.x.

    Default tag without this:  cp314-cp314-linux_x86_64  (one Python version)
    Tag with this override:     py3-none-linux_x86_64     (all Python 3.x)

    The .so / .dll inside is a pre-built C library — it has no CPython ABI
    dependency, so py3-none is correct. The platform tag (linux_x86_64 /
    win_amd64) is what tells pip to pick the right wheel per OS.
    """

    def finalize_options(self):
        super().finalize_options()
        self.root_is_pure = False   # mark as binary so the platform tag is set

    def get_tag(self):
        _, _, plat = super().get_tag()
        return "py3", "none", plat  # any Python 3, no ABI, native platform


setup(cmdclass={"bdist_wheel": BinaryWheel})
