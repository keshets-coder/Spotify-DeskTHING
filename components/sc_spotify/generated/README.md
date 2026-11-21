nanopb C files generated from `../protobuf/*.proto`, so building needs no
`protoc`. To regenerate (Python with `grpcio-tools` and `protobuf`):

```sh
cd components/sc_spotify
for f in protobuf/*.proto; do
  b=$(basename "$f" .proto)
  python -m grpc_tools.protoc -I . -I protobuf -I ../bell/external/nanopb/generator/proto \
    --include_imports --descriptor_set_out "/tmp/$b.pb.desc" "$f"
  python ../bell/external/nanopb/generator/nanopb_generator.py \
    --output-dir generated --options-path protobuf "/tmp/$b.pb.desc"
done
```
