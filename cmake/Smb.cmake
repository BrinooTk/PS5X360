set(LIBSMB2_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/.deps/libsmb2")

set(LIBSMB2_SOURCES
  "${LIBSMB2_SOURCE}/lib/aes.c"
  "${LIBSMB2_SOURCE}/lib/aes128ccm.c"
  "${LIBSMB2_SOURCE}/lib/alloc.c"
  "${LIBSMB2_SOURCE}/lib/asn1-ber.c"
  "${LIBSMB2_SOURCE}/lib/compat.c"
  "${LIBSMB2_SOURCE}/lib/dcerpc.c"
  "${LIBSMB2_SOURCE}/lib/dcerpc-lsa.c"
  "${LIBSMB2_SOURCE}/lib/dcerpc-srvsvc.c"
  "${LIBSMB2_SOURCE}/lib/errors.c"
  "${LIBSMB2_SOURCE}/lib/hmac.c"
  "${LIBSMB2_SOURCE}/lib/hmac-md5.c"
  "${LIBSMB2_SOURCE}/lib/init.c"
  "${LIBSMB2_SOURCE}/lib/krb5-wrapper.c"
  "${LIBSMB2_SOURCE}/lib/libsmb2.c"
  "${LIBSMB2_SOURCE}/lib/md4c.c"
  "${LIBSMB2_SOURCE}/lib/md5.c"
  "${LIBSMB2_SOURCE}/lib/ntlmssp.c"
  "${LIBSMB2_SOURCE}/lib/pdu.c"
  "${LIBSMB2_SOURCE}/lib/sha1.c"
  "${LIBSMB2_SOURCE}/lib/sha224-256.c"
  "${LIBSMB2_SOURCE}/lib/sha384-512.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-close.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-create.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-echo.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-error.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-flush.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-ioctl.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-lock.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-logoff.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-negotiate.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-notify-change.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-oplock-break.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-query-directory.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-query-info.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-read.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-session-setup.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-set-info.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-tree-connect.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-tree-disconnect.c"
  "${LIBSMB2_SOURCE}/lib/smb2-cmd-write.c"
  "${LIBSMB2_SOURCE}/lib/smb2-data-file-info.c"
  "${LIBSMB2_SOURCE}/lib/smb2-data-filesystem-info.c"
  "${LIBSMB2_SOURCE}/lib/smb2-data-reparse-point.c"
  "${LIBSMB2_SOURCE}/lib/smb2-data-security-descriptor.c"
  "${LIBSMB2_SOURCE}/lib/smb2-share-enum.c"
  "${LIBSMB2_SOURCE}/lib/smb2-signing.c"
  "${LIBSMB2_SOURCE}/lib/smb3-seal.c"
  "${LIBSMB2_SOURCE}/lib/socket.c"
  "${LIBSMB2_SOURCE}/lib/spnego-wrapper.c"
  "${LIBSMB2_SOURCE}/lib/sync.c"
  "${LIBSMB2_SOURCE}/lib/timestamps.c"
  "${LIBSMB2_SOURCE}/lib/unicode.c"
)

add_library(xenia_libsmb2 STATIC ${LIBSMB2_SOURCES})
target_include_directories(xenia_libsmb2 PUBLIC
  "${LIBSMB2_SOURCE}/include"
  "${LIBSMB2_SOURCE}/include/smb2")
target_compile_definitions(xenia_libsmb2 PRIVATE
  NDEBUG
  HAVE_CONFIG_H=0
  _U_=__attribute__\(\(unused\)\))
target_compile_options(xenia_libsmb2 PRIVATE
  -ffunction-sections -fdata-sections -Wno-unused-parameter)

add_library(xenia_smb_client STATIC
  platform/ps5/smb_client.cpp
  src/smb_path_device.cpp)
target_include_directories(xenia_smb_client PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${XENIA_SOURCE}/src")
target_link_libraries(xenia_smb_client PUBLIC xenia_libsmb2 xenia_base_runtime)
target_compile_definitions(xenia_smb_client PRIVATE NDEBUG)
target_compile_options(xenia_smb_client PRIVATE -ffunction-sections -fdata-sections -fno-char8_t)
