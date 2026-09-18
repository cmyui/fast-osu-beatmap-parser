ARG BASE_IMAGE
FROM ${BASE_IMAGE}

COPY .github/scripts/install-linux-gcc.sh /tmp/install-linux-gcc.sh
RUN /tmp/install-linux-gcc.sh && rm /tmp/install-linux-gcc.sh

ENV CC=/opt/fosu-gcc-16.2.0/bin/gcc
ENV CXX=/opt/fosu-gcc-16.2.0/bin/g++
RUN test "$("${CXX}" -dumpfullversion)" = "16.2.0"
