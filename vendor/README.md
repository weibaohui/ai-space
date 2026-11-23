# SMB and NFS client libraries

AI-Space distributes these libraries separately from its executable so that
users can replace them with compatible builds. The library source included in
the AI-Space repository corresponds to the libraries distributed with the app.

- libnfs is from https://github.com/sahlberg/libnfs at revision
  `3c293083ee7ee04ee14e4740a04a0be8b4d7e09a`. The client library is LGPL-2.1
  or later. Protocol definitions and generated RPC codecs are under the BSD
  license. See `libnfs/COPYING` and `libnfs/LICENCE-*.txt`.
- libsmb2 is from https://github.com/sahlberg/libsmb2 at revision
  `5fdd128012b384ed2a79293611d7f7dbaaa84cc7`. See `libsmb2/COPYING` and
  `libsmb2/LICENCE-LGPL-2.1.txt`. AI-Space adds a synchronous last-write-time
  setter using the library's existing SMB SET_INFO and event handling.

The checkout excludes standalone tools, examples, tests, and unsupported
console and embedded platform files. The build disables optional Kerberos,
TLS, RDMA, QUIC, and full DCE/RPC dependencies. To build the shared libraries
for macOS, Linux, or Windows, use the same toolchain as the application:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target nfs smb2
```
