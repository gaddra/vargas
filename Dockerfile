# Dockerfile for building htslib dependency + 
# all three SIMD versions of the vargas binary

FROM rikorose/gcc-cmake:gcc-6
ADD . /
RUN ls -lh
RUN ["docker_compile.sh"]