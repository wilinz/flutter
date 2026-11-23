# Wayland protocols

Client code generated from wayland-protocols 1.45
(`staging/color-management/color-management-v1.xml`) with wayland-scanner
1.19.0, the last release whose output builds against libwayland 1.18 (the
sysroot version):

```
wayland-scanner client-header color-management-v1.xml color-management-v1-client-protocol.h
wayland-scanner private-code color-management-v1.xml color-management-v1-protocol.c
```

The generated files are checked in because the Linux sysroot does not ship
this protocol.
