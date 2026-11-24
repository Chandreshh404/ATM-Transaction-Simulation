FROM debian:bookworm-slim AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libsqlite3-dev \
    libssl-dev \
    nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DATM_BUILD_TESTS=OFF \
    && cmake --build build --config Release -j2

FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libsqlite3-0 \
    libssl3 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /app/build/backend/atm_backend /app/atm_backend
COPY --from=build /app/database /app/database

EXPOSE 10000
CMD ["/app/atm_backend"]
