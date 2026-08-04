DIST_ROOT=$1
LIB_NAME=nghttp2
DIST_DIR="${DIST_ROOT}/${LIB_NAME}"

dist_lib ${LIB_NAME} ${DIST_DIR} $DISTF_NATIVES

# Note: nghttp2 1.70.0 only builds a shared library (libnghttp2.so/.dylib and
# nghttp2.dll+import lib); it has no static archive on any platform, so we can't
# assemble an xcframework for it. The shared libs are distributed as-is by
# dist_lib above.
