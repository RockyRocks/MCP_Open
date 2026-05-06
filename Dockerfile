# syntax=docker/dockerfile:1
FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake g++ make git python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_CSHARP=OFF \
    -DBUILD_EXAMPLE_PLUGIN=ON \
    -DBUILD_DESKTOP_NOTIFICATION_PLUGIN=OFF \
    && cmake --build build --config Release -j$(nproc) \
    && cmake --install build --prefix /opt/mcp

FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /opt/mcp /opt/mcp

ENV PATH="/opt/mcp/bin:${PATH}"
WORKDIR /opt/mcp

ENTRYPOINT ["mcp_server"]
CMD ["--stdio"]
