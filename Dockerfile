FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends cmake g++ make libcurl4-openssl-dev ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY native/ native/
COPY LICENSE SPEC.md SECURITY.md ./
COPY docs/ docs/
RUN cmake -S native -B /build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/vora \
    && cmake --build /build --parallel 2 \
    && ctest --test-dir /build --output-on-failure \
    && cmake --install /build

FROM debian:bookworm-slim
ARG VORA_REVISION=unknown
LABEL org.opencontainers.image.title="Vora" \
      org.opencontainers.image.description="Native C++17 language for bounded local AI agent workflows" \
      org.opencontainers.image.source="https://github.com/muneebshinwari1/vora-lang" \
      org.opencontainers.image.licenses="MIT" \
      org.opencontainers.image.revision="${VORA_REVISION}"
RUN apt-get update && apt-get install -y --no-install-recommends libcurl4 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --uid 10001 --create-home vora \
    && mkdir /workspace && chown vora:vora /workspace
COPY --from=build /opt/vora/ /opt/vora/
ENV PATH="/opt/vora/bin:${PATH}"
USER 10001:10001
WORKDIR /workspace
ENTRYPOINT ["vora"]
CMD ["--help"]
