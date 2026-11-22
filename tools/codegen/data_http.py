# -*- coding: utf-8 -*-
"""HTTP / TLS registry rows for schema/http.json.

The tables below are compact text blocks (one row per line, fields separated by
"|") so they stay readable and easy to review; bootstrap_schema.py parses them
into JSON and gen.py turns the JSON into C++ lookup tables.

Column meanings
  STATUS_ROWS   code|reason phrase
  HEADER_ROWS   lower-case name|Canonical-Case|flags
                flags: R = request header, S = response header, C = the field
                may be combined into one comma separated line (RFC 9110 5.3)
  MIME_ROWS     type/subtype|comma separated file extensions (no dot)
  ALPN_ROWS     protocol id|description
  WINERR_ROWS   numeric code|symbolic name|English message

Reason phrases and canonical header names follow RFC 9110 and the IANA
registries; the unofficial status codes are the ones seen in the wild (Laravel,
Twitter, nginx, Microsoft IIS, Cloudflare) and are marked in the comment.
"""

def _rows(text):
    out = []
    for line in text.strip().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        out.append([f.strip() for f in line.split("|")])
    return out

STATUS_ROWS = _rows("""
# 1xx informational
100|Continue
101|Switching Protocols
102|Processing
103|Early Hints
# 2xx success
200|OK
201|Created
202|Accepted
203|Non-Authoritative Information
204|No Content
205|Reset Content
206|Partial Content
207|Multi-Status
208|Already Reported
226|IM Used
# 3xx redirection
300|Multiple Choices
301|Moved Permanently
302|Found
303|See Other
304|Not Modified
305|Use Proxy
307|Temporary Redirect
308|Permanent Redirect
# 4xx client error
400|Bad Request
401|Unauthorized
402|Payment Required
403|Forbidden
404|Not Found
405|Method Not Allowed
406|Not Acceptable
407|Proxy Authentication Required
408|Request Timeout
409|Conflict
410|Gone
411|Length Required
412|Precondition Failed
413|Content Too Large
414|URI Too Long
415|Unsupported Media Type
416|Range Not Satisfiable
417|Expectation Failed
418|I'm a teapot
421|Misdirected Request
422|Unprocessable Content
423|Locked
424|Failed Dependency
425|Too Early
426|Upgrade Required
428|Precondition Required
429|Too Many Requests
431|Request Header Fields Too Large
451|Unavailable For Legal Reasons
# 5xx server error
500|Internal Server Error
501|Not Implemented
502|Bad Gateway
503|Service Unavailable
504|Gateway Timeout
505|HTTP Version Not Supported
506|Variant Also Negotiates
507|Insufficient Storage
508|Loop Detected
510|Not Extended
511|Network Authentication Required
# unofficial codes, kept because real servers send them
419|Page Expired
420|Enhance Your Calm
444|Connection Closed Without Response
449|Retry With
450|Blocked by Windows Parental Controls
494|Request Header Too Large
495|SSL Certificate Error
496|SSL Certificate Required
497|HTTP Request Sent to HTTPS Port
498|Invalid Token
499|Client Closed Request
509|Bandwidth Limit Exceeded
520|Unknown Error
521|Web Server Is Down
522|Connection Timed Out
523|Origin Is Unreachable
524|A Timeout Occurred
525|SSL Handshake Failed
526|Invalid SSL Certificate
527|Railgun Error
530|Site is Frozen
598|Network Read Timeout Error
599|Network Connect Timeout Error
""")

ALPN_ROWS = _rows("""
h2|HTTP/2 over TLS
h3|HTTP/3 over QUIC
http/1.1|HTTP/1.1
http/1.0|HTTP/1.0
spdy/3.1|SPDY 3.1
acme-tls/1|ACME TLS-ALPN challenge
coap|Constrained Application Protocol
coaps|CoAP over TLS
mqtt|Message Queuing Telemetry Transport
smtp|Simple Mail Transfer Protocol
imap|Internet Message Access Protocol
pop3|Post Office Protocol 3
ftp|File Transfer Protocol
dot|DNS over TLS
doq|DNS over QUIC
ntske/1|Network Time Security key establishment
sunrpc|ONC RPC
stun.turn|Traversal Using Relays around NAT
stun.nat-discovery|NAT discovery with STUN
webrtc|WebRTC media
c-webrtc|WebRTC media (confidential)
""")

HEADER_ROWS = _rows("""
# RFC 9110 core fields
accept|Accept|RC
accept-charset|Accept-Charset|RC
accept-encoding|Accept-Encoding|RC
accept-language|Accept-Language|RC
accept-ranges|Accept-Ranges|SC
access-control-allow-credentials|Access-Control-Allow-Credentials|S
access-control-allow-headers|Access-Control-Allow-Headers|SC
access-control-allow-methods|Access-Control-Allow-Methods|SC
access-control-allow-origin|Access-Control-Allow-Origin|S
access-control-allow-private-network|Access-Control-Allow-Private-Network|S
access-control-expose-headers|Access-Control-Expose-Headers|SC
access-control-max-age|Access-Control-Max-Age|S
access-control-request-headers|Access-Control-Request-Headers|RC
access-control-request-method|Access-Control-Request-Method|R
access-control-request-private-network|Access-Control-Request-Private-Network|R
age|Age|S
allow|Allow|SC
alt-svc|Alt-Svc|SC
alt-used|Alt-Used|S
authorization|Authorization|R
cache-control|Cache-Control|RSC
cache-status|Cache-Status|SC
cdn-cache-control|CDN-Cache-Control|RSC
cdn-loop|CDN-Loop|RC
clear-site-data|Clear-Site-Data|S
connection|Connection|RSC
content-digest|Content-Digest|RSC
content-disposition|Content-Disposition|RSC
content-encoding|Content-Encoding|RSC
content-language|Content-Language|RSC
content-length|Content-Length|RSC
content-location|Content-Location|RSC
content-range|Content-Range|RSC
content-security-policy|Content-Security-Policy|SC
content-security-policy-report-only|Content-Security-Policy-Report-Only|SC
content-type|Content-Type|RSC
cookie|Cookie|R
cross-origin-embedder-policy|Cross-Origin-Embedder-Policy|S
cross-origin-opener-policy|Cross-Origin-Opener-Policy|S
cross-origin-resource-policy|Cross-Origin-Resource-Policy|S
date|Date|RSC
dnt|DNT|R
etag|ETag|S
expect|Expect|RC
expect-ct|Expect-CT|SC
expires|Expires|RSC
forwarded|Forwarded|RSC
from|From|R
host|Host|R
if-match|If-Match|RC
if-modified-since|If-Modified-Since|R
if-none-match|If-None-Match|RC
if-range|If-Range|R
if-unmodified-since|If-Unmodified-Since|R
keep-alive|Keep-Alive|RSC
last-modified|Last-Modified|S
link|Link|RSC
location|Location|S
max-forwards|Max-Forwards|R
nel|NEL|S
origin|Origin|R
origin-agent-cluster|Origin-Agent-Cluster|S
permissions-policy|Permissions-Policy|SC
pragma|Pragma|RSC
prefer|Prefer|RC
preference-applied|Preference-Applied|SC
priority|Priority|R
proxy-authenticate|Proxy-Authenticate|SC
proxy-authorization|Proxy-Authorization|R
proxy-connection|Proxy-Connection|RSC
proxy-status|Proxy-Status|SC
range|Range|R
referer|Referer|R
referrer-policy|Referrer-Policy|S
report-to|Report-To|SC
reporting-endpoints|Reporting-Endpoints|SC
repr-digest|Repr-Digest|RSC
retry-after|Retry-After|S
sec-fetch-dest|Sec-Fetch-Dest|R
sec-fetch-mode|Sec-Fetch-Mode|R
sec-fetch-site|Sec-Fetch-Site|R
sec-fetch-user|Sec-Fetch-User|R
sec-gpc|Sec-GPC|R
sec-purpose|Sec-Purpose|R
sec-speculation-tags|Sec-Speculation-Tags|R
server|Server|S
server-timing|Server-Timing|SC
service-worker-allowed|Service-Worker-Allowed|S
set-cookie|Set-Cookie|S
set-cookie2|Set-Cookie2|SC
sourcemap|Sourcemap|S
strict-transport-security|Strict-Transport-Security|S
te|TE|RC
timing-allow-origin|Timing-Allow-Origin|SC
trailer|Trailer|RSC
transfer-encoding|Transfer-Encoding|RSC
upgrade|Upgrade|RSC
upgrade-insecure-requests|Upgrade-Insecure-Requests|R
user-agent|User-Agent|R
vary|Vary|SC
via|Via|RSC
want-content-digest|Want-Content-Digest|RC
want-repr-digest|Want-Repr-Digest|RC
www-authenticate|WWW-Authenticate|SC
# web sockets and client hints
sec-websocket-accept|Sec-WebSocket-Accept|S
sec-websocket-extensions|Sec-WebSocket-Extensions|RSC
sec-websocket-key|Sec-WebSocket-Key|R
sec-websocket-protocol|Sec-WebSocket-Protocol|RSC
sec-websocket-version|Sec-WebSocket-Version|R
sec-ch-ua|Sec-CH-UA|R
sec-ch-ua-arch|Sec-CH-UA-Arch|R
sec-ch-ua-bitness|Sec-CH-UA-Bitness|R
sec-ch-ua-full-version|Sec-CH-UA-Full-Version|R
sec-ch-ua-full-version-list|Sec-CH-UA-Full-Version-List|R
sec-ch-ua-mobile|Sec-CH-UA-Mobile|R
sec-ch-ua-model|Sec-CH-UA-Model|R
sec-ch-ua-platform|Sec-CH-UA-Platform|R
sec-ch-ua-platform-version|Sec-CH-UA-Platform-Version|R
sec-ch-ua-wow64|Sec-CH-UA-WoW64|R
# proxy and load balancer vendor fields
x-forwarded-for|X-Forwarded-For|RC
x-forwarded-host|X-Forwarded-Host|RC
x-forwarded-port|X-Forwarded-Port|RC
x-forwarded-prefix|X-Forwarded-Prefix|RC
x-forwarded-proto|X-Forwarded-Proto|RC
x-forwarded-server|X-Forwarded-Server|RC
x-original-forwarded-for|X-Original-Forwarded-For|RC
x-original-url|X-Original-URL|R
x-real-ip|X-Real-IP|R
x-client-ip|X-Client-IP|R
x-cluster-client-ip|X-Cluster-Client-IP|R
x-requested-with|X-Requested-With|R
x-http-method-override|X-HTTP-Method-Override|R
x-method-override|X-Method-Override|R
x-rewrite-url|X-Rewrite-URL|R
x-wap-profile|X-WAP-Profile|R
x-ua-device|X-UA-Device|R
x-device-user-agent|X-Device-User-Agent|R
x-operamini-phone-ua|X-OperaMini-Phone-UA|R
# cache and CDN
x-cache|X-Cache|SC
x-cache-hits|X-Cache-Hits|SC
x-served-by|X-Served-By|SC
x-timer|X-Timer|SC
x-varnish|X-Varnish|S
x-proxy-cache|X-Proxy-Cache|S
x-fastly-request-id|X-Fastly-Request-ID|S
cf-cache-status|CF-Cache-Status|S
cf-ray|CF-Ray|S
cf-connecting-ip|CF-Connecting-IP|S
cf-ipcountry|CF-IPCountry|S
cf-visitor|CF-Visitor|S
cf-request-id|CF-Request-ID|S
cf-worker|CF-Worker|R
surrogate-control|Surrogate-Control|RSC
surrogate-key|Surrogate-Key|RSC
edge-control|Edge-Control|S
x-accel-buffering|X-Accel-Buffering|S
x-accel-expires|X-Accel-Expires|S
x-accel-limit-rate|X-Accel-Limit-Rate|S
x-accel-redirect|X-Accel-Redirect|S
x-sendfile|X-Sendfile|S
# GitHub API and webhooks
x-github-request-id|X-GitHub-Request-Id|S
x-github-media-type|X-GitHub-Media-Type|S
x-github-api-version-selected|X-GitHub-Api-Version-Selected|S
x-github-enterprise-version|X-GitHub-Enterprise-Version|S
x-github-delivery|X-GitHub-Delivery|S
x-github-event|X-GitHub-Event|S
x-github-hook-id|X-GitHub-Hook-ID|S
x-github-hook-installation-target-id|X-GitHub-Hook-Installation-Target-ID|S
x-github-hook-installation-target-type|X-GitHub-Hook-Installation-Target-Type|S
x-hub-signature|X-Hub-Signature|S
x-hub-signature-256|X-Hub-Signature-256|S
x-oauth-scopes|X-OAuth-Scopes|SC
x-accepted-oauth-scopes|X-Accepted-OAuth-Scopes|SC
x-ratelimit-limit|X-RateLimit-Limit|S
x-ratelimit-remaining|X-RateLimit-Remaining|S
x-ratelimit-reset|X-RateLimit-Reset|S
x-ratelimit-used|X-RateLimit-Used|S
x-ratelimit-resource|X-RateLimit-Resource|S
# tracing
traceparent|traceparent|RSC
tracestate|tracestate|RSC
baggage|baggage|RSC
sentry-trace|sentry-trace|RC
x-b3-traceid|X-B3-TraceId|RC
x-b3-spanid|X-B3-SpanId|RC
x-b3-parentspanid|X-B3-ParentSpanId|RC
x-b3-sampled|X-B3-Sampled|RC
x-b3-flags|X-B3-Flags|RC
x-ot-span-context|X-OT-Span-Context|RC
x-request-id|X-Request-ID|RSC
x-correlation-id|X-Correlation-ID|RSC
x-request-start|X-Request-Start|R
x-queue-time|X-Queue-Time|S
# storage backends (S3, GCS, Azure)
x-amz-request-id|X-Amz-Request-Id|S
x-amz-id-2|X-Amz-Id-2|S
x-amz-cf-id|X-Amz-Cf-Id|S
x-amz-cf-pop|X-Amz-Cf-Pop|S
x-amz-delete-marker|X-Amz-Delete-Marker|S
x-amz-expiration|X-Amz-Expiration|S
x-amz-mp-parts-count|X-Amz-Mp-Parts-Count|S
x-amz-restore|X-Amz-Restore|S
x-amz-server-side-encryption|X-Amz-Server-Side-Encryption|S
x-amz-server-side-encryption-aws-kms-key-id|X-Amz-Server-Side-Encryption-Aws-Kms-Key-Id|S
x-amz-server-side-encryption-customer-algorithm|X-Amz-Server-Side-Encryption-Customer-Algorithm|S
x-amz-tagging-count|X-Amz-Tagging-Count|S
x-amz-version-id|X-Amz-Version-Id|S
x-amz-website-redirect-location|X-Amz-Website-Redirect-Location|S
x-amzn-requestid|X-Amzn-Requestid|S
x-amzn-trace-id|X-Amzn-Trace-Id|S
x-goog-generation|X-Goog-Generation|S
x-goog-hash|X-Goog-Hash|SC
x-goog-metageneration|X-Goog-Metageneration|S
x-goog-storage-class|X-Goog-Storage-Class|S
x-goog-stored-content-encoding|X-Goog-Stored-Content-Encoding|S
x-goog-stored-content-length|X-Goog-Stored-Content-Length|S
x-ms-blob-type|X-Ms-Blob-Type|R
x-ms-client-request-id|X-Ms-Client-Request-Id|RSC
x-ms-copy-source|X-Ms-Copy-Source|R
x-ms-request-id|X-Ms-Request-Id|S
x-ms-version|X-Ms-Version|S
# legacy and framework fields
accept-patch|Accept-Patch|SC
accept-post|Accept-Post|SC
accept-signature|Accept-Signature|RC
apply-to-redirect-ref|Apply-To-Redirect-Ref|SC
content-base|Content-Base|RSC
content-id|Content-ID|RSC
content-md5|Content-MD5|RSC
content-transfer-encoding|Content-Transfer-Encoding|RSC
digest|Digest|RSC
ediint-features|EDIINT-Features|RC
im|IM|R
message-id|Message-ID|RSC
mime-version|MIME-Version|RSC
optional-www-authenticate|Optional-WWW-Authenticate|SC
p3p|P3P|S
public-key-pins|Public-Key-Pins|SC
public-key-pins-report-only|Public-Key-Pins-Report-Only|SC
signature|Signature|RSC
signature-input|Signature-Input|RSC
status|Status|S
tk|Tk|S
variant-vary|Variant-Vary|SC
x-aspnet-version|X-AspNet-Version|S
x-aspnetmvc-version|X-AspNetMvc-Version|S
x-content-type-options|X-Content-Type-Options|S
x-dns-prefetch-control|X-DNS-Prefetch-Control|S
x-download-options|X-Download-Options|S
x-frame-options|X-Frame-Options|S
x-permitted-cross-domain-policies|X-Permitted-Cross-Domain-Policies|S
x-powered-by|X-Powered-By|S
x-robots-tag|X-Robots-Tag|SC
x-runtime|X-Runtime|S
x-ua-compatible|X-UA-Compatible|S
x-xss-protection|X-XSS-Protection|S
""")
MIME_ROWS = _rows("""
# text
text/plain|txt,text,log,ini,conf,cfg,def,list,properties
text/html|html,htm,shtml
text/css|css
text/csv|csv
text/calendar|ics
text/markdown|md,markdown
text/xml|xml,dtd,ent
text/javascript|js,mjs
text/ecmascript|es
text/rtf|rtf
text/vcard|vcf
text/vtt|vtt
text/n3|n3
text/tab-separated-values|tsv
text/richtext|rtx
text/sgml|sgml,sgm
text/x-shellscript|sh
text/x-csh|csh
text/x-python|py
text/x-c|c
text/x-c++|cpp,cxx,cc,hpp,hxx
text/x-java-source|java
text/x-perl|pl,pm
text/x-ruby|rb
text/x-lua|lua
text/x-pascal|pas
text/x-fortran|f,for,f77,f90
text/x-asm|asm,s
text/troff|t,tr,roff,man
text/x-diff|diff,patch
text/x-nfo|nfo
text/x-setext|etx
text/x-uuencode|uu
text/x-vcalendar|vcs
text/plain-bas|bas
text/x-suse-ymp|ymp
text/prs.fallenstein.rst|rst
text/x-org|org
# application: data and web
application/json|json,map
application/ld+json|jsonld
application/manifest+json|webmanifest
application/problem+json|
application/geo+json|geojson
application/json5|json5
application/x-ndjson|ndjson
application/yaml|yaml,yml
application/toml|toml
application/sql|sql
application/graphql|graphql
application/xml|xsd,rng
application/xhtml+xml|xhtml,xht
application/atom+xml|atom
application/rss+xml|rss
application/rdf+xml|rdf
application/xslt+xml|xsl,xslt
application/x-www-form-urlencoded|
application/javascript|
application/ecmascript|es6
application/rtf|rtfx
# application: archives and packages
application/pdf|pdf
application/zip|zip
application/gzip|gz,tgz
application/x-bzip2|bz2,tbz2
application/x-xz|xz,txz
application/x-7z-compressed|7z
application/x-rar-compressed|rar
application/x-tar|tar
application/x-lzip|lz
application/x-lzma|lzma
application/x-lz4|lz4
application/zstd|zst
application/x-compress|z
application/x-brotli|br
application/x-cpio|cpio
application/x-iso9660-image|iso
application/x-apple-diskimage|dmg
application/vnd.ms-cab-compressed|cab
application/x-msi|msi
application/x-msdownload|exe,dll
application/x-msdos-program|com
application/x-bat|bat
application/x-shockwave-flash|swf
application/java-archive|jar
application/x-java-class|class
application/x-python-code|pyc,pyo
application/wasm|wasm
application/vnd.android.package-archive|apk
application/x-httpd-php|php,phtml
application/x-ms-shortcut|lnk
application/x-archive|a,lib
application/x-object|o
application/x-sharedlib|so
application/x-elf|elf
application/vnd.microsoft.portable-executable|pe
application/x-msaccess|mdb
application/x-msmoney|mny
application/x-cdlink|vcd
application/x-director|dir,dcr,dxr
application/x-doom|wad
application/x-futuresplash|spl
application/x-gnumeric|gnumeric
application/x-gtar|gtar
application/x-freearc|arc
application/x-lha|lha,lzh
application/x-arj|arj
application/x-ms-wmz|wmz
application/x-ms-xbap|xbap
application/octet-stream|bin,dat,dump,img,raw,iso2
application/vnd.sqlite3|sqlite,sqlite3,db
application/x-dbf|dbf
application/x-parquet|parquet
application/vnd.apache.arrow.file|arrow,feather
application/x-hdf|hdf
application/x-hdf5|h5
application/x-netcdf|nc
application/x-nes-rom|nes
application/x-gameboy-rom|gb,gbc
application/x-gba-rom|gba
application/x-nintendo-ds-rom|nds
application/x-snes-rom|sfc,smc
application/x-chess-pgn|pgn
application/x-blender|blend
application/x-krita|kra
application/x-subrip|srt
application/ttml+xml|ttml
application/x-tex|tex
application/x-latex|latex
application/x-texinfo|texinfo,texi
application/x-dvi|dvi
application/postscript|ps,eps
application/illustrator|ai
application/epub+zip|epub
application/x-mobipocket-ebook|mobi
application/x-fictionbook+xml|fb2
application/x-abiword|abw
application/vnd.apple.installer+xml|mpkg
application/vnd.apple.mpegurl|m3u8
application/x-mpegurl|m3u
application/dash+xml|mpd
application/smil+xml|smil,smi
application/ogg|ogx
application/pkcs7-mime|p7m,p7c
application/pkcs7-signature|p7s
application/pkcs8|p8
application/pkcs10|p10
application/x-pkcs12|p12,pfx
application/x-pem-file|pem
application/pgp-signature|asc
application/pgp-encrypted|pgp
application/x-x509-ca-cert|crt,der
application/x-pkcs7-certificates|p7b,spc
application/x-7z|7z2
# application: office documents
application/msword|doc,dot
application/vnd.openxmlformats-officedocument.wordprocessingml.document|docx
application/vnd.openxmlformats-officedocument.wordprocessingml.template|dotx
application/vnd.openxmlformats-officedocument.spreadsheetml.sheet|xlsx
application/vnd.openxmlformats-officedocument.spreadsheetml.template|xltx
application/vnd.openxmlformats-officedocument.presentationml.presentation|pptx
application/vnd.openxmlformats-officedocument.presentationml.template|potx
application/vnd.openxmlformats-officedocument.presentationml.slideshow|ppsx
application/vnd.ms-excel|xls,xlt
application/vnd.ms-powerpoint|ppt,pot,ppa
application/vnd.ms-word.document.macroenabled.12|docm
application/vnd.ms-excel.sheet.macroenabled.12|xlsm
application/vnd.ms-powerpoint.presentation.macroenabled.12|pptm
application/vnd.oasis.opendocument.text|odt
application/vnd.oasis.opendocument.spreadsheet|ods
application/vnd.oasis.opendocument.presentation|odp
application/vnd.oasis.opendocument.graphics|odg
application/vnd.visio|vsd,vst,vsw
application/vnd.ms-visio.drawing|vsdx
# audio
audio/mpeg|mp3,mpga,mp2,mp2a,m2a,m3a
audio/mp4|m4a,mp4a
audio/aac|aac
audio/flac|flac
audio/wav|wav
audio/webm|weba
audio/ogg|oga,ogg,spx
audio/opus|opus
audio/midi|mid,midi,kar,rmi
audio/x-ms-wma|wma
audio/x-aiff|aif,aiff,aifc
audio/basic|au,snd
audio/x-matroska|mka
audio/3gpp|3ga
audio/amr|amr
audio/x-m4b|m4b
audio/x-ms-wax|wax
audio/x-pn-realaudio|ra,ram
audio/x-ape|ape
audio/x-wavpack|wv
audio/x-musepack|mpc
# video
video/mp4|mp4,mp4v,mpg4
video/mpeg|mpeg,mpg,mpe,m1v,m2v
video/quicktime|mov,qt
video/webm|webm
video/ogg|ogv
video/x-matroska|mkv,mk3d
video/x-msvideo|avi
video/x-ms-wmv|wmv
video/x-flv|flv
video/3gpp|3gp,3gpp
video/3gpp2|3g2,3gp2
video/x-m4v|m4v
video/mp2t|ts,m2t,m2ts,mts
video/x-ms-asf|asf,asx
video/x-f4v|f4v
video/x-fli|fli
video/h264|h264
video/h265|h265,hevc
video/av1|av1
video/x-ms-wmx|wmx
video/x-ms-vob|vob
video/x-sgi-movie|movie
# image
image/png|png
image/jpeg|jpg,jpeg,jpe,jfif
image/gif|gif
image/webp|webp
image/avif|avif
image/bmp|bmp,dib
image/tiff|tif,tiff
image/svg+xml|svg,svgz
image/x-icon|ico
image/heic|heic
image/heif|heif
image/jxl|jxl
image/jp2|jp2,jpx
image/x-portable-bitmap|pbm
image/x-portable-graymap|pgm
image/x-portable-pixmap|ppm
image/x-portable-anymap|pnm
image/x-xbitmap|xbm
image/x-xpixmap|xpm
image/x-tga|tga
image/x-pcx|pcx
image/vnd.adobe.photoshop|psd
image/x-canon-cr2|cr2
image/x-canon-crw|crw
image/x-nikon-nef|nef
image/x-sony-arw|arw
image/x-olympus-orf|orf
image/x-fuji-raf|raf
image/x-panasonic-rw2|rw2
image/x-adobe-dng|dng
image/ktx|ktx
image/ktx2|ktx2
image/astc|astc
image/vnd-ms.dds|dds
image/x-freehand|fh,fhc,fh4,fh5,fh7
image/x-xcf|xcf
image/emf|emf
image/wmf|wmf
image/vnd.wap.wbmp|wbmp
image/x-pict|pic,pct
image/x-rgb|rgb
image/x-xwindowdump|xwd
image/x-bpg|bpg
image/jxr|jxr
image/x-exr|exr
image/vnd.djvu|djvu,djv
image/vnd.microsoft.icon|ico2
image/x-cmu-raster|ras
image/x-portable-floatmap|pfm
# fonts
font/ttf|ttf
font/otf|otf
font/woff|woff
font/woff2|woff2
font/collection|ttc
font/sfnt|sfnt
application/vnd.ms-fontobject|eot
# multipart and message
multipart/form-data|
multipart/mixed|
multipart/alternative|
multipart/related|
multipart/parallel|
multipart/byteranges|
multipart/encrypted|
multipart/signed|
multipart/digest|
multipart/report|
multipart/voice-message|
message/rfc822|eml,mime
message/http|
message/partial|
message/news|
message/delivery-status|
message/disposition-notification|
message/global|
message/external-body|
# models and 3d
model/gltf+json|gltf
model/gltf-binary|glb
model/stl|stl
model/obj|obj
model/vrml|wrl
model/3mf|3mf
model/x3d+xml|x3d
model/vnd.collada+xml|dae
model/mesh|msh
model/iges|igs,iges
# geo and science
chemical/x-pdb|pdb
chemical/x-xyz|xyz
application/vnd.google-earth.kml+xml|kml
application/vnd.google-earth.kmz|kmz
application/gpx+xml|gpx
application/vnd.garmin.tcx+xml|tcx
""")
WINERR_ROWS = _rows("""
0|ERROR_SUCCESS|The operation completed successfully.
1|ERROR_INVALID_FUNCTION|Incorrect function.
2|ERROR_FILE_NOT_FOUND|The system cannot find the file specified.
3|ERROR_PATH_NOT_FOUND|The system cannot find the path specified.
4|ERROR_TOO_MANY_OPEN_FILES|The system cannot open the file.
5|ERROR_ACCESS_DENIED|Access is denied.
6|ERROR_INVALID_HANDLE|The handle is invalid.
7|ERROR_ARENA_TRASHED|The storage control blocks were destroyed.
8|ERROR_NOT_ENOUGH_MEMORY|Not enough memory resources are available to process this command.
9|ERROR_INVALID_BLOCK|The storage control block address is invalid.
10|ERROR_BAD_ENVIRONMENT|The environment is incorrect.
11|ERROR_BAD_FORMAT|An attempt was made to load a program with an incorrect format.
12|ERROR_INVALID_ACCESS|The access code is invalid.
13|ERROR_INVALID_DATA|The data is invalid.
14|ERROR_OUTOFMEMORY|Not enough memory resources are available to complete this operation.
15|ERROR_INVALID_DRIVE|The system cannot find the drive specified.
16|ERROR_CURRENT_DIRECTORY|The directory cannot be removed.
17|ERROR_NOT_SAME_DEVICE|The system cannot move the file to a different disk drive.
18|ERROR_NO_MORE_FILES|There are no more files.
19|ERROR_WRITE_PROTECT|The media is write protected.
20|ERROR_BAD_UNIT|The system cannot find the device specified.
21|ERROR_NOT_READY|The device is not ready.
22|ERROR_BAD_COMMAND|The device does not recognize the command.
23|ERROR_CRC|Data error (cyclic redundancy check).
24|ERROR_BAD_LENGTH|The program issued a command but the command length is incorrect.
25|ERROR_SEEK|The drive cannot locate a specific area or track on the disk.
26|ERROR_NOT_DOS_DISK|The specified disk or diskette cannot be accessed.
27|ERROR_SECTOR_NOT_FOUND|The drive cannot find the sector requested.
28|ERROR_OUT_OF_PAPER|The printer is out of paper.
29|ERROR_WRITE_FAULT|The system cannot write to the specified device.
30|ERROR_READ_FAULT|The system cannot read from the specified device.
31|ERROR_GEN_FAILURE|A device attached to the system is not functioning.
32|ERROR_SHARING_VIOLATION|The process cannot access the file because it is being used by another process.
33|ERROR_LOCK_VIOLATION|The process cannot access the file because another process has locked a portion of the file.
34|ERROR_WRONG_DISK|The wrong diskette is in the drive.
36|ERROR_SHARING_BUFFER_EXCEEDED|Too many files opened for sharing.
38|ERROR_HANDLE_EOF|Reached the end of the file.
39|ERROR_HANDLE_DISK_FULL|The disk is full.
50|ERROR_NOT_SUPPORTED|The request is not supported.
51|ERROR_REM_NOT_LIST|Windows cannot find the network path.
52|ERROR_DUP_NAME|A duplicate name exists on the network.
53|ERROR_BAD_NETPATH|The network path was not found.
54|ERROR_NETWORK_BUSY|The network is busy.
55|ERROR_DEV_NOT_EXIST|The specified network resource or device is no longer available.
56|ERROR_TOO_MANY_CMDS|The network BIOS command limit has been reached.
57|ERROR_ADAP_HDW_ERR|A network adapter hardware error occurred.
58|ERROR_BAD_NET_RESP|The specified server cannot perform the requested operation.
59|ERROR_UNEXP_NET_ERR|An unexpected network error occurred.
60|ERROR_BAD_REM_ADAP|The remote adapter is not compatible.
61|ERROR_PRINTQ_FULL|The printer queue is full.
62|ERROR_NO_SPOOL_SPACE|Space to store the file waiting to be printed is not available on the server.
63|ERROR_PRINT_CANCELLED|Your file waiting to be printed was deleted.
64|ERROR_NETNAME_DELETED|The specified network name is no longer available.
65|ERROR_NETWORK_ACCESS_DENIED|Network access is denied.
66|ERROR_BAD_DEV_TYPE|The network resource type is not correct.
67|ERROR_BAD_NET_NAME|The network name cannot be found.
68|ERROR_TOO_MANY_NAMES|The name limit for the local computer network adapter card was exceeded.
69|ERROR_TOO_MANY_SESS|The network BIOS session limit was exceeded.
70|ERROR_SHARING_PAUSED|The remote server has been paused or is in the process of being started.
71|ERROR_REQ_NOT_ACCEP|No more connections can be made to this remote computer at this time.
72|ERROR_REDIR_PAUSED|The specified printer or disk device has been paused.
80|ERROR_FILE_EXISTS|The file exists.
82|ERROR_CANNOT_MAKE|The directory or file cannot be created.
83|ERROR_FAIL_I24|Fail on INT 24.
84|ERROR_OUT_OF_STRUCTURES|Storage to process this request is not available.
85|ERROR_ALREADY_ASSIGNED|The local device name is already in use.
86|ERROR_INVALID_PASSWORD|The specified network password is not correct.
87|ERROR_INVALID_PARAMETER|The parameter is incorrect.
88|ERROR_NET_WRITE_FAULT|A write fault occurred on the network.
89|ERROR_NO_PROC_SLOTS|The system cannot start another process at this time.
100|ERROR_TOO_MANY_SEMAPHORES|Cannot create another system semaphore.
101|ERROR_EXCL_SEM_ALREADY_OWNED|The exclusive semaphore is owned by another process.
102|ERROR_SEM_IS_SET|The semaphore is set and cannot be closed.
103|ERROR_TOO_MANY_SEM_REQUESTS|The semaphore cannot be set again.
104|ERROR_INVALID_AT_INTERRUPT_TIME|Cannot request exclusive semaphores at interrupt time.
105|ERROR_SEM_OWNER_DIED|The previous ownership of this semaphore has ended.
107|ERROR_DISK_CHANGE|The program stopped because an alternate diskette was not inserted.
108|ERROR_DRIVE_LOCKED|The disk is in use or locked by another process.
109|ERROR_BROKEN_PIPE|The pipe has been ended.
110|ERROR_OPEN_FAILED|The system cannot open the device or file specified.
111|ERROR_BUFFER_OVERFLOW|The file name is too long.
112|ERROR_DISK_FULL|There is not enough space on the disk.
113|ERROR_NO_MORE_SEARCH_HANDLES|No more internal file identifiers available.
114|ERROR_INVALID_TARGET_HANDLE|The target internal file identifier is incorrect.
117|ERROR_INVALID_CATEGORY|The IOCTL call made by the application program is not correct.
118|ERROR_INVALID_VERIFY_SWITCH|The verify-on-write switch parameter value is not correct.
119|ERROR_BAD_DRIVER_LEVEL|The system does not support the command requested.
120|ERROR_CALL_NOT_IMPLEMENTED|This function is not supported on this system.
121|ERROR_SEM_TIMEOUT|The semaphore timeout period has expired.
122|ERROR_INSUFFICIENT_BUFFER|The data area passed to a system call is too small.
123|ERROR_INVALID_NAME|The filename, directory name, or volume label syntax is incorrect.
124|ERROR_INVALID_LEVEL|The system call level is not correct.
125|ERROR_NO_VOLUME_LABEL|The disk has no volume label.
126|ERROR_MOD_NOT_FOUND|The specified module could not be found.
127|ERROR_PROC_NOT_FOUND|The specified procedure could not be found.
128|ERROR_WAIT_NO_CHILDREN|There are no child processes to wait for.
130|ERROR_DIRECT_ACCESS_HANDLE|Attempt to use a file handle to an open disk partition for an operation other than raw disk I/O.
131|ERROR_NEGATIVE_SEEK|An attempt was made to move the file pointer before the beginning of the file.
132|ERROR_SEEK_ON_DEVICE|The file pointer cannot be set on the specified device or file.
183|ERROR_ALREADY_EXISTS|Cannot create a file when that file already exists.
203|ERROR_ENVVAR_NOT_FOUND|The system could not find the environment option that was entered.
206|ERROR_FILENAME_EXCED_RANGE|The filename or extension is too long.
259|ERROR_NO_MORE_ITEMS|No more data is available.
267|ERROR_DIRECTORY|The directory name is invalid.
487|ERROR_INVALID_ADDRESS|Attempt to access invalid address.
534|ERROR_ARITHMETIC_OVERFLOW|Arithmetic result exceeded 32 bits.
535|ERROR_PIPE_CONNECTED|There is a process on the other end of the pipe.
536|ERROR_PIPE_LISTENING|Waiting for a process to open the other end of the pipe.
995|ERROR_OPERATION_ABORTED|The I/O operation has been aborted because of either a thread exit or an application request.
996|ERROR_IO_INCOMPLETE|Overlapped I/O event is not in a signaled state.
997|ERROR_IO_PENDING|Overlapped I/O operation is in progress.
998|ERROR_NOACCESS|Invalid access to memory location.
1003|ERROR_CAN_NOT_COMPLETE|An attempt was made to reference a token that does not exist.
1004|ERROR_INVALID_FLAGS|The flags are invalid.
1015|ERROR_REGISTRY_CORRUPT|The configuration registry database is corrupt.
1016|ERROR_REGISTRY_IO_FAILED|The configuration registry database could not be written.
1017|ERROR_NOT_REGISTRY_FILE|The file cannot be opened as a registry hive.
1018|ERROR_KEY_DELETED|The registry key could not be deleted.
1019|ERROR_NO_LOG_SPACE|Some of the registry entries could not be written.
1020|ERROR_KEY_HAS_CHILDREN|A registry key cannot be deleted because it has child keys.
1021|ERROR_CHILD_MUST_BE_VOLATILE|A registry key cannot be created as volatile.
1022|ERROR_NOTIFY_ENUM_DIR|The requested notification could not be honored.
1052|ERROR_INVALID_SERVICE_CONTROL|The requested control is not valid for this service.
1053|ERROR_SERVICE_REQUEST_TIMEOUT|The service did not respond to the start or control request in a timely fashion.
1054|ERROR_SERVICE_NO_THREAD|A thread could not be created for the service.
1055|ERROR_SERVICE_DATABASE_LOCKED|The service database is locked.
1056|ERROR_SERVICE_ALREADY_RUNNING|An instance of the service is already running.
1057|ERROR_INVALID_SERVICE_ACCOUNT|The account name is invalid or does not exist, or the password is invalid for the account name specified.
1058|ERROR_SERVICE_DISABLED|The service cannot be started, either because it is disabled or because it has no enabled devices associated with it.
1059|ERROR_CIRCULAR_DEPENDENCY|Circular service dependency was specified.
1060|ERROR_SERVICE_DOES_NOT_EXIST|The specified service does not exist as an installed service.
1061|ERROR_SERVICE_CANNOT_ACCEPT_CTRL|The service cannot accept control messages at this time.
1062|ERROR_SERVICE_NOT_ACTIVE|The service has not been started.
1063|ERROR_FAILED_SERVICE_CONTROLLER_CONNECT|The service process could not connect to the service controller.
1064|ERROR_EXCEPTION_IN_SERVICE|An exception occurred in the service when handling the control request.
1065|ERROR_DATABASE_DOES_NOT_EXIST|The database specified does not exist.
1066|ERROR_SERVICE_SPECIFIC_ERROR|The service has returned a service-specific error code.
1067|ERROR_PROCESS_ABORTED|The process terminated unexpectedly.
1068|ERROR_SERVICE_DEPENDENCY_FAIL|The dependency service or group failed to start.
1069|ERROR_SERVICE_LOGON_FAILED|The service did not start due to a logon failure.
1070|ERROR_SERVICE_START_HANG|After starting, the service hung in a start-pending state.
1071|ERROR_INVALID_SERVICE_LOCK|The specified service database lock is invalid.
1072|ERROR_SERVICE_MARKED_FOR_DELETE|The specified service has been marked for deletion.
1073|ERROR_SERVICE_EXISTS|The specified service already exists.
1075|ERROR_SERVICE_DEPENDENCY_DELETED|The dependency service does not exist or has been marked for deletion.
1078|ERROR_DUPLICATE_SERVICE_NAME|The name is already in use as either a service name or a service display name.
1100|ERROR_END_OF_MEDIA|The physical end of the tape has been reached.
1114|ERROR_DLL_INIT_FAILED|A dynamic link library (DLL) initialization routine failed.
1115|ERROR_SHUTDOWN_IN_PROGRESS|A system shutdown is in progress.
1116|ERROR_NO_SHUTDOWN_IN_PROGRESS|Unable to abort the system shutdown because no shutdown was in progress.
1117|ERROR_IO_DEVICE|The request could not be performed because of an I/O device error.
1118|ERROR_SERIAL_NO_DEVICE|No serial device was successfully initialized.
1150|ERROR_OLD_WIN_VERSION|The specified program requires a newer version of Windows.
1151|ERROR_APP_WRONG_OS|The specified program is not a Windows or MS-DOS program.
1152|ERROR_SINGLE_INSTANCE_APP|Cannot start more than one instance of the specified program.
1153|ERROR_RMODE_APP|The specified program was written for an earlier version of Windows.
1154|ERROR_INVALID_DLL|One of the library files needed to run this application is damaged.
1155|ERROR_NO_ASSOCIATION|No application is associated with the specified file for this operation.
1156|ERROR_DDE_FAIL|An error occurred in sending the command to the application.
1157|ERROR_DLL_NOT_FOUND|One of the library files needed to run this application cannot be found.
1158|ERROR_NO_MORE_USER_HANDLES|The current process has used all of its system allowance of handles for Window Manager objects.
1159|ERROR_MESSAGE_SYNC_ONLY|The message can be used only with synchronous operations.
1160|ERROR_SOURCE_ELEMENT_EMPTY|The indicated source element has no media.
1161|ERROR_DESTINATION_ELEMENT_FULL|The indicated destination element already contains media.
1162|ERROR_ILLEGAL_ELEMENT_ADDRESS|The indicated element does not exist.
1163|ERROR_MAGAZINE_NOT_PRESENT|The indicated element is part of a magazine that is not present.
1164|ERROR_DEVICE_REINITIALIZATION_NEEDED|The indicated device requires reinitialization due to hardware errors.
1165|ERROR_DEVICE_REQUIRES_CLEANING|The device has indicated that cleaning is required before further operations are attempted.
1166|ERROR_DEVICE_DOOR_OPEN|The device has indicated that its door is open.
1167|ERROR_DEVICE_NOT_CONNECTED|The device is not connected.
1168|ERROR_NOT_FOUND|Element not found.
1169|ERROR_NO_MATCH|There was no match for the specified key in the index.
1170|ERROR_SET_NOT_FOUND|The property set specified does not exist on the object.
1171|ERROR_POINT_NOT_FOUND|The point passed to GetMouseMovePoints is not in the buffer.
1172|ERROR_NO_TRACKING_SERVICE|The tracking (workstation) service is not running.
1173|ERROR_NO_VOLUME_ID|The Volume ID could not be found.
1200|ERROR_BAD_DEVICE|The specified device name is invalid.
1201|ERROR_CONNECTION_UNAVAIL|The device is not currently connected but it is a remembered connection.
1202|ERROR_DEVICE_ALREADY_REMEMBERED|The local device name has a remembered connection to another network resource.
1203|ERROR_NO_NET_OR_BAD_PATH|The network path was either typed incorrectly, does not exist, or the network provider is not currently available.
1204|ERROR_BAD_PROVIDER|The specified network provider name is invalid.
1205|ERROR_CANNOT_OPEN_PROFILE|Unable to open the network connection profile.
1206|ERROR_BAD_PROFILE|The network connection profile is corrupt.
1207|ERROR_NOT_CONTAINER|Cannot enumerate a noncontainer.
1208|ERROR_EXTENDED_ERROR|An extended error has occurred.
1209|ERROR_INVALID_GROUPNAME|The format of the specified group name is invalid.
1210|ERROR_INVALID_COMPUTERNAME|The format of the specified computer name is invalid.
1211|ERROR_INVALID_EVENTNAME|The format of the specified event name is invalid.
1212|ERROR_INVALID_DOMAINNAME|The format of the specified domain name is invalid.
1213|ERROR_INVALID_SERVICENAME|The format of the specified service name is invalid.
1214|ERROR_INVALID_NETNAME|The format of the specified network name is invalid.
1215|ERROR_INVALID_SHARENAME|The format of the specified share name is invalid.
1216|ERROR_INVALID_PASSWORDNAME|The format of the specified password is invalid.
1217|ERROR_INVALID_MESSAGENAME|The format of the specified message name is invalid.
1218|ERROR_INVALID_MESSAGEDEST|The format of the specified message destination is invalid.
1219|ERROR_SESSION_CREDENTIAL_CONFLICT|Multiple connections to a server or shared resource by the same user, using more than one user name, are not allowed.
1220|ERROR_REMOTE_SESSION_LIMIT_EXCEEDED|An attempt was made to establish a session to a network server, but there are already too many sessions established to that server.
1221|ERROR_DUP_DOMAINNAME|The workgroup or domain name is already in use by another computer on the network.
1222|ERROR_NO_NETWORK|The network is not present or not started.
1223|ERROR_CANCELLED|The operation was canceled by the user.
1224|ERROR_USER_MAPPED_FILE|The requested operation cannot be performed on a file with a user-mapped section open.
1225|ERROR_CONNECTION_REFUSED|The remote computer refused the network connection.
1226|ERROR_GRACEFUL_DISCONNECT|The network connection was gracefully closed.
1227|ERROR_ADDRESS_ALREADY_ASSOCIATED|The network transport endpoint already has an address associated with it.
1228|ERROR_ADDRESS_NOT_ASSOCIATED|An address has not yet been associated with the network endpoint.
1229|ERROR_CONNECTION_INVALID|An operation was attempted on a nonexistent network connection.
1230|ERROR_CONNECTION_ACTIVE|An invalid operation was attempted on an active network connection.
1231|ERROR_NETWORK_UNREACHABLE|The network location cannot be reached.
1232|ERROR_HOST_UNREACHABLE|The network location cannot be reached.
1233|ERROR_PROTOCOL_UNREACHABLE|The network location cannot be reached.
1234|ERROR_PORT_UNREACHABLE|No service is operating at the destination network endpoint on the remote system.
1235|ERROR_REQUEST_ABORTED|The request was aborted.
1236|ERROR_CONNECTION_ABORTED|The network connection was aborted by the local system.
1237|ERROR_RETRY|The operation could not be completed. A retry should be performed.
1238|ERROR_CONNECTION_COUNT_LIMIT|A connection to the server could not be made because the limit on the number of concurrent connections for this account has been reached.
1239|ERROR_LOGIN_TIME_RESTRICTION|Attempting to log in during an unauthorized time of day for this account.
1240|ERROR_LOGIN_WKSTA_RESTRICTION|The account is not authorized to log in from this station.
1241|ERROR_INCORRECT_ADDRESS|The network address could not be used for the operation requested.
1242|ERROR_ALREADY_REGISTERED|The service is already registered.
1243|ERROR_SERVICE_NOT_FOUND|The specified service does not exist.
1244|ERROR_NOT_AUTHENTICATED|The operation being requested was not performed because the user has not been authenticated.
1245|ERROR_NOT_LOGGED_ON|The operation being requested was not performed because the user has not logged on to the network.
1246|ERROR_CONTINUE|Continue with work in progress.
1247|ERROR_ALREADY_INITIALIZED|An attempt was made to perform an initialization operation when initialization has already been completed.
1248|ERROR_NO_MORE_DEVICES|No more local devices.
1249|ERROR_NO_SUCH_SITE|The specified site does not exist.
1265|ERROR_DOWNGRADE_DETECTED|A downgrade was detected.
1300|ERROR_NOT_ALL_ASSIGNED|Not all privileges or groups referenced are assigned to the caller.
1301|ERROR_SOME_NOT_MAPPED|Some mapping between account names and security IDs was not done.
1302|ERROR_NO_QUOTAS_FOR_ACCOUNT|No system quota limits are specifically set for this account.
1303|ERROR_LOCAL_USER_SESSION_KEY|No encryption key is available.
1304|ERROR_NULL_LM_PASSWORD|The password cannot be changed.
1305|ERROR_UNKNOWN_REVISION|The revision level is unknown.
1306|ERROR_REVISION_MISMATCH|Indicates two revision levels are incompatible.
1307|ERROR_INVALID_OWNER|This security ID may not be assigned as the owner of this object.
1308|ERROR_INVALID_PRIMARY_GROUP|This security ID may not be assigned as the primary group of an object.
1309|ERROR_NO_IMPERSONATION_TOKEN|An attempt has been made to operate on an impersonation token by a thread that is not currently impersonating a client.
1310|ERROR_CANT_DISABLE_MANDATORY|The group may not be disabled.
1311|ERROR_NO_LOGON_SERVERS|There are currently no logon servers available to service the logon request.
1312|ERROR_NO_SUCH_LOGON_SESSION|A specified logon session does not exist.
1313|ERROR_NO_SUCH_PRIVILEGE|A specified privilege does not exist.
1314|ERROR_PRIVILEGE_NOT_HELD|A required privilege is not held by the client.
1315|ERROR_INVALID_ACCOUNT_NAME|The name provided is not a properly formed account name.
1316|ERROR_USER_EXISTS|The specified account already exists.
1317|ERROR_NO_SUCH_USER|The specified account does not exist.
1318|ERROR_GROUP_EXISTS|The specified group already exists.
1319|ERROR_NO_SUCH_GROUP|The specified group does not exist.
1320|ERROR_MEMBER_IN_GROUP|The specified user account is already in the specified group account.
1321|ERROR_MEMBER_NOT_IN_GROUP|The specified user account is not a member of the specified group account.
1322|ERROR_LAST_ADMIN|The last remaining administration account cannot be disabled or deleted.
1323|ERROR_WRONG_PASSWORD|Unable to update the password. The value provided as the current password is incorrect.
1324|ERROR_ILL_FORMED_PASSWORD|Unable to update the password. The value provided for the new password contains values that are not allowed in passwords.
1325|ERROR_PASSWORD_RESTRICTION|Unable to update the password. The value provided for the new password does not meet the length, complexity, or history requirements of the domain.
1326|ERROR_LOGON_FAILURE|The user name or password is incorrect.
1327|ERROR_ACCOUNT_RESTRICTION|Logon failure: account restriction.
1328|ERROR_INVALID_LOGON_HOURS|Logon failure: account logon time restriction violation.
1329|ERROR_INVALID_WORKSTATION|Logon failure: user not allowed to log on to this computer.
1330|ERROR_PASSWORD_EXPIRED|Logon failure: the specified account password has expired.
1331|ERROR_ACCOUNT_DISABLED|Logon failure: account currently disabled.
1332|ERROR_NONE_MAPPED|No mapping between account names and security IDs was done.
1355|ERROR_NO_SUCH_DOMAIN|The specified domain either does not exist or could not be contacted.
1356|ERROR_DOMAIN_EXISTS|The specified domain already exists.
1357|ERROR_DOMAIN_LIMIT_EXCEEDED|An attempt was made to exceed the limit on the number of domains per server.
1358|ERROR_INTERNAL_DB_CORRUPTION|Unable to complete the requested operation because of either a catastrophic media failure or a data structure corruption on the disk.
1359|ERROR_INTERNAL_ERROR|An internal error occurred.
1360|ERROR_GENERIC_NOT_MAPPED|Generic access types were contained in an access mask which should already be mapped to nongeneric types.
1361|ERROR_BAD_DESCRIPTOR_FORMAT|A security descriptor is not in the right format.
1362|ERROR_NOT_LOGON_PROCESS|The requested action is restricted for use by logon processes only.
1363|ERROR_LOGON_SESSION_EXISTS|Cannot start a new logon session with an ID that is already in use.
1364|ERROR_NO_SUCH_PACKAGE|A specified authentication package is unknown.
1365|ERROR_BAD_LOGON_SESSION_STATE|The logon session is not in a state that is consistent with the requested operation.
1366|ERROR_LOGON_SESSION_COLLISION|The logon session ID is already in use.
1367|ERROR_INVALID_LOGON_TYPE|A logon request contained an invalid logon type value.
1368|ERROR_CANNOT_IMPERSONATE|Unable to impersonate using a named pipe until data has been read from that pipe.
1375|ERROR_TOKEN_ALREADY_IN_USE|The token is already in use as a primary token.
1376|ERROR_NO_SUCH_ALIAS|The specified local group does not exist.
1377|ERROR_MEMBER_NOT_IN_ALIAS|The specified account name is not a member of the local group.
1378|ERROR_MEMBER_IN_ALIAS|The specified account name is already a member of the local group.
1379|ERROR_ALIAS_EXISTS|The specified local group already exists.
1380|ERROR_LOGON_NOT_GRANTED|Logon failure: the user has not been granted the requested logon type at this computer.
1381|ERROR_TOO_MANY_SECRETS|The maximum number of secrets that may be stored in a single system has been exceeded.
1382|ERROR_SECRET_TOO_LONG|The length of a secret exceeds the maximum length allowed.
1383|ERROR_INTERNAL_DB_ERROR|The local security authority database contains an internal inconsistency.
1384|ERROR_TOO_MANY_CONTEXT_IDS|During a logon attempt, the user's security context accumulated too many security IDs.
1385|ERROR_LOGON_TYPE_NOT_GRANTED|Logon failure: the user has not been granted the requested logon type at this computer.
1392|ERROR_FILE_CORRUPT|The file or directory is corrupted and unreadable.
1393|ERROR_DISK_CORRUPT|The disk structure is corrupted and unreadable.
1394|ERROR_NO_USER_SESSION_KEY|There is no user session key for the specified logon session.
1395|ERROR_LICENSE_QUOTA_EXCEEDED|The service being accessed is licensed for a particular number of connections.
1396|ERROR_WRONG_TARGET_NAME|The target account name is incorrect.
1397|ERROR_MUTUAL_AUTH_FAILED|Mutual authentication failed.
1398|ERROR_TIME_SKEW|There is a time and/or date difference between the client and server.
1399|ERROR_CURRENT_DOMAIN_NOT_ALLOWED|This operation cannot be performed on the current domain.
1400|ERROR_INVALID_WINDOW_HANDLE|Invalid window handle.
1401|ERROR_INVALID_MENU_HANDLE|Invalid menu handle.
1402|ERROR_INVALID_CURSOR_HANDLE|Invalid cursor handle.
1403|ERROR_INVALID_ACCEL_HANDLE|Invalid accelerator table handle.
1404|ERROR_INVALID_HOOK_HANDLE|Invalid hook handle.
1406|ERROR_TLW_WITH_WSCHILD|Cannot create a top-level child window.
1407|ERROR_CANNOT_FIND_WND_CLASS|Cannot find window class.
1408|ERROR_WINDOW_OF_OTHER_THREAD|Invalid window; it belongs to another thread.
1409|ERROR_HOTKEY_ALREADY_REGISTERED|Hot key is already registered.
1410|ERROR_CLASS_ALREADY_EXISTS|Class already exists.
1411|ERROR_CLASS_DOES_NOT_EXIST|Class does not exist.
1413|ERROR_INVALID_INDEX|Invalid index.
1414|ERROR_INVALID_ICON_HANDLE|Invalid icon handle.
1418|ERROR_CLIPBOARD_NOT_OPEN|Thread does not have a clipboard open.
1419|ERROR_HOTKEY_NOT_REGISTERED|Hot key is not registered.
1420|ERROR_WINDOW_NOT_DIALOG|The window is not a valid dialog window.
1421|ERROR_CONTROL_ID_NOT_FOUND|Control ID not found.
1425|ERROR_DC_NOT_FOUND|Invalid device context (DC) handle.
1426|ERROR_INVALID_HOOK_FILTER|Invalid hook procedure type.
1427|ERROR_INVALID_FILTER_PROC|Invalid hook procedure.
1428|ERROR_HOOK_NEEDS_HMOD|Cannot set nonlocal hook without a module handle.
1429|ERROR_GLOBAL_ONLY_HOOK|This hook procedure can only be set globally.
1430|ERROR_JOURNAL_HOOK_SET|The journal hook procedure is already installed.
1431|ERROR_HOOK_NOT_INSTALLED|The hook procedure is not installed.
1438|ERROR_INVALID_MSGBOX_STYLE|Invalid message box style.
1439|ERROR_INVALID_SPI_VALUE|Invalid system-wide (SPI_*) parameter.
1440|ERROR_SCREEN_ALREADY_LOCKED|Screen already locked.
1442|ERROR_NOT_CHILD_WINDOW|The window is not a child window.
1443|ERROR_INVALID_GW_COMMAND|Invalid GW_* command.
1444|ERROR_INVALID_THREAD_ID|Invalid thread identifier.
1446|ERROR_POPUP_ALREADY_ACTIVE|Popup menu already active.
1447|ERROR_NO_SCROLLBARS|The window does not have scroll bars.
1449|ERROR_INVALID_SHOWWIN_COMMAND|Invalid show window command.
1450|ERROR_NO_SYSTEM_RESOURCES|Insufficient system resources exist to complete the requested service.
1453|ERROR_WORKING_SET_QUOTA|Insufficient quota to complete the requested service.
1454|ERROR_PAGEFILE_QUOTA|Insufficient quota to complete the requested service.
1455|ERROR_COMMITMENT_LIMIT|The paging file is too small for this operation to complete.
1456|ERROR_MENU_ITEM_NOT_FOUND|A menu item was not found.
1457|ERROR_INVALID_KEYBOARD_HANDLE|Invalid keyboard layout handle.
1458|ERROR_HOOK_TYPE_NOT_ALLOWED|Hook type not allowed.
1459|ERROR_REQUIRES_INTERACTIVE_WINDOWSTATION|This operation requires an interactive window station.
1460|ERROR_TIMEOUT|This operation returned because the timeout period expired.
1461|ERROR_INVALID_MONITOR_HANDLE|Invalid monitor handle.
1462|ERROR_INCORRECT_SIZE|Incorrect size argument.
1463|ERROR_SYMLINK_CLASS_DISABLED|The symbolic link cannot be followed because its type is disabled.
1464|ERROR_SYMLINK_NOT_SUPPORTED|This application does not support the current operation on symbolic links.
1465|ERROR_XML_PARSE_ERROR|Windows was unable to parse the requested XML data.
1466|ERROR_XMLDSIG_ERROR|An error was encountered while processing an XML digital signature.
1467|ERROR_RESTART_APPLICATION|This application must be restarted.
1468|ERROR_WRONG_COMPARTMENT|The caller made the connection request in the wrong routing compartment.
1469|ERROR_AUTHIP_FAILURE|There was an AuthIP failure when attempting to connect to the remote host.
1500|ERROR_EVENTLOG_FILE_CORRUPT|The event log file is corrupted.
1501|ERROR_EVENTLOG_CANT_START|No event log file could be opened, so the event logging service did not start.
1502|ERROR_LOG_FILE_FULL|The event log file is full.
1503|ERROR_EVENTLOG_FILE_CHANGED|The event log file has changed between read operations.
1601|ERROR_INSTALL_SERVICE_FAILURE|The Windows Installer service could not be accessed.
1602|ERROR_INSTALL_USEREXIT|The user canceled installation.
1603|ERROR_INSTALL_FAILURE|A fatal error occurred during installation.
1604|ERROR_INSTALL_SUSPEND|Installation suspended, incomplete.
1605|ERROR_UNKNOWN_PRODUCT|This action is only valid for products that are currently installed.
1606|ERROR_UNKNOWN_FEATURE|Feature ID not registered.
1607|ERROR_UNKNOWN_COMPONENT|Component ID not registered.
1608|ERROR_UNKNOWN_PROPERTY|Unknown property.
1609|ERROR_INVALID_HANDLE_STATE|Handle is in an invalid state.
1610|ERROR_BAD_CONFIGURATION|The configuration data for this product is corrupt.
1611|ERROR_INDEX_ABSENT|Component qualifier not present.
1612|ERROR_INSTALL_SOURCE_ABSENT|The installation source for this product is not available.
1613|ERROR_INSTALL_PACKAGE_VERSION|This installation package cannot be installed by the Windows Installer service.
1614|ERROR_PRODUCT_UNINSTALLED|Product is uninstalled.
1615|ERROR_BAD_QUERY_SYNTAX|SQL query syntax invalid or unsupported.
1618|ERROR_INSTALL_ALREADY_RUNNING|Another installation is already in progress.
1619|ERROR_INSTALL_PACKAGE_OPEN_FAILED|This installation package could not be opened.
1620|ERROR_INSTALL_PACKAGE_INVALID|This installation package could not be opened. Contact the application vendor to verify that this is a valid Windows Installer package.
1621|ERROR_INSTALL_UI_FAILURE|There was an error starting the Windows Installer service user interface.
1622|ERROR_INSTALL_LOG_FAILURE|Error opening installation log file.
1623|ERROR_INSTALL_LANGUAGE_UNSUPPORTED|The language of this installation package is not supported by your system.
1624|ERROR_INSTALL_TRANSFORM_FAILURE|Error applying transforms.
1625|ERROR_INSTALL_PACKAGE_REJECTED|This installation is forbidden by system policy.
1626|ERROR_FUNCTION_NOT_CALLED|Function could not be executed.
1627|ERROR_FUNCTION_FAILED|Function failed during execution.
1628|ERROR_INVALID_TABLE|Invalid or unknown table specified.
1629|ERROR_DATATYPE_MISMATCH|Data supplied is of wrong type.
1630|ERROR_UNSUPPORTED_TYPE|Data of this type is not supported.
1631|ERROR_CREATE_FAILED|The Windows Installer service failed to start.
1632|ERROR_INSTALL_TEMP_UNWRITABLE|The Temp folder is on a drive that is full or is inaccessible.
1633|ERROR_INSTALL_PLATFORM_UNSUPPORTED|This installation package is not supported by this processor type.
1634|ERROR_INSTALL_NOTUSED|Component not used on this computer.
1635|ERROR_PATCH_PACKAGE_OPEN_FAILED|This update package could not be opened.
1636|ERROR_PATCH_PACKAGE_INVALID|This update package could not be opened.
1637|ERROR_PATCH_PACKAGE_UNSUPPORTED|This update package cannot be processed by the Windows Installer service.
1638|ERROR_PRODUCT_VERSION|Another version of this product is already installed.
1639|ERROR_INVALID_COMMAND_LINE|Invalid command line argument.
1640|ERROR_INSTALL_REMOTE_DISALLOWED|The Windows Installer does not permit installation from a Remote Desktop Connection.
1642|ERROR_PATCH_TARGET_NOT_FOUND|The Windows Installer service cannot update one or more protected Windows files.
1647|ERROR_UNKNOWN_PATCH|The update is not applied to this product.
1648|ERROR_PATCH_NO_SEQUENCE|No valid sequence could be found for the set of updates.
1650|ERROR_INVALID_PATCH_XML|The XML update data is invalid.
1652|ERROR_INSTALL_SERVICE_SAFEBOOT|The Windows Installer service is not accessible in Safe Mode.
1653|ERROR_FAIL_FAST_EXCEPTION|A fail fast exception occurred.
1654|ERROR_INSTALL_REJECTED|The app that you are trying to run is not supported on this version of Windows.
1816|ERROR_NOT_ENOUGH_QUOTA|Not enough quota is available to process this command.
1907|ERROR_PASSWORD_MUST_CHANGE|The user's password must be changed before signing in.
1908|ERROR_DOMAIN_CONTROLLER_NOT_FOUND|A domain controller with the specified name could not be found.
1909|ERROR_ACCOUNT_LOCKED_OUT|The referenced account is currently locked out and may not be logged on to.
6000|ERROR_ENCRYPTION_FAILED|The specified file could not be encrypted.
6001|ERROR_DECRYPTION_FAILED|The specified file could not be decrypted.
6002|ERROR_FILE_ENCRYPTED|The specified file is encrypted and the user does not have the ability to decrypt it.
6003|ERROR_NO_RECOVERY_POLICY|There is no valid encryption recovery policy configured for this system.
6004|ERROR_NO_EFS|The required encryption driver is not loaded for this system.
6006|ERROR_NO_USER_KEYS|There are no EFS keys defined for the user.
6007|ERROR_FILE_NOT_ENCRYPTED|The specified file is not encrypted.
6009|ERROR_FILE_READ_ONLY|The specified file is read only.
6010|ERROR_DIR_EFS_DISALLOWED|The directory has been disabled for encryption.
6015|ERROR_EFS_DISABLED|This machine is disabled for file encryption.
6016|ERROR_EFS_VERSION_NOT_SUPPORT|A newer system is required to decrypt this encrypted file.
8206|ERROR_DS_BUSY|The directory service is busy.
8207|ERROR_DS_UNAVAILABLE|The directory service is unavailable.
8208|ERROR_DS_NO_RIDS_ALLOCATED|The directory service was unable to allocate a relative identifier.
8209|ERROR_DS_NO_MORE_RIDS|The directory service has exhausted the pool of relative identifiers.
10004|WSAEINTR|Interrupted function call.
10009|WSAEBADF|File handle is not valid.
10013|WSAEACCES|Permission denied.
10014|WSAEFAULT|Bad address.
10022|WSAEINVAL|Invalid argument.
10024|WSAEMFILE|Too many open files.
10035|WSAEWOULDBLOCK|Resource temporarily unavailable.
10036|WSAEINPROGRESS|Operation now in progress.
10037|WSAEALREADY|Operation already in progress.
10038|WSAENOTSOCK|Socket operation on nonsocket.
10039|WSAEDESTADDRREQ|Destination address required.
10040|WSAEMSGSIZE|Message too long.
10041|WSAEPROTOTYPE|Protocol wrong type for socket.
10042|WSAENOPROTOOPT|Bad protocol option.
10043|WSAEPROTONOSUPPORT|Protocol not supported.
10044|WSAESOCKTNOSUPPORT|Socket type not supported.
10045|WSAEOPNOTSUPP|Operation not supported.
10046|WSAEPFNOSUPPORT|Protocol family not supported.
10047|WSAEAFNOSUPPORT|Address family not supported by protocol family.
10048|WSAEADDRINUSE|Address already in use.
10049|WSAEADDRNOTAVAIL|Cannot assign requested address.
10050|WSAENETDOWN|Network is down.
10051|WSAENETUNREACH|Network is unreachable.
10052|WSAENETRESET|Network dropped connection on reset.
10053|WSAECONNABORTED|Software caused connection abort.
10054|WSAECONNRESET|Connection reset by peer.
10055|WSAENOBUFS|No buffer space available.
10056|WSAEISCONN|Socket is already connected.
10057|WSAENOTCONN|Socket is not connected.
10058|WSAESHUTDOWN|Cannot send after socket shutdown.
10059|WSAETOOMANYREFS|Too many references.
10060|WSAETIMEDOUT|Connection timed out.
10061|WSAECONNREFUSED|Connection refused.
10062|WSAELOOP|Cannot translate name.
10063|WSAENAMETOOLONG|Name too long.
10064|WSAEHOSTDOWN|Host is down.
10065|WSAEHOSTUNREACH|No route to host.
10066|WSAENOTEMPTY|Directory not empty.
10067|WSAEPROCLIM|Too many processes.
10068|WSAEUSERS|User quota exceeded.
10069|WSAEDQUOT|Disk quota exceeded.
10070|WSAESTALE|Stale file handle reference.
10071|WSAEREMOTE|Item is remote.
10091|WSASYSNOTREADY|Network subsystem is unavailable.
10092|WSAVERNOTSUPPORTED|Winsock version not supported.
10093|WSANOTINITIALISED|Successful WSAStartup not yet performed.
10101|WSAEDISCON|Graceful shutdown in progress.
10102|WSAENOMORE|No more results.
10103|WSAECANCELLED|Call has been cancelled.
10104|WSAEINVALIDPROCTABLE|Procedure call table is invalid.
10105|WSAEINVALIDPROVIDER|Service provider is invalid.
10106|WSAEPROVIDERFAILEDINIT|Service provider failed to initialize.
10107|WSASYSCALLFAILURE|System call failure.
10108|WSASERVICE_NOT_FOUND|Service not found.
10109|WSATYPE_NOT_FOUND|Class type not found.
10110|WSA_E_NO_MORE|No more results.
10111|WSA_E_CANCELLED|Call was cancelled.
10112|WSAEREFUSED|Database query was refused.
11001|WSAHOST_NOT_FOUND|Host not found.
11002|WSATRY_AGAIN|Nonauthoritative host not found.
11003|WSANO_RECOVERY|This is a nonrecoverable error.
11004|WSANO_DATA|Valid name, no data record of requested type.
11005|WSA_QOS_RECEIVERS|QoS receivers.
11006|WSA_QOS_SENDERS|QoS senders.
11007|WSA_QOS_NO_SENDERS|QoS no senders.
11008|WSA_QOS_NO_RECEIVERS|QoS no receivers.
11009|WSA_QOS_REQUEST_CONFIRMED|QoS request confirmed.
11010|WSA_QOS_ADMISSION_FAILURE|QoS admission error.
11011|WSA_QOS_POLICY_FAILURE|QoS policy failure.
11012|WSA_QOS_BAD_STYLE|QoS bad style.
11013|WSA_QOS_BAD_OBJECT|QoS bad object.
11014|WSA_QOS_TRAFFIC_CTRL_ERROR|QoS traffic control error.
11015|WSA_QOS_GENERIC_ERROR|QoS generic error.
11016|WSA_QOS_ESERVICETYPE|QoS service type error.
11017|WSA_QOS_EFLOWSPEC|QoS flowspec error.
11018|WSA_QOS_EPROVSPECBUF|Invalid QoS provider buffer.
11019|WSA_QOS_EFILTERSTYLE|Invalid QoS filter style.
11020|WSA_QOS_EFILTERTYPE|Invalid QoS filter type.
11021|WSA_QOS_EFILTERCOUNT|Incorrect QoS filter count.
11022|WSA_QOS_EOBJLENGTH|Invalid QoS object length.
11023|WSA_QOS_EFLOWCOUNT|Incorrect QoS flow count.
11024|WSA_QOS_EUNKNOWNPSOBJ|Unrecognized QoS object.
11025|WSA_QOS_EPOLICYOBJ|Invalid QoS policy object.
11026|WSA_QOS_EFLOWDESC|Invalid QoS flow descriptor.
11027|WSA_QOS_EPSFLOWSPEC|Invalid QoS provider-specific flowspec.
11028|WSA_QOS_EPSFILTERSPEC|Invalid QoS provider-specific filterspec.
11029|WSA_QOS_ESDMODEOBJ|Invalid QoS shape discard mode object.
11030|WSA_QOS_ESHAPERATEOBJ|Invalid QoS shaping rate object.
11031|WSA_QOS_RESERVED_PETYPE|Reserved policy QoS element type.
11032|WSA_SECURE_HOST_NOT_FOUND|No such host is known securely.
11033|WSA_IPSEC_NAME_POLICY_ERROR|Name policy error.
""")
