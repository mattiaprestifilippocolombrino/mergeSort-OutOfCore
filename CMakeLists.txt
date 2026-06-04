cmake_minimum_required(VERSION 3.16)
project(OutOfCoreMergeSort CXX)

# ─── Build type di default ────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ─── Standard C++17 ───────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Flag di compilazione ─────────────────────────────────────────────────────
add_compile_options(-O3 -march=native -Wall -Wextra)

# ─── PAYLOAD_MAX configurabile ────────────────────────────────────────────────
if(DEFINED PAYLOAD_MAX)
    add_compile_definitions(PAYLOAD_MAX=${PAYLOAD_MAX})
endif()

# ─── Include directories ──────────────────────────────────────────────────────
include_directories(common/include fastflow/include)
set(SPM_TEST_SCRATCH "/scratch/m.prestifilippoco/spm_tests" CACHE PATH "Directory scratch per i test manuali")

# ─── OpenMP ───────────────────────────────────────────────────────────────────
find_package(OpenMP REQUIRED)

# ─── FastFlow ─────────────────────────────────────────────────────────────────
if(NOT FF_ROOT)
    set(FF_ROOT "$ENV{FF_ROOT}")
endif()

set(FF_SEARCH_PATHS
    "${FF_ROOT}"
    "$ENV{HOME}/fastflow"
    "$ENV{HOME}/FF"
    "$ENV{HOME}/SPM/fastflow"
    "/usr/local/include"
    "/usr/include"
)

find_path(FF_INCLUDE_DIR ff/ff.hpp
    PATHS ${FF_SEARCH_PATHS}
    PATH_SUFFIXES "" "include"
    NO_DEFAULT_PATH
)

if(FF_INCLUDE_DIR)
    message(STATUS "FastFlow trovato in: ${FF_INCLUDE_DIR}")
    set(FF_FOUND TRUE)
else()
    message(WARNING "FastFlow non trovato. ff_sort non sarà compilato.\n"
        "Specificare il percorso con: cmake -DFF_ROOT=/path/to/fastflow ..")
    set(FF_FOUND FALSE)
endif()

# ─── MPI ──────────────────────────────────────────────────────────────────────
find_package(MPI)
if(MPI_CXX_FOUND)
    message(STATUS "MPI trovato: ${MPI_CXX_COMPILER}")
else()
    find_path(SPM_MPI_INCLUDE_DIR mpi.h
        PATHS
            /usr/lib/x86_64-linux-gnu/openmpi/include
            /usr/include/openmpi-x86_64
            /usr/local/include
            /usr/include
    )
    find_library(SPM_MPI_LIBRARY mpi
        PATHS
            /usr/lib/x86_64-linux-gnu/openmpi/lib
            /usr/lib64/openmpi/lib
            /usr/local/lib
            /usr/lib
    )
    find_library(SPM_MPI_CXX_LIBRARY mpi_cxx
        PATHS
            /usr/lib/x86_64-linux-gnu/openmpi/lib
            /usr/lib64/openmpi/lib
            /usr/local/lib
            /usr/lib
    )

    if(SPM_MPI_INCLUDE_DIR AND SPM_MPI_LIBRARY)
        message(WARNING "FindMPI non ha validato MPI, uso fallback OpenMPI: ${SPM_MPI_INCLUDE_DIR}")
        if(NOT TARGET MPI::MPI_CXX)
            add_library(MPI::MPI_CXX INTERFACE IMPORTED)
        endif()
        target_include_directories(MPI::MPI_CXX INTERFACE ${SPM_MPI_INCLUDE_DIR})
        if(SPM_MPI_CXX_LIBRARY)
            target_link_libraries(MPI::MPI_CXX INTERFACE ${SPM_MPI_CXX_LIBRARY})
        endif()
        target_link_libraries(MPI::MPI_CXX INTERFACE ${SPM_MPI_LIBRARY})
        set(MPI_CXX_FOUND TRUE)
        set(MPIEXEC_EXECUTABLE mpiexec)
        set(MPIEXEC_NUMPROC_FLAG -n)
    else()
        message(WARNING "MPI non trovato. mpi_sort non sarà compilato.")
    endif()
endif()

# =============================================================================
# Eseguibili
# =============================================================================

# ─── Utilità comuni ───────────────────────────────────────────────────────────
add_executable(generate common/src/generate.cpp)
add_executable(verify common/src/verify.cpp)

# ─── Versione OpenMP (Single Node) ────────────────────────────────────────────
add_executable(omp_sort omp/src/omp_sort.cpp)
target_include_directories(omp_sort PRIVATE omp/include)
target_link_libraries(omp_sort PRIVATE OpenMP::OpenMP_CXX)

# ─── Versione FastFlow (Single Node) ──────────────────────────────────────────
if(FF_FOUND)
    add_executable(ff_sort fastflow/src/ff_sort.cpp)
    target_include_directories(ff_sort PRIVATE ${FF_INCLUDE_DIR})
    target_compile_definitions(ff_sort PRIVATE BLOCKING_MODE NO_DEFAULT_MAPPING)
    target_compile_options(ff_sort PRIVATE -Wno-unknown-pragmas -Wno-unused-parameter)
    target_link_libraries(ff_sort PRIVATE pthread OpenMP::OpenMP_CXX)
endif()

# ─── Versione MPI + OpenMP (Distributed) ──────────────────────────────────────
if(MPI_CXX_FOUND)
    add_executable(mpi_sort mpi/src/mpi_sort.cpp)
    target_include_directories(mpi_sort PRIVATE omp/include)
    target_link_libraries(mpi_sort PRIVATE MPI::MPI_CXX OpenMP::OpenMP_CXX)
endif()

# =============================================================================
# Target di test
# =============================================================================

add_custom_target(run_test_omp
    DEPENDS omp_sort generate verify
    COMMAND ${CMAKE_COMMAND} -E echo "=== Test OMP multi-pass - 500k record ==="
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SPM_TEST_SCRATCH}
    COMMAND ./generate ${SPM_TEST_SCRATCH}/test_input.bin 500000 --payload-max 64
    COMMAND ./omp_sort  ${SPM_TEST_SCRATCH}/test_input.bin ${SPM_TEST_SCRATCH}/test_output_omp.bin --chunk-mb 64 --threads 4 --tmp-dir ${SPM_TEST_SCRATCH} --multipass-merge --merge-fan 64
    COMMAND ./verify    ${SPM_TEST_SCRATCH}/test_input.bin ${SPM_TEST_SCRATCH}/test_output_omp.bin
    COMMAND ${CMAKE_COMMAND} -E remove ${SPM_TEST_SCRATCH}/test_input.bin ${SPM_TEST_SCRATCH}/test_output_omp.bin
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Test correttezza OMP multi-pass..."
)

if(FF_FOUND)
    add_custom_target(run_test_ff
        DEPENDS ff_sort generate verify
        COMMAND ${CMAKE_COMMAND} -E echo "=== Test FastFlow multi-pass - 500k record ==="
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPM_TEST_SCRATCH}
        COMMAND ./generate ${SPM_TEST_SCRATCH}/test_input_ff.bin 500000 --payload-max 256
        COMMAND ./ff_sort   ${SPM_TEST_SCRATCH}/test_input_ff.bin ${SPM_TEST_SCRATCH}/test_output_ff.bin --chunk-mb 64 --workers 4 --tmp-dir ${SPM_TEST_SCRATCH} --multipass-merge --merge-fan 64
        COMMAND ./verify    ${SPM_TEST_SCRATCH}/test_input_ff.bin ${SPM_TEST_SCRATCH}/test_output_ff.bin
        COMMAND ${CMAKE_COMMAND} -E remove ${SPM_TEST_SCRATCH}/test_input_ff.bin ${SPM_TEST_SCRATCH}/test_output_ff.bin
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Test correttezza FF multi-pass..."
    )
endif()

if(MPI_CXX_FOUND)
    add_custom_target(run_test_mpi
        DEPENDS mpi_sort generate verify
        COMMAND ${CMAKE_COMMAND} -E echo "=== Test MPI - 500k record, 2 rank ==="
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SPM_TEST_SCRATCH}
        COMMAND ./generate ${SPM_TEST_SCRATCH}/test_input_mpi.bin 500000 --payload-max 256
        COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 2 ./mpi_sort ${SPM_TEST_SCRATCH}/test_input_mpi.bin ${SPM_TEST_SCRATCH}/test_output_mpi.bin --chunk-mb 64 --threads 2 --tmp-dir ${SPM_TEST_SCRATCH} --multipass-local-merge --merge-fan 64
        COMMAND ./verify ${SPM_TEST_SCRATCH}/test_input_mpi.bin ${SPM_TEST_SCRATCH}/test_output_mpi.bin
        COMMAND ${CMAKE_COMMAND} -E remove ${SPM_TEST_SCRATCH}/test_input_mpi.bin ${SPM_TEST_SCRATCH}/test_output_mpi.bin
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Test correttezza MPI..."
    )
endif()

add_custom_target(run_test DEPENDS run_test_omp)
