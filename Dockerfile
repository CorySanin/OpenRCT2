# Build OpenRCT2
FROM node:16-alpine3.13 AS build-env
RUN apk add --no-cache gcc g++ make cmake duktape-dev nlohmann-json libzip-dev curl-dev sdl2-dev speexdsp-dev fontconfig-dev fts-dev icu-dev musl-dev linux-headers

WORKDIR /openrct2

COPY . .

RUN mkdir build \
 && cd build \
 && cmake .. -DCMAKE_CXX_COMPILER=/usr/bin/g++ -DCMAKE_BUILD_TYPE=release -DCMAKE_INSTALL_PREFIX=/openrct2-install/usr -DCMAKE_INSTALL_LIBDIR=/openrct2-install/usr/lib -DDISABLE_OPENGL=ON \
 && make -j4 install \
 && rm /openrct2-install/usr/lib/libopenrct2.a

# Build runtime image
FROM node:16-alpine3.13
COPY --from=build-env /openrct2-install /openrct2-install
WORKDIR /usr/src/saveprep
COPY ./config /home/node/.config/OpenRCT2/
COPY ./saveprep-node .
RUN apk add --no-cache rsync ca-certificates libpng libzip libcurl duktape freetype fontconfig icu \
 && rsync -a /openrct2-install/* / \
 && rm -rf /openrct2-install \
 && openrct2-cli --version \
 && npm install \
 && chown -R node:node /home/node/.config/OpenRCT2
USER node
EXPOSE 8080

# Test run and scan
RUN openrct2-cli --version \
 && openrct2-cli scan-objects

CMD [ "node", "index.js" ]