FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential python3 doxygen valgrind clang clang-format-14 ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
CMD ["make", "test"]
