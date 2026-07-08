from setuptools import setup, find_packages

setup(
    packages=find_packages(),
    python_requires='>=3.11',
    install_requires=[
        "websockets>=12.0",
		"numpy>=1.26.0",
        "PyQt6>=6.5.0",
        "pyqtgraph>=0.13.0",
        "pyzmq"
    ],
    include_package_data=True
)