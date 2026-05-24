# gRPC Echo Example

`examples/grpc-echo` demonstrates the optional raw-byte unary gRPC plugin.

Build it with gRPC enabled:

```bash
cmake --preset default-tests -DAEVOX_ENABLE_GRPC=ON
cmake --build --preset default-tests --target grpc-echo
```

The example serves HTTP `/health` on port `8080` and an h2c unary echo method
`/aevox.examples.Echo/Echo` on port `50051`.
