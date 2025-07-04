#!/usr/bin/env bash
#
# Copyright 2022 The TensorFlow Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ==============================================================================
#
# setup.python.sh: Install a specific Python version and packages for it.
# Usage: setup.python.sh <pyversion> <requirements.txt> <runtime mode>
set -xe


function build_python_from_src() {
    VERSION=$1
    REQUIREMENTS=$2
    local python_map
    declare -A python_map=(
        [python3.8]='3.8.12'
        [python3.9]='3.9.12'
        [python3.10]='3.10.9'
        [python3.11]='3.11.2'
        [python3.12]='3.12.4'
    )
    local _ver=${python_map[$VERSION]}
    wget https://www.python.org/ftp/python/${_ver}/Python-${_ver}.tgz
    tar xvf "Python-${_ver}.tgz" && rm -rf "Python-${_ver}.tgz"
    pushd Python-${_ver}/
	./configure --enable-optimizations
	make altinstall -j16

    ln -sf "/usr/local/bin/python${_ver%.*}" /usr/bin/python3
    ln -sf "/usr/local/bin/pip${_ver%.*}" /usr/bin/pip3
    ln -sf "/usr/local/lib/python${_ver%.*}" /usr/lib/tf_python
    popd
}

if (source /etc/os-release && [[ ${NAME} == SLES ]]); then
    build_python_from_src $1 $2
else

source ~/.bashrc
VERSION=$1
REQUIREMENTS=$2
PY_VERSION="python${VERSION}"

# Add deadsnakes repo for Python installation
DEBIAN_FRONTEND=noninteractive apt-get --allow-unauthenticated update
DEBIAN_FRONTEND=noninteractive apt install -y wget software-properties-common
DEBIAN_FRONTEND=noninteractive apt-get clean all
add-apt-repository -y 'ppa:deadsnakes/ppa'

# Install Python packages for this container's version
cat >pythons.txt <<EOF
$PY_VERSION
$PY_VERSION-dev
$PY_VERSION-venv
$PY_VERSION-distutils
EOF
/setup.packages.sh pythons.txt

# Setup links for TensorFlow to compile.
# Referenced in devel.usertools/*.bazelrc
ln -sf /usr/bin/$PY_VERSION /usr/bin/python3
ln -sf /usr/bin/$PY_VERSION /usr/bin/python
ln -sf /usr/lib/$PY_VERSION /usr/lib/tf_python

fi # end of conditional check of various distros


# Python 3.10 include headers fix:
# sysconfig.get_path('include') incorrectly points to /usr/local/include/python
# map /usr/include/python3.10 to /usr/local/include/python3.10
if [[ ! -f "/usr/local/include/$PY_VERSION" ]]; then
  ln -sf /usr/include/$PY_VERSION /usr/local/include/$PY_VERSION
fi

# Install pip
curl https://bootstrap.pypa.io/get-pip.py -o get-pip.py
python3 get-pip.py
python3 -m pip install --no-cache-dir --upgrade pip
python3 -m pip install -U setuptools

if [[ $3 ]]; then
    echo "Runtime mode"
    python3 -m pip install --no-cache-dir --no-deps tf-keras-nightly~=2.18.0.dev
else
    echo "Install Requirements"
    # Disable the cache dir to save image space, and install packages
    python3 -m pip install --no-cache-dir -r $REQUIREMENTS -U
    python3 -m pip install --no-cache-dir --no-deps tf-keras-nightly~=2.18.0.dev
fi
