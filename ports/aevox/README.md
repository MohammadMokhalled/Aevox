# Aevox vcpkg Overlay Skeleton

This private overlay is a release-engineering skeleton for AEV-023. It downloads the source archive
published by the GitHub Release workflow:

```text
https://github.com/MohammadMokhalled/Aevox/releases/download/vX.Y.Z/aevox-X.Y.Z-source.tar.gz
```

Before using it for a real release, replace `SHA512 0` in `portfile.cmake` with the hash printed by
vcpkg for the published artifact, then validate the port against that release asset.

The initial v0.3 workflow keeps this overlay private. Official vcpkg registry submission is deferred.
