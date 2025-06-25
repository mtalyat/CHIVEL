from setuptools import setup

setup(
    name="chivel",
    version="0.1.1",
    description="Chivel C++/Python extension",
    author="Mitchell Talyat",
    packages=["chivel"],
    package_data={"chivel": ["chivel.pyd", "chivel.pyi"]},
    include_package_data=True,
    zip_safe=False,
)