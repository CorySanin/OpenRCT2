FROM node:lts-alpine3.19 AS base

# Build OpenRCT2
FROM base AS build-env
RUN apk add --no-cache git gcc g++ make cmake nlohmann-json libzip-dev curl-dev fontconfig-dev icu-dev musl-dev linux-headers

WORKDIR /openrct2

COPY . .

RUN mkdir build \
 && cd build \
 && cmake .. -DCMAKE_CXX_COMPILER=/usr/bin/g++ -DCMAKE_BUILD_TYPE=release -DCMAKE_INSTALL_PREFIX=/openrct2-install/usr -DCMAKE_INSTALL_LIBDIR=/openrct2-install/usr/lib -DDISABLE_OPENGL=ON -DDISABLE_GUI=ON \
 && make -j4 install \
 && rm /openrct2-install/usr/lib/libopenrct2.a \
 # HACK due to issue in cmakelists, move content from cli
 && mv /openrct2-install/usr/share/openrct2-cli/* /openrct2-install/usr/share/openrct2 \
 && rm -rf /openrct2-install/usr/share/openrct2-cli


# Build runtime image
FROM base as deploy
HEALTHCHECK  --timeout=3s \
  CMD curl --fail http://localhost:8080/healthcheck || exit 1
COPY --from=build-env /openrct2-install /openrct2-install
WORKDIR /usr/src/saveprep
COPY ./config /home/node/.config/OpenRCT2/
COPY ./saveprep-node .
RUN apk add --no-cache rsync ca-certificates libpng libzip libcurl freetype fontconfig icu curl \
 && rsync -a /openrct2-install/* / \
 && rm -rf /openrct2-install \
 && openrct2-cli --version \
 && npm install \
 && npm run build \
 && chown -R node:node /home/node/.config/OpenRCT2 \
 && ln -sf /game /rct2
USER node
EXPOSE 8080

# Test run and scan
RUN openrct2-cli --version \
 && openrct2-cli scan-objects

CMD [ "node", "index.js" ]
