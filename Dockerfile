FROM debian:bookworm-slim AS tools
RUN apt-get update && apt-get install -y --no-install-recommends gcc cmake ninja-build python3 \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY CMakeLists.txt ./
COPY cmake ./cmake
COPY src ./src
COPY include ./include
COPY tests ./tests
COPY configs ./configs
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel

FROM tools AS test
ENTRYPOINT ["python3", "tests/run.py", "--binary", "build/gallery"]
CMD ["--jobs", "4"]

FROM tools AS experiments
ENTRYPOINT ["python3", "tests/experiments.py", "--binary", "build/gallery"]
CMD ["--jobs", "4", "--output", "/output/experiments"]

FROM debian:bookworm-slim AS runtime
COPY --from=tools /app/build/gallery /usr/local/bin/gallery
COPY configs /app/configs
WORKDIR /output
ENTRYPOINT ["gallery"]
CMD ["--config", "/app/configs/demo.env"]
