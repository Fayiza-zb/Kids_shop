# Stage 1: compile the C server
FROM gcc:13 AS build
WORKDIR /app
COPY server.c .
RUN gcc -O2 -Wall server.c -o server

# Stage 2: small runtime image
FROM debian:bookworm-slim
WORKDIR /app
COPY --from=build /app/server ./server
COPY public ./public
COPY data ./data
EXPOSE 10000
CMD ["./server"]
