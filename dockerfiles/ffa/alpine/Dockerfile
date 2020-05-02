# Build duktape library
FROM alpine:3.11.6 AS ducktape-env
RUN apk update \
 && apk add gcc g++ make
WORKDIR /duktape
RUN wget https://duktape.org/duktape-2.5.0.tar.xz -O duktape.tar.xz && tar -xf ./duktape.tar.xz && mv duktape-*/* . && make -f ./Makefile.sharedlibrary && make -f ./Makefile.sharedlibrary install INSTALL_PREFIX="/usr" && ldconfig /etc/ld.so.conf.d && ldconfig -n /usr/lib/
RUN mkdir -p /usr/lib/pkgconfig/ && printf "prefix=/usr\nexec_prefix=\${prefix}\nlibdir=\${exec_prefix}/lib\nincludedir=\${prefix}/include\n\nName: duktape\nDescription: embeddable Javascript engine with a focus on portability and compact footprint\nVersion: 2.5.0\nLibs: -L\${libdir} -lduktape\nCflags: -I\${includedir}" > /usr/lib/pkgconfig/duktape.pc
# Build OpenRCT2
FROM ducktape-env AS build-env
RUN apk update \
 && apk add git gcc g++ make cmake jansson-dev libzip-dev curl-dev libressl-dev sdl2-dev speexdsp-dev fontconfig-dev fts-dev icu-dev musl-dev linux-headers ninja
WORKDIR /openrct2
COPY . .
RUN mkdir build \
 && cd build \
 && cmake .. -G Ninja -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++ -DCMAKE_BUILD_TYPE=release -DCMAKE_INSTALL_PREFIX=/openrct2-install/usr -DCMAKE_INSTALL_LIBDIR=/openrct2-install/usr/lib -DDISABLE_OPENGL=ON \
 && ninja -k0 install

# Build runtime image
FROM ducktape-env
RUN apk update
RUN apk upgrade
RUN apk add rsync ca-certificates jansson libpng libzip libcurl freetype fontconfig icu

COPY --from=build-env /openrct2-install /openrct2-install
RUN rsync -a /openrct2-install/* / \
 && rm -rf /openrct2-install \
 && openrct2-cli --version

# Set up ordinary user
RUN addgroup -S openrct2 && adduser -S openrct2 -G openrct2
USER openrct2
WORKDIR /home/openrct2
EXPOSE 11753

# Test run and scan
RUN openrct2-cli --version \
 && openrct2-cli scan-objects

# Done
ENTRYPOINT ["openrct2-cli"]