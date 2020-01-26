# Build OpenRCT2
FROM alpine:edge AS build-env
RUN apk update \
 && apk add git gcc g++ make cmake jansson-dev libzip-dev curl-dev libressl-dev sdl2-dev speexdsp-dev fontconfig-dev fts-dev icu-dev musl-dev linux-headers \
 && apk add crypto++-dev --update-cache --repository http://dl-3.alpinelinux.org/alpine/edge/testing/ --allow-untrusted

WORKDIR /openrct2
COPY . .
RUN git -c http.sslVerify=false clone --depth 1 https://github.com/OpenRCT2/objects objects-repo \
 && cp -r ./objects-repo/objects ./data/objects \
 && mkdir build \
 && cd build \
 && cmake .. -DCMAKE_C_COMPILER=/usr/bin/gcc -DCMAKE_CXX_COMPILER=/usr/bin/g++ -DCMAKE_BUILD_TYPE=release -DCMAKE_INSTALL_PREFIX=/openrct2-install/usr -DCMAKE_INSTALL_LIBDIR=/openrct2-install/usr/lib -DDISABLE_OPENGL=ON \
 && make -j4 install \
 # copy data folder because something's not right with the install process
 && cp -r ../data/ /openrct2-install/usr/bin/data

# Build runtime image
FROM alpine:latest
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