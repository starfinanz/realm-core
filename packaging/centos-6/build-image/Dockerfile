FROM ci/realm-core:centos-6-base

# whatever is required for building should be installed in this image; just
# like BuildRequires: for RPM specs
# On CentOS6, there is a bug with OverlayFS and Docker. It is needed to touch
# /var/lib/rpm/* in order to work around this issue.
# Link: https://github.com/docker/docker/issues/10180
RUN touch /var/lib/rpm/* \
    && yum -y install \
        cmake3 \
        git \
        wget \
        unzip \
        which \
        chrpath \
        openssl-devel \
        devtoolset-3-gcc \
        devtoolset-3-gcc-c++ \
        devtoolset-3-binutils \
    && yum clean all

# Install ninja
RUN wget https://github.com/ninja-build/ninja/releases/download/v1.7.2/ninja-linux.zip \
    && unzip ninja-linux.zip \
    && chmod a+x ninja \
    && mv ninja /usr/bin

ENTRYPOINT ["/bin/bash", "/inside/docker-entrypoint.sh"]
CMD ["/bin/bash", "/inside/build-package"]
