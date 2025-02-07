.. _building-summit:

Summit (OLCF)
=============

The `Summit cluster <https://www.olcf.ornl.gov/summit/>`_ is located at OLCF.

If you are new to this system, please see the following resources:

* `Summit user guide <https://docs.olcf.ornl.gov/systems/summit_user_guide.html>`_
* Batch system: `LSF <https://docs.olcf.ornl.gov/systems/summit_user_guide.html#running-jobs>`_
* `Production directories <https://docs.olcf.ornl.gov/data/storage_overview.html>`_:

  * ``$PROJWORK/$proj/``: shared with all members of a project (recommended)
  * ``$MEMBERWORK/$proj/``: single user (usually smaller quota)
  * ``$WORLDWORK/$proj/``: shared with all users
  * Note that the ``$HOME`` directory is mounted as read-only on compute nodes.
    That means you cannot run in your ``$HOME``.


Installation
------------

Use the following commands to download the WarpX source code and switch to the correct branch:

.. code-block:: bash

   git clone https://github.com/ECP-WarpX/WarpX.git $HOME/src/warpx

We use the following modules and environments on the system (``$HOME/warpx.profile``).

.. code-block:: bash

   # please set your project account
   export proj=<yourProject>

   # optional: just an additional text editor
   module load nano

   # required dependencies
   module load cmake/3.20.2
   module load gcc/9.3.0
   module load cuda/11.0.3

   # optional: faster re-builds
   module load ccache

   # optional: for PSATD in RZ geometry support
   module load blaspp/2021.04.01
   module load lapackpp/2021.04.00

   # optional: for PSATD support (CPU only)
   #module load fftw/3.3.9

   # optional: for QED lookup table generation support
   module load boost/1.76.0

   # optional: for openPMD support
   module load adios2/2.7.1
   module load hdf5/1.10.7

   # optional: for openPMD support (GNUmake only)
   #module load ums
   #module load ums-aph114
   #module load openpmd-api/0.14.2

   # often unstable at runtime with dependencies
   module unload darshan-runtime

   # optional: Ascent in situ support
   #   note: build WarpX with CMake
   export Ascent_DIR=/gpfs/alpine/csc340/world-shared/software/ascent/2021_09_01_gcc_9_3_0_warpx/summit/cuda/gnu/ascent-install

   # optional: for Python bindings or libEnsemble
   module load python/3.8.10
   module load openblas/0.3.5-omp  # numpy; same as for blaspp & lapackpp
   module load freetype/2.10.4     # matplotlib
   if [ -d "$HOME/sw/venvs/warpx" ]
   then
     source $HOME/sw/venvs/warpx/bin/activate
   fi

   # an alias to request an interactive batch node for two hours
   #   for paralle execution, start on the batch node: jsrun <command>
   alias getNode="bsub -q debug -P $proj -W 2:00 -nnodes 1 -Is /bin/bash"
   # an alias to run a command on a batch node for up to 30min
   #   usage: nrun <command>
   alias runNode="bsub -q debug -P $proj -W 0:30 -nnodes 1 -I"

   # fix system defaults: do not escape $ with a \ on tab completion
   shopt -s direxpand

   # make output group-readable by default
   umask 0027

   # optimize CUDA compilation for V100
   export AMREX_CUDA_ARCH=7.0

   # compiler environment hints
   export CC=$(which gcc)
   export CXX=$(which g++)
   export FC=$(which gfortran)
   export CUDACXX=$(which nvcc)
   export CUDAHOSTCXX=$(which g++)


We recommend to store the above lines in a file, such as ``$HOME/warpx.profile``, and load it into your shell after a login:

.. code-block:: bash

   source $HOME/warpx.profile

Optionally, download and install Python packages for :ref:`PICMI <usage-picmi>` or dynamic ensemble optimizations (:ref:`libEnsemble <libensemble>`):

.. code-block:: bash

   export BLAS=$OLCF_OPENBLAS_ROOT/lib/libopenblas.so
   export LAPACK=$OLCF_OPENBLAS_ROOT/lib/libopenblas.so
   python3 -m pip install --user --upgrade pip
   python3 -m pip install --user virtualenv
   python3 -m pip cache purge
   rm -rf $HOME/sw/venvs/warpx
   python3 -m venv $HOME/sw/venvs/warpx
   source $HOME/sw/venvs/warpx/bin/activate
   python3 -m pip install --upgrade pip
   python3 -m pip install --upgrade wheel
   python3 -m pip install --upgrade cython
   python3 -m pip install --upgrade numpy
   python3 -m pip install --upgrade scipy
   python3 -m pip install --upgrade mpi4py --no-binary mpi4py
   python3 -m pip install --upgrade openpmd-api
   python3 -m pip install --upgrade matplotlib==3.2.2  # does not try to build freetype itself
   python3 -m pip install --upgrade yt
   # WIP: issues with nlopt
   # python3 -m pip install -r $HOME/src/warpx/Tools/LibEnsemble/requirements.txt

Then, ``cd`` into the directory ``$HOME/src/warpx`` and use the following commands to compile:

.. code-block:: bash

   cd $HOME/src/warpx
   rm -rf build

   cmake -S . -B build -DWarpX_OPENPMD=ON -DWarpX_DIMS=3 -DWarpX_COMPUTE=CUDA
   cmake --build build -j 6

The general :ref:`cmake compile-time options <building-cmake>` apply as usual.

For a full PICMI install, follow the :ref:`instructions for Python (PICMI) bindings <building-cmake-python>`.
We only prefix it to request a node for the compilation (``runNode``), so we can compile faster:

.. code-block:: bash

   # PICMI build
   cd $HOME/src/warpx

   # compile parallel PICMI interfaces with openPMD support and 3D, 2D and RZ
   runNode WarpX_MPI=ON WarpX_COMPUTE=CUDA WarpX_PSATD=ON WarpX_OPENPMD=ON BUILD_PARALLEL=32 python3 -m pip install --force-reinstall -v .


.. _running-cpp-summit:

Running
-------

.. _running-cpp-summit-V100-GPUs:

V100 GPUs
^^^^^^^^^

The batch script below can be used to run a WarpX simulation on 2 nodes on
the supercomputer Summit at OLCF. Replace descriptions between chevrons ``<>``
by relevant values, for instance ``<input file>`` could be
``plasma_mirror_inputs``. Note that the only option so far is to run with one
MPI rank per GPU.

.. literalinclude:: ../../../../Tools/BatchScripts/batch_summit.sh
   :language: bash

To run a simulation, copy the lines above to a file ``batch_summit.sh`` and
run
::

  bsub batch_summit.sh

to submit the job.

For a 3D simulation with a few (1-4) particles per cell using FDTD Maxwell
solver on Summit for a well load-balanced problem (in our case laser
wakefield acceleration simulation in a boosted frame in the quasi-linear
regime), the following set of parameters provided good performance:

* ``amr.max_grid_size=256`` and ``amr.blocking_factor=128``.

* **One MPI rank per GPU** (e.g., 6 MPI ranks for the 6 GPUs on each Summit
  node)

* **Two `128x128x128` grids per GPU**, or **one `128x128x256` grid per GPU**.

A batch script with more options regarding profiling on Summit can be found at
:download:`Summit batch script <../../../../Tools/BatchScripts/script_profiling_summit.sh>`

.. _running-cpp-summit-Power9-CPUs:

Power9 CPUs
^^^^^^^^^^^

Similar to above, the batch script below can be used to run a WarpX simulation on
1 node on the supercomputer Summit at OLCF, on Power9 CPUs (i.e., the GPUs are
ignored).

.. literalinclude:: ../../../../Tools/BatchScripts/batch_summit_power9.sh
   :language: bash

For a 3D simulation with a few (1-4) particles per cell using FDTD Maxwell
solver on Summit for a well load-balanced problem, the following set of
parameters provided good performance:

* ``amr.max_grid_size=64`` and ``amr.blocking_factor=64``

* **Two MPI ranks per node** (i.e. 2 resource sets per node; equivalently, 1
  resource set per socket)

* **21 physical CPU cores per MPI rank**

* **21 OpenMP threads per MPI rank** (i.e. 1 OpenMP thread per physical core)

* **SMT 1 (Simultaneous Multithreading level 1)**

* **Sixteen `64x64x64` grids per MPI rank** (with default tiling in WarpX, this
  results in ~49 tiles per OpenMP thread)
