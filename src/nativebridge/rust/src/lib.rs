#![allow(non_camel_case_types)]
#![allow(unused_macros)]
#![allow(non_upper_case_globals)]
#![allow(nonstandard_style)]
#![allow(dead_code)]
#![allow(private_interfaces)]

use libc::{c_char, c_double, c_int, c_long, c_uint, c_void, intptr_t, size_t};
use std::collections::HashMap;
use std::mem;
use std::ptr;
use std::sync::atomic::{AtomicPtr, Ordering};
use std::sync::{Mutex, OnceLock};
use std::time::Instant;

pub type CURL = c_void;
pub type CURLM = c_void;
pub type CURLSH = c_void;
pub type CURLU = c_void;
pub type curl_mime = c_void;
pub type curl_mimepart = c_void;

#[cfg(windows)]
pub type curl_socket_t = libc::c_uint;
#[cfg(not(windows))]
pub type curl_socket_t = c_int;

pub type curl_off_t = i64;
pub type CURLoption = c_int;
pub type CURLINFO = c_int;
pub type CURLcode = c_int;
pub type CURLMoption = c_int;
pub type CURLMcode = c_int;
pub type CURLSHoption = c_int;
pub type CURLSHcode = c_int;
pub type CURLversion = c_int;
pub type CURLMSG = c_uint;
pub type CURLHcode = c_int;
pub type CURLUPart = c_uint;
pub type CURLUcode = c_uint;
pub type curl_lock_data = c_int;
pub type curl_lock_access = c_int;
pub type curlioerr = c_int;
pub type curl_infotype = c_int;

pub const CURLE_OK: CURLcode = 0;
pub const CURLE_FAILED_INIT: CURLcode = 2;
pub const CURLE_OUT_OF_MEMORY: CURLcode = 27;
pub const CURLE_BAD_FUNCTION_ARGUMENT: CURLcode = 43;

pub const CURLM_OK: CURLMcode = 0;
pub const CURLM_BAD_FUNCTION_ARGUMENT: CURLMcode = 10;

pub const CURLSHE_OK: CURLSHcode = 0;
pub const CURLSHE_BAD_OPTION: CURLSHcode = 1;
pub const CURLSHE_INVALID: CURLSHcode = 3;
pub const CURLSHE_NOMEM: CURLSHcode = 4;

pub const CURLHE_OK: CURLHcode = 0;
pub const CURLHE_BAD_INDEX: CURLHcode = 1;
pub const CURLHE_MISSING_HEADER: CURLHcode = 2;
pub const CURLHE_NO_HEADER: CURLHcode = 3;
pub const CURLHE_OUT_OF_MEMORY: CURLHcode = 4;

pub const CURLUE_OK: CURLUcode = 0;

pub const CURLVERSION_NOW: CURLversion = 12;
pub const CURLMSG_DONE: CURLMSG = 1;

pub const CURLUPART_URL: CURLUPart = 0;
pub const CURLUPART_SCHEME: CURLUPart = 1;
pub const CURLUPART_USER: CURLUPart = 2;
pub const CURLUPART_PASSWORD: CURLUPart = 3;
pub const CURLUPART_OPTIONS: CURLUPart = 4;
pub const CURLUPART_HOST: CURLUPart = 5;
pub const CURLUPART_PORT: CURLUPart = 6;
pub const CURLUPART_PATH: CURLUPart = 7;
pub const CURLUPART_QUERY: CURLUPart = 8;
pub const CURLUPART_FRAGMENT: CURLUPart = 9;
pub const CURLUPART_ZONEID: CURLUPart = 10;

pub const CURLU_DEFAULT_PORT: c_uint = 1 << 0;
pub const CURLU_NO_DEFAULT_PORT: c_uint = 1 << 1;
pub const CURLU_DEFAULT_SCHEME: c_uint = 1 << 2;
pub const CURLU_NON_SUPPORT_SCHEME: c_uint = 1 << 3;
pub const CURLU_PATH_AS_IS: c_uint = 1 << 4;
pub const CURLU_DISALLOW_USER: c_uint = 1 << 5;
pub const CURLU_URLDECODE: c_uint = 1 << 6;
pub const CURLU_URLENCODE: c_uint = 1 << 7;
pub const CURLU_APPENDQUERY: c_uint = 1 << 8;
pub const CURLU_GUESS_SCHEME: c_uint = 1 << 9;
pub const CURLU_NO_AUTHORITY: c_uint = 1 << 10;
pub const CURLU_ALLOW_SPACE: c_uint = 1 << 11;
pub const CURLU_PUNYCODE: c_uint = 1 << 12;
pub const CURLU_PUNY2IDN: c_uint = 1 << 13;

pub const CURLOPTTYPE_LONG: c_int = 0;
pub const CURLOPTTYPE_OBJECTPOINT: c_int = 10000;
pub const CURLOPTTYPE_FUNCTIONPOINT: c_int = 20000;
pub const CURLOPTTYPE_OFF_T: c_int = 30000;
pub const CURLOPTTYPE_BLOB: c_int = 40000;

pub const CURLINFO_TYPEMASK: c_int = 0xf00000;
pub const CURLINFO_STRING: c_int = 0x100000;
pub const CURLINFO_LONG: c_int = 0x200000;
pub const CURLINFO_DOUBLE: c_int = 0x300000;
pub const CURLINFO_SLIST: c_int = 0x400000;
pub const CURLINFO_SOCKET: c_int = 0x500000;
pub const CURLINFO_OFF_T: c_int = 0x600000;

macro_rules! opt_long {
    ($n:expr) => { CURLOPTTYPE_LONG + $n };
}
macro_rules! opt_obj {
    ($n:expr) => { CURLOPTTYPE_OBJECTPOINT + $n };
}
macro_rules! opt_func {
    ($n:expr) => { CURLOPTTYPE_FUNCTIONPOINT + $n };
}
macro_rules! opt_offt {
    ($n:expr) => { CURLOPTTYPE_OFF_T + $n };
}

pub const CURLOPT_WRITEDATA: CURLoption = opt_obj!(1);
pub const CURLOPT_URL: CURLoption = opt_obj!(2);
pub const CURLOPT_PORT: CURLoption = opt_long!(3);
pub const CURLOPT_PROXY: CURLoption = opt_obj!(4);
pub const CURLOPT_USERPWD: CURLoption = opt_obj!(5);
pub const CURLOPT_PROXYUSERPWD: CURLoption = opt_obj!(6);
pub const CURLOPT_RANGE: CURLoption = opt_obj!(7);
pub const CURLOPT_READDATA: CURLoption = opt_obj!(9);
pub const CURLOPT_ERRORBUFFER: CURLoption = opt_obj!(10);
pub const CURLOPT_WRITEFUNCTION: CURLoption = opt_func!(11);
pub const CURLOPT_READFUNCTION: CURLoption = opt_func!(12);
pub const CURLOPT_TIMEOUT: CURLoption = opt_long!(13);
pub const CURLOPT_TIMEOUT_MS: CURLoption = opt_long!(155);
pub const CURLOPT_INFILESIZE: CURLoption = opt_long!(14);
pub const CURLOPT_POSTFIELDS: CURLoption = opt_obj!(15);
pub const CURLOPT_REFERER: CURLoption = opt_obj!(16);
pub const CURLOPT_FTPPORT: CURLoption = opt_obj!(17);
pub const CURLOPT_USERAGENT: CURLoption = opt_obj!(18);
pub const CURLOPT_LOW_SPEED_LIMIT: CURLoption = opt_long!(19);
pub const CURLOPT_LOW_SPEED_TIME: CURLoption = opt_long!(20);
pub const CURLOPT_RESUME_FROM: CURLoption = opt_long!(21);
pub const CURLOPT_COOKIE: CURLoption = opt_obj!(22);
pub const CURLOPT_HTTPHEADER: CURLoption = opt_obj!(23);
pub const CURLOPT_HTTPPOST: CURLoption = opt_obj!(24);
pub const CURLOPT_SSLCERT: CURLoption = opt_obj!(25);
pub const CURLOPT_KEYPASSWD: CURLoption = opt_obj!(26);
pub const CURLOPT_CUSTOMREQUEST: CURLoption = opt_obj!(36);
pub const CURLOPT_VERBOSE: CURLoption = opt_long!(41);
pub const CURLOPT_HEADER: CURLoption = opt_long!(42);
pub const CURLOPT_NOBODY: CURLoption = opt_long!(44);
pub const CURLOPT_UPLOAD: CURLoption = opt_long!(46);
pub const CURLOPT_POST: CURLoption = opt_long!(47);
pub const CURLOPT_PUT: CURLoption = opt_long!(54);
pub const CURLOPT_POSTFIELDSIZE: CURLoption = opt_long!(60);
pub const CURLOPT_SSL_VERIFYPEER: CURLoption = opt_long!(64);
pub const CURLOPT_CAINFO: CURLoption = opt_obj!(65);
pub const CURLOPT_FOLLOWLOCATION: CURLoption = opt_long!(52);
pub const CURLOPT_PROXYPORT: CURLoption = opt_long!(59);
pub const CURLOPT_HTTPGET: CURLoption = opt_long!(80);
pub const CURLOPT_SSL_VERIFYHOST: CURLoption = opt_long!(81);
pub const CURLOPT_HTTP_VERSION: CURLoption = opt_long!(84);
pub const CURLOPT_NOSIGNAL: CURLoption = opt_long!(99);
pub const CURLOPT_PROXYTYPE: CURLoption = opt_long!(101);
pub const CURLOPT_SHARE: CURLoption = opt_obj!(101);
pub const CURLOPT_PRIVATE: CURLoption = opt_obj!(103);
pub const CURLOPT_ENCODING: CURLoption = opt_obj!(102);
pub const CURLOPT_ACCEPT_ENCODING: CURLoption = CURLOPT_ENCODING;
pub const CURLOPT_CAPATH: CURLoption = opt_obj!(97);
pub const CURLOPT_CONNECTTIMEOUT: CURLoption = opt_long!(78);
pub const CURLOPT_CONNECTTIMEOUT_MS: CURLoption = opt_long!(156);
pub const CURLOPT_SSLVERSION: CURLoption = opt_long!(32);
pub const CURLOPT_INTERFACE: CURLoption = opt_obj!(62);
pub const CURLOPT_DNS_CACHE_TIMEOUT: CURLoption = opt_long!(92);
pub const CURLOPT_DNS_SERVERS: CURLoption = opt_obj!(255);
pub const CURLOPT_DNS_LOCAL_IP4: CURLoption = opt_obj!(263);
pub const CURLOPT_DNS_LOCAL_IP6: CURLoption = opt_obj!(264);
pub const CURLOPT_RESOLVE: CURLoption = opt_obj!(203);
pub const CURLOPT_USE_SSL: CURLoption = opt_long!(119);
pub const CURLOPT_SSL_OPTIONS: CURLoption = opt_long!(216);
pub const CURLOPT_HTTPAUTH: CURLoption = opt_long!(107);
pub const CURLOPT_PROXYAUTH: CURLoption = opt_long!(111);
pub const CURLOPT_SSH_AUTH_TYPES: CURLoption = opt_long!(152);
pub const CURLOPT_PROTOCOLS: CURLoption = opt_long!(181);
pub const CURLOPT_REDIR_PROTOCOLS: CURLoption = opt_long!(182);
pub const CURLOPT_POSTREDIR: CURLoption = opt_long!(161);
pub const CURLOPT_SOCKS5_AUTH: CURLoption = opt_long!(233);
pub const CURLOPT_SOCKOPTDATA: CURLoption = opt_obj!(148);
pub const CURLOPT_OPENSOCKETFUNCTION: CURLoption = opt_func!(163);
pub const CURLOPT_OPENSOCKETDATA: CURLoption = opt_obj!(164);
pub const CURLOPT_CLOSESOCKETFUNCTION: CURLoption = opt_func!(208);
pub const CURLOPT_CLOSESOCKETDATA: CURLoption = opt_obj!(209);
pub const CURLOPT_HEADERDATA: CURLoption = opt_obj!(29);
pub const CURLOPT_HEADERFUNCTION: CURLoption = opt_func!(79);
pub const CURLOPT_COPYPOSTFIELDS: CURLoption = opt_obj!(166);
pub const CURLOPT_POSTFIELDSIZE_LARGE: CURLoption = opt_offt!(120);
pub const CURLOPT_INFILESIZE_LARGE: CURLoption = opt_offt!(115);
pub const CURLOPT_RESUME_FROM_LARGE: CURLoption = opt_offt!(116);
pub const CURLOPT_MAX_RECV_SPEED_LARGE: CURLoption = opt_offt!(305);
pub const CURLOPT_MAX_SEND_SPEED_LARGE: CURLoption = opt_offt!(306);
pub const CURLOPT_ALTSVC_CTRL: CURLoption = opt_long!(250);
pub const CURLOPT_MIMEPOST: CURLoption = opt_obj!(267);
pub const CURLOPT_INTERLEAVEDATA: CURLoption = opt_obj!(265);
pub const CURLOPT_INTERLEAVEFUNCTION: CURLoption = opt_func!(266);

pub const CURLINFO_RESPONSE_CODE: CURLINFO = CURLINFO_LONG + 0x2;
pub const CURLINFO_HTTP_VERSION: CURLINFO = CURLINFO_LONG + 0x22;
pub const CURLINFO_TOTAL_TIME: CURLINFO = CURLINFO_DOUBLE + 0x3;
pub const CURLINFO_NAMELOOKUP_TIME: CURLINFO = CURLINFO_DOUBLE + 0x4;
pub const CURLINFO_CONNECT_TIME: CURLINFO = CURLINFO_DOUBLE + 0x5;
pub const CURLINFO_APPCONNECT_TIME: CURLINFO = CURLINFO_DOUBLE + 0x21;
pub const CURLINFO_PRETRANSFER_TIME: CURLINFO = CURLINFO_DOUBLE + 0x6;
pub const CURLINFO_STARTTRANSFER_TIME: CURLINFO = CURLINFO_DOUBLE + 0x7;
pub const CURLINFO_REDIRECT_TIME: CURLINFO = CURLINFO_DOUBLE + 0x1d;
pub const CURLINFO_REDIRECT_COUNT: CURLINFO = CURLINFO_LONG + 0x14;
pub const CURLINFO_REDIRECT_URL: CURLINFO = CURLINFO_STRING + 0x1f;
pub const CURLINFO_EFFECTIVE_URL: CURLINFO = CURLINFO_STRING + 0x1;
pub const CURLINFO_CONTENT_TYPE: CURLINFO = CURLINFO_STRING + 0x12;
pub const CURLINFO_CONTENT_LENGTH_DOWNLOAD: CURLINFO = CURLINFO_DOUBLE + 0xf;
pub const CURLINFO_CONTENT_LENGTH_UPLOAD: CURLINFO = CURLINFO_DOUBLE + 0x10;
pub const CURLINFO_SIZE_DOWNLOAD: CURLINFO = CURLINFO_DOUBLE + 0x8;
pub const CURLINFO_SIZE_UPLOAD: CURLINFO = CURLINFO_DOUBLE + 0xe;
pub const CURLINFO_SPEED_DOWNLOAD: CURLINFO = CURLINFO_DOUBLE + 0x9;
pub const CURLINFO_SPEED_UPLOAD: CURLINFO = CURLINFO_DOUBLE + 0xb;
pub const CURLINFO_HEADER_SIZE: CURLINFO = CURLINFO_LONG + 0x15;
pub const CURLINFO_REQUEST_SIZE: CURLINFO = CURLINFO_LONG + 0x16;
pub const CURLINFO_SSL_VERIFYRESULT: CURLINFO = CURLINFO_LONG + 0x17;
pub const CURLINFO_FILETIME: CURLINFO = CURLINFO_LONG + 0x18;
pub const CURLINFO_FILETIME_T: CURLINFO = CURLINFO_OFF_T + 0x3c;
pub const CURLINFO_HTTPAUTH_AVAIL: CURLINFO = CURLINFO_LONG + 0x1b;
pub const CURLINFO_PROXYAUTH_AVAIL: CURLINFO = CURLINFO_LONG + 0x1c;
pub const CURLINFO_OS_ERRNO: CURLINFO = CURLINFO_LONG + 0x19;
pub const CURLINFO_NUM_CONNECTS: CURLINFO = CURLINFO_LONG + 0x1a;
pub const CURLINFO_PRIMARY_IP: CURLINFO = CURLINFO_STRING + 0x20;
pub const CURLINFO_PRIMARY_PORT: CURLINFO = CURLINFO_LONG + 0x24;
pub const CURLINFO_LOCAL_IP: CURLINFO = CURLINFO_STRING + 0x23;
pub const CURLINFO_LOCAL_PORT: CURLINFO = CURLINFO_LONG + 0x25;
pub const CURLINFO_COOKIELIST: CURLINFO = CURLINFO_SLIST + 0x13;
pub const CURLINFO_LASTSOCKET: CURLINFO = CURLINFO_LONG + 0x1d;
pub const CURLINFO_ACTIVESOCKET: CURLINFO = CURLINFO_SOCKET + 0x2e;
pub const CURLINFO_CERTINFO: CURLINFO = CURLINFO_SLIST + 0x3a;
pub const CURLINFO_PRIVATE: CURLINFO = CURLINFO_STRING + 0x29;
pub const CURLINFO_RETRY_AFTER: CURLINFO = CURLINFO_OFF_T + 0x40;
pub const CURLINFO_HTTP_CONNECTCODE: CURLINFO = CURLINFO_LONG + 0x26;
pub const CURLINFO_PROTOCOL: CURLINFO = CURLINFO_LONG + 0x36;
pub const CURLINFO_SCHEME: CURLINFO = CURLINFO_STRING + 0x37;
pub const CURLINFO_APPCONNECT_TIME_T: CURLINFO = CURLINFO_OFF_T + 0x3d;
pub const CURLINFO_CONNECT_TIME_T: CURLINFO = CURLINFO_OFF_T + 0x3b;
pub const CURLINFO_NAMELOOKUP_TIME_T: CURLINFO = CURLINFO_OFF_T + 0x3e;
pub const CURLINFO_PRETRANSFER_TIME_T: CURLINFO = CURLINFO_OFF_T + 0x3f;
pub const CURLINFO_REDIRECT_TIME_T: CURLINFO = CURLINFO_OFF_T + 0x41;
pub const CURLINFO_STARTTRANSFER_TIME_T: CURLINFO = CURLINFO_OFF_T + 0x42;
pub const CURLINFO_TOTAL_TIME_T: CURLINFO = CURLINFO_OFF_T + 0x3a;
pub const CURLINFO_SIZE_DOWNLOAD_T: CURLINFO = CURLINFO_OFF_T + 0x30;
pub const CURLINFO_SIZE_UPLOAD_T: CURLINFO = CURLINFO_OFF_T + 0x31;
pub const CURLINFO_SPEED_DOWNLOAD_T: CURLINFO = CURLINFO_OFF_T + 0x33;
pub const CURLINFO_SPEED_UPLOAD_T: CURLINFO = CURLINFO_OFF_T + 0x34;
pub const CURLINFO_CONTENT_LENGTH_DOWNLOAD_T: CURLINFO = CURLINFO_OFF_T + 0x35;
pub const CURLINFO_CONTENT_LENGTH_UPLOAD_T: CURLINFO = CURLINFO_OFF_T + 0x36;
pub const CURLINFO_EFFECTIVE_METHOD: CURLINFO = CURLINFO_STRING + 0x42;
pub const CURLINFO_XFER_ID: CURLINFO = CURLINFO_OFF_T + 0x45;
pub const CURLINFO_CONN_ID: CURLINFO = CURLINFO_OFF_T + 0x46;
pub const CURLINFO_TLS_SSL_PTR: CURLINFO = 0x400000 + 0x2b;

pub const CURLMOPT_SOCKETFUNCTION: CURLMoption = opt_func!(1);
pub const CURLMOPT_SOCKETDATA: CURLMoption = opt_obj!(2);
pub const CURLMOPT_PIPELINING: CURLMoption = opt_long!(3);
pub const CURLMOPT_TIMERFUNCTION: CURLMoption = opt_func!(4);
pub const CURLMOPT_TIMERDATA: CURLMoption = opt_obj!(5);
pub const CURLMOPT_MAXCONNECTS: CURLMoption = opt_long!(6);
pub const CURLMOPT_MAX_HOST_CONNECTIONS: CURLMoption = opt_long!(7);
pub const CURLMOPT_MAX_TOTAL_CONNECTIONS: CURLMoption = opt_long!(13);

pub const CURLSHOPT_SHARE: CURLSHoption = 1;
pub const CURLSHOPT_UNSHARE: CURLSHoption = 2;
pub const CURLSHOPT_LOCKFUNC: CURLSHoption = 3;
pub const CURLSHOPT_UNLOCKFUNC: CURLSHoption = 4;
pub const CURLSHOPT_USERDATA: CURLSHoption = 5;

pub const CURL_LOCK_DATA_SHARE: curl_lock_data = 0;
pub const CURL_LOCK_DATA_COOKIE: curl_lock_data = 1;
pub const CURL_LOCK_DATA_DNS: curl_lock_data = 2;
pub const CURL_LOCK_DATA_SSL_SESSION: curl_lock_data = 3;
pub const CURL_LOCK_DATA_CONNECT: curl_lock_data = 4;
pub const CURL_LOCK_DATA_PSL: curl_lock_data = 5;
pub const CURL_LOCK_DATA_HSTS: curl_lock_data = 6;
pub const CURL_LOCK_DATA_LAST: curl_lock_data = 7;

pub const CURL_LOCK_ACCESS_NONE: curl_lock_access = 0;
pub const CURL_LOCK_ACCESS_SHARED: curl_lock_access = 1;
pub const CURL_LOCK_ACCESS_SINGLE: curl_lock_access = 2;

pub const CURL_VERSION_IPV6: c_int = 1;
pub const CURL_VERSION_KERBEROS4: c_int = 2;
pub const CURL_VERSION_SSL: c_int = 4;
pub const CURL_VERSION_LIBZ: c_int = 8;
pub const CURL_VERSION_NTLM: c_int = 16;
pub const CURL_VERSION_GSSNEGOTIATE: c_int = 32;
pub const CURL_VERSION_DEBUG: c_int = 64;
pub const CURL_VERSION_ASYNCHDNS: c_int = 128;
pub const CURL_VERSION_SPNEGO: c_int = 256;
pub const CURL_VERSION_HTTP2: c_int = 65536;
pub const CURL_VERSION_BROTLI: c_int = 1 << 20;
pub const CURL_VERSION_HTTP3: c_int = 1 << 25;
pub const CURL_VERSION_ZSTD: c_int = 1 << 26;

pub const CURL_HTTP_VERSION_1_0: c_long = 1;
pub const CURL_HTTP_VERSION_1_1: c_long = 2;
pub const CURL_HTTP_VERSION_2_0: c_long = 3;
pub const CURL_HTTP_VERSION_2TLS: c_long = 4;
pub const CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE: c_long = 5;
pub const CURL_HTTP_VERSION_3: c_long = 30;
pub const CURL_HTTP_VERSION_3ONLY: c_long = 31;

pub const CURL_SSLVERSION_TLSv1: c_long = 1;
pub const CURL_SSLVERSION_TLSv1_0: c_long = 4;
pub const CURL_SSLVERSION_TLSv1_1: c_long = 5;
pub const CURL_SSLVERSION_TLSv1_2: c_long = 6;
pub const CURL_SSLVERSION_TLSv1_3: c_long = 7;

pub const CURLPROXY_HTTP: c_long = 0;
pub const CURLPROXY_HTTPS: c_long = 2;
pub const CURLPROXY_SOCKS4: c_long = 4;
pub const CURLPROXY_SOCKS5: c_long = 5;
pub const CURLPROXY_SOCKS5_HOSTNAME: c_long = 5;

pub const CURL_ZERO_TERMINATED: isize = -1;

pub const CURLH_HEADER: c_uint = 1 << 0;
pub const CURLH_TRAILER: c_uint = 1 << 1;
pub const CURLH_CONNECT: c_uint = 1 << 2;
pub const CURLH_1XX: c_uint = 1 << 3;
pub const CURLH_PSEUDO: c_uint = 1 << 4;

pub const CURLWS_TEXT: c_uint = 1 << 0;
pub const CURLWS_BINARY: c_uint = 1 << 1;
pub const CURLWS_CONT: c_uint = 1 << 2;
pub const CURLWS_CLOSE: c_uint = 1 << 3;
pub const CURLWS_PING: c_uint = 1 << 4;
pub const CURLWS_PONG: c_uint = 1 << 5;
pub const CURLWS_OFFSET: c_uint = 1 << 6;

pub const CURL_BLOB_COPY: c_uint = 1;
pub const CURL_BLOB_NOCOPY: c_uint = 0;

#[cfg(windows)]
pub const CURL_SOCKET_BAD: curl_socket_t = !0;
#[cfg(not(windows))]
pub const CURL_SOCKET_BAD: curl_socket_t = -1;

#[repr(C)]
pub struct curl_slist {
    pub data: *mut c_char,
    pub next: *mut curl_slist,
}

#[repr(C)]
pub struct curl_waitfd {
    pub fd: curl_socket_t,
    pub events: c_short,
    pub revents: c_short,
}

#[cfg(windows)]
pub type c_short = i16;
#[cfg(not(windows))]
pub type c_short = i16;

#[repr(C)]
pub struct CURLMsg {
    pub msg: CURLMSG,
    pub easy_handle: *mut CURL,
    pub data: [u8; 64],
}

impl CURLMsg {
    unsafe fn result_code(&self) -> CURLcode {
        ptr::read_unaligned(self.data.as_ptr() as *const CURLcode)
    }
}

#[repr(C)]
pub struct curl_blob {
    pub data: *mut c_void,
    pub len: size_t,
    pub flags: c_uint,
}

#[repr(C)]
pub struct curl_version_info_data {
    pub age: CURLversion,
    pub version: *const c_char,
    pub version_num: c_uint,
    pub host: *const c_char,
    pub features: c_int,
    pub ssl_version: *const c_char,
    pub ssl_version_num: c_long,
    pub libz_version: *const c_char,
    pub protocols: *const *const c_char,
    pub ares: *const c_char,
    pub ares_num: c_int,
    pub libidn: *const c_char,
    pub iconv_ver_num: c_int,
    pub libssh_version: *const c_char,
    pub brotli_ver_num: c_uint,
    pub brotli_version: *const c_char,
    pub nghttp2_ver_num: c_uint,
    pub nghttp2_version: *const c_char,
    pub quic_version: *const c_char,
    pub cainfo: *const c_char,
    pub capath: *const c_char,
    pub zstd_ver_num: c_uint,
    pub zstd_version: *const c_char,
    pub hyper_version: *const c_char,
    pub gsasl_version: *const c_char,
    pub feature_names: *const *const c_char,
}

#[repr(C)]
pub struct curl_header {
    pub name: *mut c_char,
    pub value: *mut c_char,
    pub amount: size_t,
    pub index: size_t,
    pub origin: c_uint,
    pub anchor: *mut c_void,
}

#[repr(C)]
pub struct curl_ws_frame {
    pub age: c_int,
    pub flags: c_int,
    pub offset: curl_off_t,
    pub bytesleft: curl_off_t,
    pub len: size_t,
}

#[repr(C)]
pub struct curl_sockaddr {
    pub family: c_int,
    pub socktype: c_int,
    pub protocol: c_int,
    pub addrlen: c_uint,
    pub addr: [u8; 128],
}

pub type curl_write_callback = unsafe extern "C" fn(*mut c_char, size_t, size_t, *mut c_void) -> size_t;
pub type curl_read_callback = unsafe extern "C" fn(*mut c_char, size_t, size_t, *mut c_void) -> size_t;
pub type curl_opensocket_callback = unsafe extern "C" fn(*mut c_void, curlsocktype, *mut curl_sockaddr) -> curl_socket_t;
pub type curl_closesocket_callback = unsafe extern "C" fn(*mut c_void, curl_socket_t) -> c_int;
pub type curl_socket_callback = unsafe extern "C" fn(*mut CURL, curl_socket_t, c_int, *mut c_void, *mut c_void) -> c_int;
pub type curl_multi_timer_callback = unsafe extern "C" fn(*mut CURLM, c_long, *mut c_void) -> c_int;
pub type curl_lock_function = unsafe extern "C" fn(*mut CURL, curl_lock_data, curl_lock_access, *mut c_void);
pub type curl_unlock_function = unsafe extern "C" fn(*mut CURL, curl_lock_data, *mut c_void);
pub type curlsocktype = c_int;

#[repr(C)]
pub struct timeval {
    tv_sec: c_long,
    tv_usec: c_long,
}

#[cfg(windows)]
#[repr(C)]
pub struct fd_set {
    pub fd_count: u_int,
    pub fd_array: [curl_socket_t; 64],
}

#[cfg(not(windows))]
pub use libc::fd_set;

pub type u_int = c_uint;

pub const WSAEINTR: c_int = 10004;
pub const WSAETIMEDOUT: c_int = 10060;
pub const SD_BOTH: c_int = 2;

#[cfg(not(windows))]
const ETIMEDOUT: c_int = 110;
#[cfg(not(windows))]
const EINTR: c_int = 4;
#[cfg(not(windows))]
const SHUT_RDWR: c_int = 2;

#[cfg(windows)]
extern "system" {
    pub fn WSAGetLastError() -> c_int;
    pub fn WSASetLastError(iErr: c_int);
    pub fn select(
        nfds: c_int,
        readfds: *mut fd_set,
        writefds: *mut fd_set,
        exceptfds: *mut fd_set,
        timeout: *mut timeval,
    ) -> c_int;
    pub fn closesocket(s: curl_socket_t) -> c_int;
    pub fn shutdown(s: curl_socket_t, how: c_int) -> c_int;
    pub fn socket(af: c_int, socktype: c_int, protocol: c_int) -> curl_socket_t;
}

#[cfg(not(windows))]
extern "C" {
    pub fn select(
        nfds: c_int,
        readfds: *mut fd_set,
        writefds: *mut fd_set,
        exceptfds: *mut fd_set,
        timeout: *mut timeval,
    ) -> c_int;
    pub fn socket(af: c_int, socktype: c_int, protocol: c_int) -> c_int;
    pub fn close(fd: c_int) -> c_int;
    pub fn shutdown(s: c_int, how: c_int) -> c_int;
}

#[cfg(any(target_os = "linux", target_os = "android"))]
extern "C" {
    fn __errno_location() -> *mut c_int;
}

#[cfg(any(target_os = "macos", target_os = "ios", target_os = "tvos"))]
extern "C" {
    fn __error() -> *mut c_int;
}

#[inline]
unsafe fn errno_ptr() -> *mut c_int {
    #[cfg(any(target_os = "linux", target_os = "android"))]
    {
        __errno_location()
    }
    #[cfg(any(target_os = "macos", target_os = "ios", target_os = "tvos"))]
    {
        __error()
    }
}

extern "C" {
    pub fn curl_version() -> *const c_char;
    pub fn curl_version_info(t: CURLversion) -> *const curl_version_info_data;
    pub fn curl_global_init(flags: c_long) -> CURLcode;
    pub fn curl_global_cleanup();
    pub fn curl_free(p: *mut c_void);

    pub fn curl_easy_init() -> *mut CURL;
    pub fn curl_easy_cleanup(curl: *mut CURL);
    pub fn curl_easy_reset(curl: *mut CURL);
    pub fn curl_easy_duphandle(curl: *mut CURL) -> *mut CURL;
    pub fn curl_easy_perform(curl: *mut CURL) -> CURLcode;
    pub fn curl_easy_pause(handle: *mut CURL, bitmask: c_int) -> CURLcode;
    pub fn curl_easy_upkeep(handle: *mut CURL) -> CURLcode;
    pub fn curl_easy_recv(curl: *mut CURL, buffer: *mut c_void, buflen: size_t, n: *mut size_t) -> CURLcode;
    pub fn curl_easy_send(curl: *mut CURL, buffer: *const c_void, buflen: size_t, n: *mut size_t) -> CURLcode;
    pub fn curl_easy_escape(handle: *mut CURL, string: *const c_char, length: c_int) -> *mut c_char;
    pub fn curl_easy_unescape(handle: *mut CURL, string: *const c_char, inlength: c_int, outlength: *mut c_int) -> *mut c_char;
    pub fn curl_easy_strerror(code: CURLcode) -> *const c_char;

    fn curl_easy_setopt(handle: *mut CURL, option: CURLoption, ...) -> CURLcode;
    fn curl_easy_getinfo(handle: *mut CURL, info: CURLINFO, ...) -> CURLcode;

    pub fn curl_multi_init() -> *mut CURLM;
    pub fn curl_multi_cleanup(multi_handle: *mut CURLM) -> CURLMcode;
    pub fn curl_multi_add_handle(multi_handle: *mut CURLM, curl_handle: *mut CURL) -> CURLMcode;
    pub fn curl_multi_remove_handle(multi_handle: *mut CURLM, curl_handle: *mut CURL) -> CURLMcode;
    pub fn curl_multi_perform(multi_handle: *mut CURLM, running_handles: *mut c_int) -> CURLMcode;
    pub fn curl_multi_fdset(
        multi_handle: *mut CURLM,
        read_fd_set: *mut fd_set,
        write_fd_set: *mut fd_set,
        exc_fd_set: *mut fd_set,
        max_fd: *mut c_int,
    ) -> CURLMcode;
    pub fn curl_multi_timeout(multi_handle: *mut CURLM, milliseconds: *mut c_long) -> CURLMcode;
    pub fn curl_multi_wait(
        multi_handle: *mut CURLM,
        extra_fds: *mut curl_waitfd,
        extra_nfds: c_uint,
        timeout_ms: c_int,
        ret: *mut c_int,
    ) -> CURLMcode;
    pub fn curl_multi_poll(
        multi_handle: *mut CURLM,
        extra_fds: *mut curl_waitfd,
        extra_nfds: c_uint,
        timeout_ms: c_int,
        numfds: *mut c_int,
    ) -> CURLMcode;
    pub fn curl_multi_wakeup(multi_handle: *mut CURLM) -> CURLMcode;
    pub fn curl_multi_strerror(code: CURLMcode) -> *const c_char;
    pub fn curl_multi_info_read(multi_handle: *mut CURLM, msgs_in_queue: *mut c_int) -> *mut CURLMsg;
    pub fn curl_multi_socket_action(
        multi_handle: *mut CURLM,
        s: curl_socket_t,
        ev_bitmask: c_int,
        running_handles: *mut c_int,
    ) -> CURLMcode;
    pub fn curl_multi_assign(multi_handle: *mut CURLM, sockfd: curl_socket_t, sockp: *mut c_void) -> CURLMcode;
    pub fn curl_multi_get_handles(multi_handle: *mut CURLM) -> *mut *mut CURL;

    fn curl_multi_setopt(handle: *mut CURLM, option: CURLMoption, ...) -> CURLMcode;

    pub fn curl_slist_append(list: *mut curl_slist, val: *const c_char) -> *mut curl_slist;
    pub fn curl_slist_free_all(list: *mut curl_slist);

    pub fn curl_share_init() -> *mut CURLSH;
    pub fn curl_share_cleanup(sh: *mut CURLSH) -> CURLSHcode;
    pub fn curl_share_strerror(code: CURLSHcode) -> *const c_char;

    fn curl_share_setopt(sh: *mut CURLSH, opt: CURLSHoption, ...) -> CURLSHcode;

    pub fn curl_mime_init(easy: *mut CURL) -> *mut curl_mime;
    pub fn curl_mime_free(mime: *mut curl_mime);
    pub fn curl_mime_addpart(mime: *mut curl_mime) -> *mut curl_mimepart;
    pub fn curl_mime_name(part: *mut curl_mimepart, name: *const c_char) -> CURLcode;
    pub fn curl_mime_data(part: *mut curl_mimepart, data: *const c_char, datasize: isize) -> CURLcode;
    pub fn curl_mime_filedata(part: *mut curl_mimepart, filename: *const c_char) -> CURLcode;
    pub fn curl_mime_filename(part: *mut curl_mimepart, filename: *const c_char) -> CURLcode;
    pub fn curl_mime_type(part: *mut curl_mimepart, mimetype: *const c_char) -> CURLcode;
    pub fn curl_mime_encoder(part: *mut curl_mimepart, encoding: *const c_char) -> CURLcode;
    pub fn curl_mime_headers(part: *mut curl_mimepart, headers: *mut curl_slist, take_ownership: c_int) -> CURLcode;
    pub fn curl_mime_subparts(part: *mut curl_mimepart, subparts: *mut curl_mime) -> CURLcode;

    pub fn curl_url() -> *mut CURLU;
    pub fn curl_url_cleanup(handle: *mut CURLU);
    pub fn curl_url_dup(in_: *const CURLU) -> *mut CURLU;
    pub fn curl_url_get(handle: *const CURLU, what: CURLUPart, part: *mut *mut c_char, flags: c_uint) -> CURLUcode;
    pub fn curl_url_set(handle: *mut CURLU, what: CURLUPart, part: *const c_char, flags: c_uint) -> CURLUcode;
    pub fn curl_url_strerror(error: CURLUcode) -> *const c_char;

    pub fn curl_easy_header(
        handle: *mut CURL,
        name: *const c_char,
        index: size_t,
        origin: c_uint,
        request: c_int,
        hout: *mut *mut curl_header,
    ) -> CURLHcode;
    pub fn curl_easy_nextheader(
        handle: *mut CURL,
        origin: c_uint,
        request: c_int,
        prev: *mut curl_header,
    ) -> *mut curl_header;

    pub fn curl_ws_recv(
        handle: *mut CURL,
        buffer: *mut c_void,
        buflen: size_t,
        recv: *mut size_t,
        meta: *mut *const curl_ws_frame,
    ) -> CURLcode;
    pub fn curl_ws_send(
        handle: *mut CURL,
        buffer: *const c_void,
        buflen: size_t,
        sent: *mut size_t,
        fragsize: curl_off_t,
        flags: c_uint,
    ) -> CURLcode;
    pub fn curl_ws_meta(handle: *mut CURL) -> *const curl_ws_frame;
}

type curl_easy_setopt_long_t = unsafe extern "C" fn(*mut CURL, CURLoption, c_long) -> CURLcode;
type curl_easy_setopt_offt_t = unsafe extern "C" fn(*mut CURL, CURLoption, curl_off_t) -> CURLcode;
type curl_easy_setopt_ptr_t = unsafe extern "C" fn(*mut CURL, CURLoption, *mut c_void) -> CURLcode;
type curl_easy_getinfo_long_t = unsafe extern "C" fn(*mut CURL, CURLINFO, *mut c_long) -> CURLcode;
type curl_easy_getinfo_double_t = unsafe extern "C" fn(*mut CURL, CURLINFO, *mut c_double) -> CURLcode;
type curl_easy_getinfo_ptr_t = unsafe extern "C" fn(*mut CURL, CURLINFO, *mut c_void) -> CURLcode;
type curl_easy_getinfo_offt_t = unsafe extern "C" fn(*mut CURL, CURLINFO, *mut curl_off_t) -> CURLcode;
type curl_multi_setopt_long_t = unsafe extern "C" fn(*mut CURLM, CURLMoption, c_long) -> CURLMcode;
type curl_multi_setopt_offt_t = unsafe extern "C" fn(*mut CURLM, CURLMoption, curl_off_t) -> CURLMcode;
type curl_multi_setopt_ptr_t = unsafe extern "C" fn(*mut CURLM, CURLMoption, *mut c_void) -> CURLMcode;
type curl_share_setopt_int_t = unsafe extern "C" fn(*mut CURLSH, CURLSHoption, c_int) -> CURLSHcode;
type curl_share_setopt_ptr_t = unsafe extern "C" fn(*mut CURLSH, CURLSHoption, *mut c_void) -> CURLSHcode;

#[inline(always)]
unsafe fn curl_easy_setopt_long(handle: *mut CURL, option: CURLoption, value: c_long) -> CURLcode {
    mem::transmute::<unsafe extern "C" fn(*mut CURL, CURLoption, ...) -> CURLcode, curl_easy_setopt_long_t>(curl_easy_setopt)(handle, option, value)
}

#[inline(always)]
unsafe fn curl_easy_setopt_offt(handle: *mut CURL, option: CURLoption, value: curl_off_t) -> CURLcode {
    mem::transmute::<unsafe extern "C" fn(*mut CURL, CURLoption, ...) -> CURLcode, curl_easy_setopt_offt_t>(curl_easy_setopt)(handle, option, value)
}

#[inline(always)]
unsafe fn curl_easy_setopt_ptr(handle: *mut CURL, option: CURLoption, value: *mut c_void) -> CURLcode {
    mem::transmute::<unsafe extern "C" fn(*mut CURL, CURLoption, ...) -> CURLcode, curl_easy_setopt_ptr_t>(curl_easy_setopt)(handle, option, value)
}

#[inline(always)]
unsafe fn curl_easy_getinfo_long(handle: *mut CURL, info: CURLINFO, value: *mut c_long) -> CURLcode {
    mem::transmute::<unsafe extern "C" fn(*mut CURL, CURLINFO, ...) -> CURLcode, curl_easy_getinfo_long_t>(curl_easy_getinfo)(handle, info, value)
}

#[inline(always)]
unsafe fn curl_easy_getinfo_double(handle: *mut CURL, info: CURLINFO, value: *mut c_double) -> CURLcode {
    mem::transmute::<unsafe extern "C" fn(*mut CURL, CURLINFO, ...) -> CURLcode, curl_easy_getinfo_double_t>(curl_easy_getinfo)(handle, info, value)
}

#[inline(always)]
unsafe fn curl_easy_getinfo_ptr(handle: *mut CURL, info: CURLINFO, value: *mut c_void) -> CURLcode {
    mem::transmute::<unsafe extern "C" fn(*mut CURL, CURLINFO, ...) -> CURLcode, curl_easy_getinfo_ptr_t>(curl_easy_getinfo)(handle, info, value)
}

#[inline(always)]
unsafe fn curl_easy_getinfo_offt(handle: *mut CURL, info: CURLINFO, value: *mut curl_off_t) -> CURLcode {
    mem::transmute::<unsafe extern "C" fn(*mut CURL, CURLINFO, ...) -> CURLcode, curl_easy_getinfo_offt_t>(curl_easy_getinfo)(handle, info, value)
}

#[inline(always)]
unsafe fn curl_multi_setopt_long(handle: *mut CURLM, option: CURLMoption, value: c_long) -> CURLMcode {
    mem::transmute::<unsafe extern "C" fn(*mut CURLM, CURLMoption, ...) -> CURLMcode, curl_multi_setopt_long_t>(curl_multi_setopt)(handle, option, value)
}

#[inline(always)]
unsafe fn curl_multi_setopt_offt(handle: *mut CURLM, option: CURLMoption, value: curl_off_t) -> CURLMcode {
    mem::transmute::<unsafe extern "C" fn(*mut CURLM, CURLMoption, ...) -> CURLMcode, curl_multi_setopt_offt_t>(curl_multi_setopt)(handle, option, value)
}

#[inline(always)]
unsafe fn curl_multi_setopt_ptr(handle: *mut CURLM, option: CURLMoption, value: *mut c_void) -> CURLMcode {
    mem::transmute::<unsafe extern "C" fn(*mut CURLM, CURLMoption, ...) -> CURLMcode, curl_multi_setopt_ptr_t>(curl_multi_setopt)(handle, option, value)
}

#[inline(always)]
unsafe fn curl_share_setopt_int(sh: *mut CURLSH, opt: CURLSHoption, value: c_int) -> CURLSHcode {
    mem::transmute::<unsafe extern "C" fn(*mut CURLSH, CURLSHoption, ...) -> CURLSHcode, curl_share_setopt_int_t>(curl_share_setopt)(sh, opt, value)
}

#[inline(always)]
unsafe fn curl_share_setopt_ptr(sh: *mut CURLSH, opt: CURLSHoption, value: *mut c_void) -> CURLSHcode {
    mem::transmute::<unsafe extern "C" fn(*mut CURLSH, CURLSHoption, ...) -> CURLSHcode, curl_share_setopt_ptr_t>(curl_share_setopt)(sh, opt, value)
}

unsafe fn fd_zero(set: *mut fd_set) {
    #[cfg(windows)]
    {
        (*set).fd_count = 0;
    }
    #[cfg(not(windows))]
    {
        ptr::write_bytes(set, 0, 1);
    }
}

unsafe fn fd_copy(dst: *mut fd_set, src: *const fd_set) {
    ptr::copy_nonoverlapping(src, dst, 1);
}

#[cfg(windows)]
#[repr(C)]
struct CRITICAL_SECTION {
    _opaque: [u8; 40],
}

#[cfg(windows)]
extern "system" {
    fn InitializeCriticalSection(lpCriticalSection: *mut CRITICAL_SECTION);
    fn EnterCriticalSection(lpCriticalSection: *mut CRITICAL_SECTION);
    fn LeaveCriticalSection(lpCriticalSection: *mut CRITICAL_SECTION);
    fn DeleteCriticalSection(lpCriticalSection: *mut CRITICAL_SECTION);
}

#[cfg(not(windows))]
type pthread_mutex_t = [u8; 40];

#[cfg(not(windows))]
const PTHREAD_MUTEX_INITIALIZER: pthread_mutex_t = [0; 40];

#[cfg(not(windows))]
extern "C" {
    fn pthread_mutex_init(mutex: *mut pthread_mutex_t, attr: *const c_void) -> c_int;
    fn pthread_mutex_lock(mutex: *mut pthread_mutex_t) -> c_int;
    fn pthread_mutex_unlock(mutex: *mut pthread_mutex_t) -> c_int;
    fn pthread_mutex_destroy(mutex: *mut pthread_mutex_t) -> c_int;
}

struct RawLock {
    #[cfg(windows)]
    cs: CRITICAL_SECTION,
    #[cfg(not(windows))]
    mtx: pthread_mutex_t,
}

impl RawLock {
    fn new() -> Self {
        #[cfg(windows)]
        unsafe {
            let mut cs: CRITICAL_SECTION = mem::zeroed();
            InitializeCriticalSection(&mut cs);
            Self { cs }
        }
        #[cfg(not(windows))]
        unsafe {
            let mut mtx: pthread_mutex_t = PTHREAD_MUTEX_INITIALIZER;
            pthread_mutex_init(&mut mtx, ptr::null());
            Self { mtx }
        }
    }

    fn lock(&self) {
        #[cfg(windows)]
        unsafe {
            EnterCriticalSection(&self.cs as *const _ as *mut _);
        }
        #[cfg(not(windows))]
        unsafe {
            pthread_mutex_lock(&self.mtx as *const _ as *mut _);
        }
    }

    fn unlock(&self) {
        #[cfg(windows)]
        unsafe {
            LeaveCriticalSection(&self.cs as *const _ as *mut _);
        }
        #[cfg(not(windows))]
        unsafe {
            pthread_mutex_unlock(&self.mtx as *const _ as *mut _);
        }
    }
}

impl Drop for RawLock {
    fn drop(&mut self) {
        #[cfg(windows)]
        unsafe {
            DeleteCriticalSection(&mut self.cs);
        }
        #[cfg(not(windows))]
        unsafe {
            pthread_mutex_destroy(&mut self.mtx);
        }
    }
}

unsafe impl Send for RawLock {}
unsafe impl Sync for RawLock {}

struct ShareLockSet {
    locks: [RawLock; CURL_LOCK_DATA_LAST as usize],
}

impl ShareLockSet {
    fn new() -> Box<Self> {
        let locks: [RawLock; CURL_LOCK_DATA_LAST as usize] = [
            RawLock::new(),
            RawLock::new(),
            RawLock::new(),
            RawLock::new(),
            RawLock::new(),
            RawLock::new(),
            RawLock::new(),
        ];
        Box::new(Self { locks })
    }
}

struct FdSetPoolInner {
    chunk_size: usize,
    blocks: Vec<Box<[fd_set]>>,
    free_list: Vec<*mut fd_set>,
}

struct FdSetPool {
    inner: Mutex<FdSetPoolInner>,
}

impl FdSetPool {
    fn new(chunk: usize) -> Self {
        let chunk_size = if chunk == 0 { 32 } else { chunk };
        Self {
            inner: Mutex::new(FdSetPoolInner {
                chunk_size,
                blocks: Vec::new(),
                free_list: Vec::new(),
            }),
        }
    }

    fn allocate(&self) -> *mut fd_set {
        let mut inner = self.inner.lock().unwrap();
        if inner.free_list.is_empty() {
            let count = inner.chunk_size;
            let mut sets = Vec::with_capacity(count);
            for _ in 0..count {
                sets.push(unsafe { mem::zeroed::<fd_set>() });
            }
            let sets = sets.into_boxed_slice();
            let base = sets.as_ptr() as *mut fd_set;
            for i in 0..count {
                inner.free_list.push(unsafe { base.add(i) });
            }
            inner.blocks.push(sets);
        }
        inner.free_list.pop().unwrap()
    }

    fn deallocate(&self, p: *mut fd_set) {
        if p.is_null() {
            return;
        }
        let mut inner = self.inner.lock().unwrap();
        inner.free_list.push(p);
    }
}

unsafe impl Send for FdSetPool {}
unsafe impl Sync for FdSetPool {}

static GLOBAL_MTX: OnceLock<Mutex<()>> = OnceLock::new();
static G_INIT_COUNT: OnceLock<Mutex<usize>> = OnceLock::new();
static G_FD_POOL: AtomicPtr<FdSetPool> = AtomicPtr::new(ptr::null_mut());
static G_SHARE_LOCKS_MTX: OnceLock<Mutex<()>> = OnceLock::new();
static G_SHARE_LOCKS: OnceLock<Mutex<HashMap<usize, Box<ShareLockSet>>>> = OnceLock::new();

fn global_mtx() -> &'static Mutex<()> {
    GLOBAL_MTX.get_or_init(|| Mutex::new(()))
}

fn init_count() -> &'static Mutex<usize> {
    G_INIT_COUNT.get_or_init(|| Mutex::new(0))
}

fn share_locks_mtx() -> &'static Mutex<()> {
    G_SHARE_LOCKS_MTX.get_or_init(|| Mutex::new(()))
}

fn share_locks() -> &'static Mutex<HashMap<usize, Box<ShareLockSet>>> {
    G_SHARE_LOCKS.get_or_init(|| Mutex::new(HashMap::new()))
}

type SocketManagedCb = unsafe extern "C" fn(sockfd: intptr_t, userptr: *mut c_void) -> c_int;

static G_OPEN_CB: AtomicPtr<c_void> = AtomicPtr::new(ptr::null_mut());
static G_CLOSE_CB: AtomicPtr<c_void> = AtomicPtr::new(ptr::null_mut());

unsafe extern "C" fn open_socket_trampoline(
    clientp: *mut c_void,
    _purpose: curlsocktype,
    address: *mut curl_sockaddr,
) -> curl_socket_t {
    let cb_ptr = G_OPEN_CB.load(Ordering::Acquire);
    if cb_ptr.is_null() {
        return CURL_SOCKET_BAD;
    }
    let cb: SocketManagedCb = mem::transmute(cb_ptr);
    if cb(-1 as intptr_t, clientp) == 0 {
        return CURL_SOCKET_BAD;
    }
    let family = (*address).family;
    let socktype = (*address).socktype;
    let protocol = (*address).protocol;
    #[cfg(windows)]
    let fd = socket(family, socktype, protocol);
    #[cfg(not(windows))]
    let fd = socket(family, socktype, protocol) as curl_socket_t;
    if fd != CURL_SOCKET_BAD {
        cb(fd as intptr_t, clientp);
    }
    fd
}

unsafe extern "C" fn close_socket_trampoline(
    clientp: *mut c_void,
    item: curl_socket_t,
) -> c_int {
    let cb_ptr = G_CLOSE_CB.load(Ordering::Acquire);
    if !cb_ptr.is_null() {
        let cb: SocketManagedCb = mem::transmute(cb_ptr);
        if cb(item as intptr_t, clientp) != 0 {
            return 0;
        }
    }
    if item == CURL_SOCKET_BAD {
        return 0;
    }
    #[cfg(windows)]
    {
        if closesocket(item) == 0 { 0 } else { 1 }
    }
    #[cfg(not(windows))]
    {
        if close(item as c_int) == 0 { 0 } else { 1 }
    }
}

unsafe extern "C" fn share_lock(
    _handle: *mut CURL,
    data: curl_lock_data,
    _access: curl_lock_access,
    userptr: *mut c_void,
) {
    if userptr.is_null() {
        return;
    }
    if data < 0 || data >= CURL_LOCK_DATA_LAST {
        return;
    }
    let lock_set = &*(userptr as *const ShareLockSet);
    lock_set.locks[data as usize].lock();
}

unsafe extern "C" fn share_unlock(
    _handle: *mut CURL,
    data: curl_lock_data,
    userptr: *mut c_void,
) {
    if userptr.is_null() {
        return;
    }
    if data < 0 || data >= CURL_LOCK_DATA_LAST {
        return;
    }
    let lock_set = &*(userptr as *const ShareLockSet);
    lock_set.locks[data as usize].unlock();
}

fn is_long_option(opt: CURLoption) -> bool {
    let v = opt as c_uint;
    v > CURLOPTTYPE_LONG as c_uint && v < CURLOPTTYPE_OBJECTPOINT as c_uint
}

fn is_object_option(opt: CURLoption) -> bool {
    let v = opt as c_uint;
    v > CURLOPTTYPE_OBJECTPOINT as c_uint && v < CURLOPTTYPE_FUNCTIONPOINT as c_uint
}

fn is_pointer_option(opt: CURLoption) -> bool {
    let v = opt as c_uint;
    v > CURLOPTTYPE_OBJECTPOINT as c_uint && v < CURLOPTTYPE_OFF_T as c_uint
}

fn is_offt_option(opt: CURLoption) -> bool {
    let v = opt as c_uint;
    v > CURLOPTTYPE_OFF_T as c_uint && v < CURLOPTTYPE_BLOB as c_uint
}

fn is_blob_option(opt: CURLoption) -> bool {
    (opt as c_uint) > CURLOPTTYPE_BLOB as c_uint
}

fn allows_unsigned_long(opt: CURLoption) -> bool {
    matches!(
        opt,
        CURLOPT_HTTPAUTH
            | CURLOPT_PROXYAUTH
            | CURLOPT_SOCKS5_AUTH
            | CURLOPT_SSH_AUTH_TYPES
            | CURLOPT_PROTOCOLS
            | CURLOPT_REDIR_PROTOCOLS
    )
}

fn multi_allows_unsigned_long(opt: CURLMoption) -> bool {
    opt == CURLMOPT_MAXCONNECTS
}

fn try_native_long(opt: CURLoption, value: i64) -> Option<c_long> {
    if mem::size_of::<c_long>() == mem::size_of::<i64>() {
        return Some(value as c_long);
    }
    let min = c_long::MIN as i64;
    let max = c_long::MAX as i64;
    if value < min {
        return None;
    }
    if value > max {
        if !allows_unsigned_long(opt) {
            return None;
        }
        let umax = c_long::MAX as u64;
        if (value as u64) > (umax * 2 + 1) {
            return None;
        }
    }
    Some(value as c_long)
}

fn multi_try_native_long(opt: CURLMoption, value: i64) -> Option<c_long> {
    if mem::size_of::<c_long>() == mem::size_of::<i64>() {
        return Some(value as c_long);
    }
    let min = c_long::MIN as i64;
    let max = c_long::MAX as i64;
    if value < min {
        return None;
    }
    if value > max {
        if !multi_allows_unsigned_long(opt) {
            return None;
        }
        let umax = c_long::MAX as u64;
        if (value as u64) > (umax * 2 + 1) {
            return None;
        }
    }
    Some(value as c_long)
}

fn info_type(info: CURLINFO) -> c_int {
    (info as c_uint & CURLINFO_TYPEMASK as c_uint) as c_int
}

#[cfg(windows)]
const SOCKET_TIMED_OUT: c_int = WSAETIMEDOUT;
#[cfg(not(windows))]
const SOCKET_TIMED_OUT: c_int = ETIMEDOUT;

#[inline]
unsafe fn set_socket_timeout_errno() {
    #[cfg(windows)]
    {
        WSASetLastError(SOCKET_TIMED_OUT);
    }
    #[cfg(not(windows))]
    {
        *errno_ptr() = SOCKET_TIMED_OUT;
    }
}

#[inline]
unsafe fn get_socket_errno() -> c_int {
    #[cfg(windows)]
    {
        WSAGetLastError()
    }
    #[cfg(not(windows))]
    {
        *errno_ptr()
    }
}

#[inline]
fn get_eintr_errno() -> c_int {
    #[cfg(windows)]
    {
        WSAEINTR
    }
    #[cfg(not(windows))]
    {
        EINTR
    }
}

#[no_mangle]
pub unsafe extern "C" fn nativebridge_version() -> *const c_char {
    b"1.0.0-rust\0".as_ptr() as *const c_char
}

#[no_mangle]
pub unsafe extern "C" fn curlw_abi_version() -> c_int {
    1
}

#[no_mangle]
pub unsafe extern "C" fn curlw_version_imp() -> *const c_char {
    curl_version()
}

#[no_mangle]
pub unsafe extern "C" fn curlw_create_socket(af: c_int, socktype: c_int, protocol: c_int) -> intptr_t {
    #[cfg(windows)]
    {
        socket(af, socktype, protocol) as intptr_t
    }
    #[cfg(not(windows))]
    {
        socket(af, socktype, protocol) as intptr_t
    }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_shutdown_socket(sockfd: intptr_t) -> c_int {
    #[cfg(windows)]
    {
        shutdown(sockfd as curl_socket_t, SD_BOTH)
    }
    #[cfg(not(windows))]
    {
        shutdown(sockfd as c_int, SHUT_RDWR)
    }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_close_socket(sockfd: intptr_t) -> c_int {
    #[cfg(windows)]
    {
        closesocket(sockfd as curl_socket_t)
    }
    #[cfg(not(windows))]
    {
        close(sockfd as c_int)
    }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_errno() -> c_int {
    get_socket_errno()
}

#[no_mangle]
pub unsafe extern "C" fn curlw_global_init(flags: c_int, max_fd_set: c_uint) -> CURLcode {
    let _lk = global_mtx().lock().unwrap();

    let ec = curl_global_init(flags as c_long);
    if ec != CURLE_OK {
        return ec;
    }

    let mut count = init_count().lock().unwrap();
    if *count == 0 {
        let pool = Box::new(FdSetPool::new(max_fd_set as usize));
        let raw = Box::into_raw(pool);
        G_FD_POOL.store(raw, Ordering::Release);
    }
    *count += 1;

    CURLE_OK
}

#[no_mangle]
pub unsafe extern "C" fn curlw_global_cleanup() {
    let _lk = global_mtx().lock().unwrap();

    let mut count = init_count().lock().unwrap();
    if *count == 0 {
        return;
    }

    curl_global_cleanup();
    *count -= 1;

    if *count == 0 {
        let pool_ptr = G_FD_POOL.load(Ordering::Acquire);
        if !pool_ptr.is_null() {
            let _ = Box::from_raw(pool_ptr);
            G_FD_POOL.store(ptr::null_mut(), Ordering::Release);
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_socket_allocfds() -> *mut fd_set {
    let pool = G_FD_POOL.load(Ordering::Acquire);
    if pool.is_null() {
        return ptr::null_mut();
    }
    let p = (*pool).allocate();
    ptr::write_bytes(p, 0, 1);
    p
}

#[no_mangle]
pub unsafe extern "C" fn curlw_socket_freefds(pfds: *mut fd_set) {
    if pfds.is_null() {
        return;
    }
    let pool = G_FD_POOL.load(Ordering::Acquire);
    if !pool.is_null() {
        (*pool).deallocate(pfds);
    }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_socket_zerofds(pfds: *mut fd_set) {
    if pfds.is_null() {
        return;
    }
    fd_zero(pfds);
}

#[no_mangle]
pub unsafe extern "C" fn curlw_socket_select(
    nfds: c_int,
    readfds: *mut fd_set,
    writefds: *mut fd_set,
    exceptfds: *mut fd_set,
    microseconds: u64,
) -> c_int {
    let mut read_input: fd_set = mem::zeroed();
    let mut write_input: fd_set = mem::zeroed();
    let mut except_input: fd_set = mem::zeroed();
    if !readfds.is_null() {
        fd_copy(&mut read_input, readfds);
    }
    if !writefds.is_null() {
        fd_copy(&mut write_input, writefds);
    }
    if !exceptfds.is_null() {
        fd_copy(&mut except_input, exceptfds);
    }

    let mut remaining_us = microseconds;

    loop {
        let mut tv = timeval {
            tv_sec: (remaining_us / 1_000_000) as c_long,
            tv_usec: (remaining_us % 1_000_000) as c_long,
        };
        let tv_ptr = &mut tv as *mut _;

        let start = Instant::now();
        let n = select(
            nfds,
            readfds,
            writefds,
            exceptfds,
            tv_ptr,
        );

        if n < 0 && get_socket_errno() == get_eintr_errno() {
            let elapsed = start.elapsed().as_micros() as u64;
            if elapsed < remaining_us {
                remaining_us -= elapsed;
                if !readfds.is_null() {
                    fd_copy(readfds, &read_input);
                }
                if !writefds.is_null() {
                    fd_copy(writefds, &write_input);
                }
                if !exceptfds.is_null() {
                    fd_copy(exceptfds, &except_input);
                }
                continue;
            }
            set_socket_timeout_errno();
            return 0;
        }

        if n == 0 {
            set_socket_timeout_errno();
        }
        return n;
    }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_init() -> *mut CURL {
    curl_easy_init()
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_perform(curl: *mut CURL) -> CURLcode {
    curl_easy_perform(curl)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_cleanup(curl: *mut CURL) {
    curl_easy_cleanup(curl)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_reset(curl: *mut CURL) {
    curl_easy_reset(curl)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_strerror_imp(error: CURLcode) -> *const c_char {
    curl_easy_strerror(error)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_setopt_int(
    handle: *mut CURL,
    option: CURLoption,
    optval: c_int,
) -> CURLcode {
    if !is_long_option(option) {
        return CURLE_BAD_FUNCTION_ARGUMENT;
    }
    curl_easy_setopt_long(handle, option, optval as c_long)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_setopt_long(
    handle: *mut CURL,
    option: CURLoption,
    optval: i64,
) -> CURLcode {
    if !is_long_option(option) {
        return CURLE_BAD_FUNCTION_ARGUMENT;
    }
    let Some(native) = try_native_long(option, optval) else {
        return CURLE_BAD_FUNCTION_ARGUMENT;
    };
    curl_easy_setopt_long(handle, option, native)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_setopt_offt(
    handle: *mut CURL,
    option: CURLoption,
    optval: i64,
) -> CURLcode {
    if !is_offt_option(option) {
        return CURLE_BAD_FUNCTION_ARGUMENT;
    }
    curl_easy_setopt_offt(handle, option, optval as curl_off_t)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_setopt_pointer(
    handle: *mut CURL,
    option: CURLoption,
    optval: *mut c_void,
) -> CURLcode {
    if !is_pointer_option(option) {
        return CURLE_BAD_FUNCTION_ARGUMENT;
    }
    curl_easy_setopt_ptr(handle, option, optval)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_setopt_string(
    handle: *mut CURL,
    option: CURLoption,
    optval: *const c_char,
) -> CURLcode {
    if !is_object_option(option) {
        return CURLE_BAD_FUNCTION_ARGUMENT;
    }
    curl_easy_setopt_ptr(handle, option, optval as *mut c_void)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_setopt_blob(
    handle: *mut CURL,
    option: CURLoption,
    data: *mut c_void,
    len: size_t,
    flags: c_uint,
) -> CURLcode {
    if !is_blob_option(option) {
        return CURLE_BAD_FUNCTION_ARGUMENT;
    }
    let blob = curl_blob { data, len, flags };
    curl_easy_setopt_ptr(handle, option, &blob as *const _ as *mut c_void)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_getinfo_int(
    handle: *mut CURL,
    info: CURLINFO,
    outval: *mut c_int,
) -> CURLcode {
    if outval.is_null() || info_type(info) != CURLINFO_LONG {
        return CURLE_BAD_FUNCTION_ARGUMENT;
    }
    let mut tmp: c_long = 0;
    let ec = curl_easy_getinfo_long(handle, info, &mut tmp);
    if ec == CURLE_OK {
        if tmp < c_int::MIN as c_long || tmp > c_int::MAX as c_long {
            return CURLE_BAD_FUNCTION_ARGUMENT;
        }
        *outval = tmp as c_int;
    }
    ec
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_getinfo_long(
    handle: *mut CURL,
    info: CURLINFO,
    outval: *mut i64,
) -> CURLcode {
    if outval.is_null() {
        return CURLE_BAD_FUNCTION_ARGUMENT;
    }
    let t = info_type(info);
    if t == CURLINFO_OFF_T {
        let mut tmp: curl_off_t = 0;
        let ec = curl_easy_getinfo_offt(handle, info, &mut tmp);
        if ec == CURLE_OK {
            *outval = tmp as i64;
        }
        return ec;
    }
    if t != CURLINFO_LONG {
        return CURLE_BAD_FUNCTION_ARGUMENT;
    }
    let mut tmp: c_long = 0;
    let ec = curl_easy_getinfo_long(handle, info, &mut tmp);
    if ec == CURLE_OK {
        *outval = tmp as i64;
    }
    ec
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_getinfo_double(
    handle: *mut CURL,
    info: CURLINFO,
    outval: *mut c_double,
) -> CURLcode {
    if outval.is_null() || info_type(info) != CURLINFO_DOUBLE {
        return CURLE_BAD_FUNCTION_ARGUMENT;
    }
    curl_easy_getinfo_double(handle, info, outval)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_getinfo_pointer(
    handle: *mut CURL,
    info: CURLINFO,
    outval: *mut *mut c_void,
) -> CURLcode {
    if outval.is_null() {
        return CURLE_BAD_FUNCTION_ARGUMENT;
    }
    let t = info_type(info);
    if t == CURLINFO_STRING {
        let mut tmp: *mut c_char = ptr::null_mut();
        let ec = curl_easy_getinfo_ptr(handle, info, &mut tmp as *mut *mut c_char as *mut c_void);
        if ec == CURLE_OK {
            *outval = tmp as *mut c_void;
        }
        return ec;
    }
    if t == CURLINFO_SLIST {
        let mut tmp: *mut curl_slist = ptr::null_mut();
        let ec = curl_easy_getinfo_ptr(handle, info, &mut tmp as *mut *mut curl_slist as *mut c_void);
        if ec == CURLE_OK {
            *outval = tmp as *mut c_void;
        }
        return ec;
    }
    if t == CURLINFO_SOCKET {
        let mut tmp: curl_socket_t = CURL_SOCKET_BAD;
        let ec = curl_easy_getinfo_long(handle, info, &mut tmp as *mut curl_socket_t as *mut c_long);
        if ec == CURLE_OK {
            *outval = tmp as usize as *mut c_void;
        }
        return ec;
    }
    CURLE_BAD_FUNCTION_ARGUMENT
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_set_opensocket_global_cb(cb: SocketManagedCb) {
    G_OPEN_CB.store(cb as *mut c_void, Ordering::Release);
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_set_opensocket_cb(
    handle: *mut CURL,
    userdata: *mut c_void,
) -> CURLcode {
    if G_OPEN_CB.load(Ordering::Acquire).is_null() {
        return CURLE_FAILED_INIT;
    }
    let mut res = curl_easy_setopt_ptr(handle, CURLOPT_OPENSOCKETDATA, userdata);
    if res == CURLE_OK {
        res = curl_easy_setopt_ptr(
            handle,
            CURLOPT_OPENSOCKETFUNCTION,
            open_socket_trampoline as *mut c_void,
        );
    }
    res
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_clear_opensocket_cb(handle: *mut CURL) {
    curl_easy_setopt_ptr(handle, CURLOPT_OPENSOCKETFUNCTION, ptr::null_mut());
    curl_easy_setopt_ptr(handle, CURLOPT_OPENSOCKETDATA, ptr::null_mut());
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_set_closesocket_global_cb(cb: SocketManagedCb) {
    G_CLOSE_CB.store(cb as *mut c_void, Ordering::Release);
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_set_closesocket_cb(
    handle: *mut CURL,
    userdata: *mut c_void,
) -> CURLcode {
    if G_CLOSE_CB.load(Ordering::Acquire).is_null() {
        return CURLE_FAILED_INIT;
    }
    let mut res = curl_easy_setopt_ptr(handle, CURLOPT_CLOSESOCKETDATA, userdata);
    if res == CURLE_OK {
        res = curl_easy_setopt_ptr(
            handle,
            CURLOPT_CLOSESOCKETFUNCTION,
            close_socket_trampoline as *mut c_void,
        );
    }
    res
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_clear_closesocket_cb(handle: *mut CURL) {
    curl_easy_setopt_ptr(handle, CURLOPT_CLOSESOCKETFUNCTION, ptr::null_mut());
    curl_easy_setopt_ptr(handle, CURLOPT_CLOSESOCKETDATA, ptr::null_mut());
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_init() -> *mut CURLM {
    curl_multi_init()
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_cleanup(multi_handle: *mut CURLM) -> CURLMcode {
    curl_multi_cleanup(multi_handle)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_add_handle(
    multi_handle: *mut CURLM,
    curl_handle: *mut CURL,
) -> CURLMcode {
    curl_multi_add_handle(multi_handle, curl_handle)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_remove_handle(
    multi_handle: *mut CURLM,
    curl_handle: *mut CURL,
) -> CURLMcode {
    curl_multi_remove_handle(multi_handle, curl_handle)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_perform(
    multi_handle: *mut CURLM,
    running_handles: *mut c_int,
) -> CURLMcode {
    curl_multi_perform(multi_handle, running_handles)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_fdset(
    multi_handle: *mut CURLM,
    read_fds: *mut fd_set,
    write_fds: *mut fd_set,
    exc_fds: *mut fd_set,
    max_fd: *mut c_int,
) -> CURLMcode {
    curl_multi_fdset(
        multi_handle,
        read_fds as *mut _,
        write_fds as *mut _,
        exc_fds as *mut _,
        max_fd,
    )
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_timeout(
    multi_handle: *mut CURLM,
    milliseconds: *mut i64,
) -> CURLMcode {
    let mut ms: c_long = 0;
    let ec = curl_multi_timeout(multi_handle, &mut ms);
    if !milliseconds.is_null() {
        *milliseconds = ms as i64;
    }
    ec
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_poll(
    multi_handle: *mut CURLM,
    extra_fds: *mut curl_waitfd,
    extra_nfds: c_uint,
    timeout_ms: c_int,
    numfds: *mut c_int,
) -> CURLMcode {
    curl_multi_poll(multi_handle, extra_fds, extra_nfds, timeout_ms, numfds)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_wait(
    multi_handle: *mut CURLM,
    extra_fds: *mut curl_waitfd,
    extra_nfds: c_uint,
    timeout_ms: c_int,
    numfds: *mut c_int,
) -> CURLMcode {
    curl_multi_wait(multi_handle, extra_fds, extra_nfds, timeout_ms, numfds)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_wakeup(multi_handle: *mut CURLM) -> CURLMcode {
    curl_multi_wakeup(multi_handle)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_strerror_imp(error: CURLMcode) -> *const c_char {
    curl_multi_strerror(error)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_setopt_int(
    multi_handle: *mut CURLM,
    option: CURLMoption,
    optval: c_int,
) -> CURLMcode {
    if !is_long_option(option as CURLoption) {
        return CURLM_BAD_FUNCTION_ARGUMENT;
    }
    curl_multi_setopt_long(multi_handle, option, optval as c_long)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_setopt_long(
    multi_handle: *mut CURLM,
    option: CURLMoption,
    optval: i64,
) -> CURLMcode {
    if !is_long_option(option as CURLoption) {
        return CURLM_BAD_FUNCTION_ARGUMENT;
    }
    let Some(native) = multi_try_native_long(option, optval) else {
        return CURLM_BAD_FUNCTION_ARGUMENT;
    };
    curl_multi_setopt_long(multi_handle, option, native)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_setopt_offt(
    multi_handle: *mut CURLM,
    option: CURLMoption,
    optval: i64,
) -> CURLMcode {
    if !is_offt_option(option as CURLoption) {
        return CURLM_BAD_FUNCTION_ARGUMENT;
    }
    curl_multi_setopt_offt(multi_handle, option, optval as curl_off_t)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_setopt_pointer(
    multi_handle: *mut CURLM,
    option: CURLMoption,
    optval: *mut c_void,
) -> CURLMcode {
    if !is_pointer_option(option as CURLoption) {
        return CURLM_BAD_FUNCTION_ARGUMENT;
    }
    curl_multi_setopt_ptr(multi_handle, option, optval)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_setopt_string(
    multi_handle: *mut CURLM,
    option: CURLMoption,
    optval: *const c_char,
) -> CURLMcode {
    if !is_object_option(option as CURLoption) {
        return CURLM_BAD_FUNCTION_ARGUMENT;
    }
    curl_multi_setopt_ptr(multi_handle, option, optval as *mut c_void)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_info_read(
    multi_handle: *mut CURLM,
    msgs_in_queue: *mut c_int,
) -> *mut CURLMsg {
    curl_multi_info_read(multi_handle, msgs_in_queue)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_msg_get_msg(msg: *const CURLMsg) -> c_int {
    if msg.is_null() {
        return 0;
    }
    (*msg).msg as c_int
}

#[no_mangle]
pub unsafe extern "C" fn curlw_msg_get_easy_handle(msg: *const CURLMsg) -> *mut CURL {
    if msg.is_null() {
        return ptr::null_mut();
    }
    (*msg).easy_handle
}

#[no_mangle]
pub unsafe extern "C" fn curlw_msg_get_result(msg: *const CURLMsg) -> CURLcode {
    if msg.is_null() {
        return CURLE_OK;
    }
    (*msg).result_code()
}

#[no_mangle]
pub unsafe extern "C" fn curlw_slist_append(
    list: *mut curl_slist,
    value: *const c_char,
) -> *mut curl_slist {
    curl_slist_append(list, value)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_slist_free_all(list: *mut curl_slist) {
    curl_slist_free_all(list)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_free(p: *mut c_void) {
    curl_free(p)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_duphandle(handle: *mut CURL) -> *mut CURL {
    curl_easy_duphandle(handle)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_pause(handle: *mut CURL, action: c_int) -> CURLcode {
    curl_easy_pause(handle, action)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_upkeep(handle: *mut CURL) -> CURLcode {
    curl_easy_upkeep(handle)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_recv(
    curl: *mut CURL,
    buffer: *mut c_void,
    buflen: size_t,
    n: *mut size_t,
) -> CURLcode {
    curl_easy_recv(curl, buffer, buflen, n)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_send(
    curl: *mut CURL,
    buffer: *const c_void,
    buflen: size_t,
    n: *mut size_t,
) -> CURLcode {
    curl_easy_send(curl, buffer, buflen, n)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_escape(
    handle: *mut CURL,
    string: *const c_char,
    length: c_int,
) -> *mut c_char {
    curl_easy_escape(handle, string, length)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_unescape(
    handle: *mut CURL,
    string: *const c_char,
    inlength: c_int,
    outlength: *mut c_int,
) -> *mut c_char {
    curl_easy_unescape(handle, string, inlength, outlength)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_version_info() -> *const curl_version_info_data {
    curl_version_info(CURLVERSION_NOW)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_verinfo_features(d: *const curl_version_info_data) -> c_int {
    if d.is_null() { 0 } else { (*d).features }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_verinfo_version_num(d: *const curl_version_info_data) -> c_uint {
    if d.is_null() { 0 } else { (*d).version_num }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_verinfo_version(d: *const curl_version_info_data) -> *const c_char {
    if d.is_null() { ptr::null() } else { (*d).version }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_verinfo_ssl_version(d: *const curl_version_info_data) -> *const c_char {
    if d.is_null() { ptr::null() } else { (*d).ssl_version }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_verinfo_libz_version(d: *const curl_version_info_data) -> *const c_char {
    if d.is_null() { ptr::null() } else { (*d).libz_version }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_verinfo_nghttp2_version(d: *const curl_version_info_data) -> *const c_char {
    if d.is_null() { ptr::null() } else { (*d).nghttp2_version }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_verinfo_quic_version(d: *const curl_version_info_data) -> *const c_char {
    if d.is_null() { ptr::null() } else { (*d).quic_version }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_verinfo_cainfo(d: *const curl_version_info_data) -> *const c_char {
    if d.is_null() { ptr::null() } else { (*d).cainfo }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_verinfo_capath(d: *const curl_version_info_data) -> *const c_char {
    if d.is_null() { ptr::null() } else { (*d).capath }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_header(
    handle: *mut CURL,
    name: *const c_char,
    nameindex: size_t,
    origin: c_uint,
    request: c_int,
    hout: *mut *mut curl_header,
) -> CURLHcode {
    curl_easy_header(handle, name, nameindex, origin, request, hout)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_easy_nextheader(
    handle: *mut CURL,
    origin: c_uint,
    request: c_int,
    prev: *mut curl_header,
) -> *mut curl_header {
    curl_easy_nextheader(handle, origin, request, prev)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_header_name(h: *const curl_header) -> *const c_char {
    if h.is_null() { ptr::null() } else { (*h).name }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_header_value(h: *const curl_header) -> *const c_char {
    if h.is_null() { ptr::null() } else { (*h).value }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_header_amount(h: *const curl_header) -> size_t {
    if h.is_null() { 0 } else { (*h).amount }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_header_index(h: *const curl_header) -> size_t {
    if h.is_null() { 0 } else { (*h).index }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_header_origin(h: *const curl_header) -> c_uint {
    if h.is_null() { 0 } else { (*h).origin }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_share_init() -> *mut CURLSH {
    curl_share_init()
}

#[no_mangle]
pub unsafe extern "C" fn curlw_share_cleanup(share: *mut CURLSH) -> CURLSHcode {
    if share.is_null() {
        return CURLSHE_INVALID;
    }
    let key = share as usize;
    let ec = curl_share_cleanup(share);
    if ec == CURLSHE_OK {
        let _lk = share_locks_mtx().lock().unwrap();
        let g = share_locks();
        g.lock().unwrap().remove(&key);
    }
    ec
}

#[no_mangle]
pub unsafe extern "C" fn curlw_share_setopt_int(
    share: *mut CURLSH,
    option: CURLSHoption,
    value: c_int,
) -> CURLSHcode {
    if option != CURLSHOPT_SHARE && option != CURLSHOPT_UNSHARE {
        return CURLSHE_BAD_OPTION;
    }
    curl_share_setopt_int(share, option, value)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_share_enable_default_locks(share: *mut CURLSH) -> CURLSHcode {
    if share.is_null() {
        return CURLSHE_INVALID;
    }
    let key = share as usize;
    let lock_set_ptr;
    {
        let _lk = share_locks_mtx().lock().unwrap();
        let g = share_locks();
        let mut map = g.lock().unwrap();
        if map.contains_key(&key) {
            return CURLSHE_OK;
        }
        let ls = ShareLockSet::new();
        let ptr = &*ls as *const ShareLockSet as *mut c_void;
        map.insert(key, ls);
        lock_set_ptr = ptr;
    }

    let mut ec = curl_share_setopt_ptr(share, CURLSHOPT_USERDATA, lock_set_ptr);
    if ec == CURLSHE_OK {
        ec = curl_share_setopt_ptr(share, CURLSHOPT_LOCKFUNC, share_lock as *mut c_void);
    }
    if ec == CURLSHE_OK {
        ec = curl_share_setopt_ptr(share, CURLSHOPT_UNLOCKFUNC, share_unlock as *mut c_void);
    }

    if ec != CURLSHE_OK {
        let _lk = share_locks_mtx().lock().unwrap();
        let g = share_locks();
        g.lock().unwrap().remove(&key);
        curl_share_setopt_ptr(share, CURLSHOPT_LOCKFUNC, ptr::null_mut());
        curl_share_setopt_ptr(share, CURLSHOPT_UNLOCKFUNC, ptr::null_mut());
        curl_share_setopt_ptr(share, CURLSHOPT_USERDATA, ptr::null_mut());
    }
    ec
}

#[no_mangle]
pub unsafe extern "C" fn curlw_share_strerror_imp(error: CURLSHcode) -> *const c_char {
    curl_share_strerror(error)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_mime_init(easy: *mut CURL) -> *mut curl_mime {
    curl_mime_init(easy)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_mime_free(mime: *mut curl_mime) {
    curl_mime_free(mime)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_mime_addpart(mime: *mut curl_mime) -> *mut curl_mimepart {
    curl_mime_addpart(mime)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_mime_name(part: *mut curl_mimepart, name: *const c_char) -> CURLcode {
    curl_mime_name(part, name)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_mime_data(
    part: *mut curl_mimepart,
    data: *const c_char,
    datasize: size_t,
) -> CURLcode {
    curl_mime_data(part, data, datasize as isize)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_mime_filedata(part: *mut curl_mimepart, filename: *const c_char) -> CURLcode {
    curl_mime_filedata(part, filename)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_mime_filename(part: *mut curl_mimepart, filename: *const c_char) -> CURLcode {
    curl_mime_filename(part, filename)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_mime_type(part: *mut curl_mimepart, mimetype: *const c_char) -> CURLcode {
    curl_mime_type(part, mimetype)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_mime_encoder(part: *mut curl_mimepart, encoding: *const c_char) -> CURLcode {
    curl_mime_encoder(part, encoding)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_mime_headers(
    part: *mut curl_mimepart,
    headers: *mut curl_slist,
    take_ownership: c_int,
) -> CURLcode {
    curl_mime_headers(part, headers, take_ownership)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_mime_subparts(part: *mut curl_mimepart, subparts: *mut curl_mime) -> CURLcode {
    curl_mime_subparts(part, subparts)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_url() -> *mut CURLU {
    curl_url()
}

#[no_mangle]
pub unsafe extern "C" fn curlw_url_cleanup(handle: *mut CURLU) {
    curl_url_cleanup(handle)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_url_dup(in_: *const CURLU) -> *mut CURLU {
    curl_url_dup(in_)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_url_get(
    handle: *const CURLU,
    what: CURLUPart,
    part: *mut *mut c_char,
    flags: c_uint,
) -> CURLUcode {
    curl_url_get(handle, what, part, flags)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_url_set(
    handle: *mut CURLU,
    what: CURLUPart,
    part: *const c_char,
    flags: c_uint,
) -> CURLUcode {
    curl_url_set(handle, what, part, flags)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_url_strerror_imp(error: CURLUcode) -> *const c_char {
    curl_url_strerror(error)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_ws_recv(
    handle: *mut CURL,
    buffer: *mut c_void,
    buflen: size_t,
    recv: *mut size_t,
    meta: *mut *const curl_ws_frame,
) -> CURLcode {
    curl_ws_recv(handle, buffer, buflen, recv, meta)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_ws_send(
    handle: *mut CURL,
    buffer: *const c_void,
    buflen: size_t,
    sent: *mut size_t,
    fragsize: i64,
    flags: c_uint,
) -> CURLcode {
    curl_ws_send(handle, buffer, buflen, sent, fragsize as curl_off_t, flags)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_ws_meta(handle: *mut CURL) -> *const curl_ws_frame {
    curl_ws_meta(handle)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_wsframe_flags(f: *const curl_ws_frame) -> c_int {
    if f.is_null() { 0 } else { (*f).flags }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_wsframe_offset(f: *const curl_ws_frame) -> i64 {
    if f.is_null() { 0 } else { (*f).offset as i64 }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_wsframe_bytesleft(f: *const curl_ws_frame) -> i64 {
    if f.is_null() { 0 } else { (*f).bytesleft as i64 }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_wsframe_len(f: *const curl_ws_frame) -> size_t {
    if f.is_null() { 0 } else { (*f).len }
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_assign(
    multi_handle: *mut CURLM,
    sockfd: intptr_t,
    sockp: *mut c_void,
) -> CURLMcode {
    curl_multi_assign(multi_handle, sockfd as curl_socket_t, sockp)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_socket_action(
    multi_handle: *mut CURLM,
    s: intptr_t,
    ev_bitmask: c_int,
    running_handles: *mut c_int,
) -> CURLMcode {
    curl_multi_socket_action(multi_handle, s as curl_socket_t, ev_bitmask, running_handles)
}

#[no_mangle]
pub unsafe extern "C" fn curlw_multi_get_handles(multi_handle: *mut CURLM) -> *mut *mut CURL {
    curl_multi_get_handles(multi_handle)
}
