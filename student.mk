# --- Execution Parameters ---
export OMP_NUM_THREADS=8
MPI_PROCS=4 #8
export OMP_PLACES=cores
export OMP_PROC_BIND=close
#export OMP_WAIT_POLICY=active
export OMP_DYNAMIC=false
export IS_PROFILING=false
# --- MPI Run Flags ---
MPIRUN_FLAGS = -np $(MPI_PROCS) \
			   --map-by socket:PE=8 \
			   --bind-to core

# --- Compiler Flags ---
# Flags for MPI+OpenMP code
# Uncomment and add extra flags if you need them
MPI_OMP_EXTRA_CFLAGS = -DIS_PROFILING=$(IS_PROFILING)
MPI_OMP_EXTRA_LIBS =  -fopenmp \
                       -O3 \
                       -march=native \
                       -mtune=native \
                       -mfma \
                       -mavx2 \
                       -fno-math-errno \
                       -fno-trapping-math \
                       -fno-signaling-nans \
                       -funroll-loops \
					   -ffinite-math-only

# Flags for CUDA code
# Uncomment and add extra flags if you need them
CUDA_EXTRA_CFLAGS = -03
#CUDA_EXTRA_LIBS =
