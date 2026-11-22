#include "tls.h"

#include <windows.h>
#include <wincrypt.h>

#define SECURITY_WIN32 1
#include <security.h>
#include <schannel.h>
#include <sspi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#include "socket.h"

namespace vd::remote::net {
namespace {

constexpr std::size_t kTokenCapacity = 64 * 1024;
constexpr std::size_t kHeaderReserve = 1024;
constexpr std::size_t kTrailerReserve = 1024;
constexpr std::size_t kMaxTlsRecord = 16 * 1024;

std::wstring widen(const std::string& s) {
  if (s.empty()) return std::wstring();
  int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(need), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), need);
  return out;
}

/* One process wide Schannel credential. AcquireCredentialsHandle is expensive
   (it loads the TLS provider) and the handle is safe to share across threads. */
std::mutex g_cred_mutex;
CredHandle g_cred{};
bool g_cred_valid = false;

CredHandle& client_credential() {
  std::lock_guard<std::mutex> lock(g_cred_mutex);
  if (!g_cred_valid) {
    SCHANNEL_CRED cred{};
    cred.dwVersion = SCHANNEL_CRED_VERSION;
    /* We validate the chain ourselves so the policy (host name, usage) is
       explicit; SCH_USE_STRONG_CRYPTO keeps 3DES/RC4 out of the offer. */
    cred.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS |
                   SCH_USE_STRONG_CRYPTO;
    cred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
#ifdef SP_PROT_TLS1_3_CLIENT
    cred.grbitEnabledProtocols |= SP_PROT_TLS1_3_CLIENT;
#endif
    cred.dwMinimumCipherStrength = 128;
    TimeStamp expiry{};
    SECURITY_STATUS sc =
        AcquireCredentialsHandleW(nullptr, const_cast<wchar_t*>(UNISP_NAME_W),
                                  SECPKG_CRED_OUTBOUND, nullptr, &cred, nullptr, nullptr, &g_cred,
                                  &expiry);
    if (sc != SEC_E_OK) {
      throw Failure(1, "tls credential failed", sspi_error_text(sc));
    }
    g_cred_valid = true;
  }
  return g_cred;
}

/* SEC_APPLICATION_PROTOCOLS blob for ALPN: one length prefixed protocol name. */
std::vector<char> build_alpn(const std::string& protocol) {
  std::vector<char> blob(sizeof(SEC_APPLICATION_PROTOCOLS) + protocol.size(), 0);
  auto* head = reinterpret_cast<SEC_APPLICATION_PROTOCOLS*>(blob.data());
  head->ProtocolListsSize =
      static_cast<ULONG>(sizeof(SEC_APPLICATION_PROTOCOL_LIST) + protocol.size());
  auto* list = reinterpret_cast<SEC_APPLICATION_PROTOCOL_LIST*>(head->ProtocolLists);
  /* SecApplicationProtocolNegotiationExt_ALPN; spelled numerically because the
     mingw sspi.h enum is not usable in a preprocessor test. */
  list->ProtoNegoExt = static_cast<SEC_APPLICATION_PROTOCOL_NEGOTIATION_EXT>(2);
  list->ProtocolListSize = static_cast<USHORT>(protocol.size() + 1);
  list->ProtocolList[0] = static_cast<BYTE>(protocol.size());
  std::memcpy(&list->ProtocolList[1], protocol.data(), protocol.size());
  return blob;
}

}  // namespace

std::string sspi_error_text(long status) {
  char text[64];
  std::snprintf(text, sizeof(text), "0x%08lX", static_cast<unsigned long>(status));
  switch (status) {
    case SEC_E_UNSUPPORTED_FUNCTION:
      return std::string(text) + " SEC_E_UNSUPPORTED_FUNCTION";
    case SEC_E_INTERNAL_ERROR:
      return std::string(text) + " SEC_E_INTERNAL_ERROR";
    case SEC_E_INVALID_TOKEN:
      return std::string(text) + " SEC_E_INVALID_TOKEN";
    case SEC_E_INVALID_HANDLE:
      return std::string(text) + " SEC_E_INVALID_HANDLE";
    case SEC_E_INCOMPLETE_MESSAGE:
      return std::string(text) + " SEC_E_INCOMPLETE_MESSAGE";
    case SEC_E_ILLEGAL_MESSAGE:
      return std::string(text) + " SEC_E_ILLEGAL_MESSAGE";
    case SEC_E_CERT_UNKNOWN:
      return std::string(text) + " SEC_E_CERT_UNKNOWN";
    case SEC_E_CERT_EXPIRED:
      return std::string(text) + " SEC_E_CERT_EXPIRED";
    case SEC_E_WRONG_PRINCIPAL:
      return std::string(text) + " SEC_E_WRONG_PRINCIPAL";
    case SEC_E_ALGORITHM_MISMATCH:
      return std::string(text) + " SEC_E_ALGORITHM_MISMATCH";
    default:
      return std::string(text) + " " + socket_error_text(static_cast<int>(status));
  }
}

TlsStream::TlsStream(SOCKET sock, std::string host, TlsOptions options)
    : sock_(sock), host_(std::move(host)), options_(std::move(options)) {
  ctx_ = new CtxtHandle();
  std::memset(ctx_, 0, sizeof(CtxtHandle));
}

TlsStream::~TlsStream() {
  if (ctx_) {
    auto* handle = static_cast<CtxtHandle*>(ctx_);
    if (handle->dwLower || handle->dwUpper) DeleteSecurityContext(handle);
    delete handle;
    ctx_ = nullptr;
  }
  close_socket(sock_);
  sock_ = INVALID_SOCKET;
}

void TlsStream::handshake() {
  if (handshaken_) return;
  handshake_step(nullptr, 0);
  handshaken_ = true;
  if (!options_.insecure_skip_verify) verify_peer();
  /* The stream sizes are per cipher suite; hardcoding them would be a guess. */
  SecPkgContext_StreamSizes sizes{};
  SECURITY_STATUS sc = QueryContextAttributes(static_cast<CtxtHandle*>(ctx_),
                                              SECPKG_ATTR_STREAM_SIZES, &sizes);
  if (sc == SEC_E_OK) {
    header_size_ = sizes.cbHeader;
    trailer_size_ = sizes.cbTrailer;
    max_message_ = sizes.cbMaximumMessage;
  }
}

void TlsStream::handshake_step(const void* initial_data, std::size_t initial_len) {
  std::wstring whost = widen(host_);
  std::vector<char> alpn;
  if (!options_.alpn.empty()) alpn = build_alpn(options_.alpn);
  std::string input;
  if (initial_data && initial_len) input.assign(static_cast<const char*>(initial_data), initial_len);
  bool first_call = initial_data == nullptr && initial_len == 0;
  /* INV_TLS_DEBUG=1 traces the handshake flight by flight; it is a diagnostic
     switch for field problems, not part of the normal path. */
  const bool trace = std::getenv("INV_TLS_DEBUG") != nullptr;
  int step = 0;

  for (;;) {
    /* The client speaks first: wait for bytes only after the first call has
       produced a ClientHello, otherwise both sides block and the peer drops
       the connection. */
    if (!first_call && input.empty()) {
      char tmp[kMaxTlsRecord];
      std::size_t n = recv_some(sock_, tmp, sizeof(tmp), options_.handshake_timeout_ms);
      if (n == 0) {
        throw Failure(1, "tls handshake failed",
                      "peer closed the connection during the TLS handshake (12030)");
      }
      input.assign(tmp, n);
    }

    SecBuffer in_bufs[2];
    std::memset(in_bufs, 0, sizeof(in_bufs));
    SecBufferDesc in_desc{};
    in_desc.ulVersion = SECBUFFER_VERSION;
    SecBufferDesc* in_desc_ptr = nullptr;
    if (first_call) {
      if (!alpn.empty()) {
        in_bufs[0].BufferType = SECBUFFER_APPLICATION_PROTOCOLS;
        in_bufs[0].pvBuffer = alpn.data();
        in_bufs[0].cbBuffer = static_cast<unsigned long>(alpn.size());
        in_desc.cBuffers = 1;
        in_desc.pBuffers = in_bufs;
        in_desc_ptr = &in_desc;
      }
    } else {
      in_bufs[0].BufferType = SECBUFFER_TOKEN;
      in_bufs[0].pvBuffer = input.data();
      in_bufs[0].cbBuffer = static_cast<unsigned long>(input.size());
      in_bufs[1].BufferType = SECBUFFER_EMPTY;
      in_desc.cBuffers = 2;
      in_desc.pBuffers = in_bufs;
      in_desc_ptr = &in_desc;
    }

    std::vector<char> token(kTokenCapacity);
    SecBuffer out_buf{};
    out_buf.BufferType = SECBUFFER_TOKEN;
    out_buf.pvBuffer = token.data();
    out_buf.cbBuffer = static_cast<unsigned long>(token.size());
    SecBufferDesc out_desc{};
    out_desc.ulVersion = SECBUFFER_VERSION;
    out_desc.cBuffers = 1;
    out_desc.pBuffers = &out_buf;

    unsigned long attrs = 0;
    TimeStamp expiry{};
    auto* ctx = static_cast<CtxtHandle*>(ctx_);
    SECURITY_STATUS sc = InitializeSecurityContextW(
        &client_credential(), first_call ? nullptr : ctx,
        const_cast<wchar_t*>(whost.c_str()),
        ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
            ISC_REQ_EXTENDED_ERROR | ISC_REQ_STREAM | ISC_REQ_MANUAL_CRED_VALIDATION,
        0, SECURITY_NATIVE_DREP, in_desc_ptr, 0, ctx, &out_desc, &attrs, &expiry);

    if (trace) {
      std::fprintf(stderr, "tls %s step=%d first=%d in=%zu sc=0x%08lX out=%lu\n", host_.c_str(),
                   step, first_call ? 1 : 0, input.size(),
                   static_cast<unsigned long>(sc), static_cast<unsigned long>(out_buf.cbBuffer));
      ++step;
    }
    if (out_buf.cbBuffer > 0 && out_buf.pvBuffer) {
      send_all(sock_, out_buf.pvBuffer, out_buf.cbBuffer, options_.handshake_timeout_ms,
               "tls handshake write " + host_);
    }

    if (sc == SEC_E_INCOMPLETE_MESSAGE) {
      char tmp[kMaxTlsRecord];
      std::size_t n = recv_some(sock_, tmp, sizeof(tmp), options_.handshake_timeout_ms);
      if (n == 0) {
        throw Failure(1, "tls handshake failed",
                      "peer closed the connection during the TLS handshake (12030)");
      }
      input.append(tmp, n);
      continue;
    }

    /* Bytes the SSP did not consume stay in the input buffer; after the
       handshake they are the first TLS records and must be decrypted later. */
    std::string leftover;
    if (in_desc_ptr && in_desc.cBuffers > 1 && in_bufs[1].BufferType == SECBUFFER_EXTRA &&
        in_bufs[1].cbBuffer) {
      leftover.assign(input.data() + input.size() - in_bufs[1].cbBuffer, in_bufs[1].cbBuffer);
    }

    if (sc == SEC_E_OK) {
      pending_ = leftover;
      return;
    }
    if (sc == SEC_I_CONTINUE_NEEDED) {
      input = leftover;
      first_call = false;
      continue;
    }
    if (sc == SEC_I_INCOMPLETE_CREDENTIALS) {
      throw Failure(1, "tls handshake failed",
                    "server requested a client certificate, which inv does not provide (12175)");
    }
    if (sc == SEC_E_INVALID_TOKEN || sc == SEC_E_ILLEGAL_MESSAGE ||
        sc == SEC_E_UNSUPPORTED_FUNCTION || sc == SEC_E_ALGORITHM_MISMATCH) {
      throw Failure(1, "tls handshake failed", sspi_error_text(sc) + " (12175)");
    }
    throw Failure(1, "tls handshake failed", sspi_error_text(sc));
  }
}

void TlsStream::verify_peer() {
  auto* ctx = static_cast<CtxtHandle*>(ctx_);
  PCCERT_CONTEXT cert = nullptr;
  SECURITY_STATUS sc =
      QueryContextAttributes(ctx, SECPKG_ATTR_REMOTE_CERT_CONTEXT, static_cast<void*>(&cert));
  if (sc != SEC_E_OK || !cert) {
    throw Failure(1, "tls certificate check failed",
                  "no peer certificate available " + sspi_error_text(sc) + " (12175)");
  }

  CERT_CHAIN_PARA para{};
  para.cbSize = sizeof(para);
  const CERT_CHAIN_CONTEXT* chain = nullptr;
  DWORD flags = CERT_CHAIN_CACHE_END_CERT | CERT_CHAIN_REVOCATION_CHECK_CACHE_ONLY;
  BOOL got_chain = CertGetCertificateChain(nullptr, cert, nullptr, cert->hCertStore, &para, flags,
                                           nullptr, &chain);
  if (!got_chain || !chain) {
    CertFreeCertificateContext(cert);
    throw Failure(1, "tls certificate check failed",
                  "could not build a certificate chain (12175)");
  }

  std::wstring whost = widen(host_);
  SSL_EXTRA_CERT_CHAIN_POLICY_PARA extra{};
  extra.cbSize = sizeof(extra);
  extra.dwAuthType = AUTHTYPE_SERVER;
  extra.fdwChecks = 0;
  extra.pwszServerName = const_cast<wchar_t*>(whost.c_str());

  CERT_CHAIN_POLICY_PARA policy{};
  policy.cbSize = sizeof(policy);
  policy.dwFlags = 0;
  policy.pvExtraPolicyPara = &extra;

  CERT_CHAIN_POLICY_STATUS status{};
  status.cbSize = sizeof(status);
  BOOL ok = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &status);
  CertFreeCertificateChain(chain);
  CertFreeCertificateContext(cert);
  if (!ok || status.dwError != 0) {
    throw Failure(1, "tls certificate check failed",
                  "peer certificate rejected for \"" + host_ + "\": policy error " +
                      std::to_string(static_cast<unsigned long>(status.dwError)) +
                      " chain error " + std::to_string(static_cast<unsigned long>(status.lChainIndex)) +
                      " (12175)");
  }
}

void TlsStream::read_raw(int timeout_ms) {
  char tmp[kMaxTlsRecord];
  std::size_t n = recv_some(sock_, tmp, sizeof(tmp), timeout_ms, "tls record read " + host_);
  if (n == 0) {
    closed_ = true;
    return;
  }
  pending_.append(tmp, n);
}

void TlsStream::write(const void* data, std::size_t len, int timeout_ms) {
  if (closed_) throw Failure(1, "tls write failed", "stream is already closed (12030)");
  auto* ctx = static_cast<CtxtHandle*>(ctx_);
  const char* p = static_cast<const char*>(data);
  std::size_t sent = 0;
  while (sent < len) {
    std::size_t chunk = std::min<std::size_t>(len - sent, max_message_);
    std::vector<char> buffer(header_size_ + chunk + trailer_size_, 0);
    std::memcpy(buffer.data() + header_size_, p + sent, chunk);

    SecBuffer bufs[3];
    std::memset(bufs, 0, sizeof(bufs));
    bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
    bufs[0].pvBuffer = buffer.data();
    bufs[0].cbBuffer = static_cast<unsigned long>(header_size_);
    bufs[1].BufferType = SECBUFFER_DATA;
    bufs[1].pvBuffer = buffer.data() + header_size_;
    bufs[1].cbBuffer = static_cast<unsigned long>(chunk);
    bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
    bufs[2].pvBuffer = buffer.data() + header_size_ + chunk;
    bufs[2].cbBuffer = static_cast<unsigned long>(trailer_size_);
    SecBufferDesc desc{};
    desc.ulVersion = SECBUFFER_VERSION;
    desc.cBuffers = 3;
    desc.pBuffers = bufs;

    SECURITY_STATUS sc = EncryptMessage(ctx, 0, &desc, 0);
    if (sc != SEC_E_OK) {
      throw Failure(1, "tls write failed",
                    sspi_error_text(sc) + " (chunk=" + std::to_string(chunk) + " header=" +
                        std::to_string(header_size_) + " trailer=" +
                        std::to_string(trailer_size_) + " max=" +
                        std::to_string(max_message_) + ")");
    }
    std::size_t total = static_cast<std::size_t>(bufs[0].cbBuffer) + bufs[1].cbBuffer +
                        bufs[2].cbBuffer;
    send_all(sock_, buffer.data(), total, timeout_ms, "tls record write " + host_);
    sent += chunk;
  }
}

std::size_t TlsStream::read(void* buf, std::size_t len, int timeout_ms) {
  if (len == 0) return 0;
  auto* ctx = static_cast<CtxtHandle*>(ctx_);
  for (;;) {
    if (!plain_.empty()) {
      std::size_t n = std::min<std::size_t>(len, plain_.size());
      std::memcpy(buf, plain_.data(), n);
      plain_.erase(0, n);
      return n;
    }
    if (closed_) return 0;
    if (pending_.empty()) {
      read_raw(timeout_ms);
      if (closed_) return 0;
    }

    SecBuffer bufs[4];
    std::memset(bufs, 0, sizeof(bufs));
    bufs[0].BufferType = SECBUFFER_DATA;
    bufs[0].pvBuffer = pending_.data();
    bufs[0].cbBuffer = static_cast<unsigned long>(pending_.size());
    bufs[1].BufferType = SECBUFFER_EMPTY;
    bufs[2].BufferType = SECBUFFER_EMPTY;
    bufs[3].BufferType = SECBUFFER_EMPTY;
    SecBufferDesc desc{};
    desc.ulVersion = SECBUFFER_VERSION;
    desc.cBuffers = 4;
    desc.pBuffers = bufs;

    unsigned long qop = 0;
    SECURITY_STATUS sc = DecryptMessage(ctx, &desc, 0, &qop);
    if (sc == SEC_E_INCOMPLETE_MESSAGE) {
      read_raw(timeout_ms);
      if (closed_) return 0;
      continue;
    }
    if (sc == SEC_I_CONTEXT_EXPIRED) {
      closed_ = true;
      return 0;
    }
    if (sc == SEC_I_RENEGOTIATE) {
      std::string extra;
      for (int i = 0; i < 4; ++i) {
        if (bufs[i].BufferType == SECBUFFER_EXTRA && bufs[i].cbBuffer) {
          extra.assign(pending_.data() + pending_.size() - bufs[i].cbBuffer, bufs[i].cbBuffer);
        }
      }
      pending_.clear();
      handshake_step(extra.data(), extra.size());
      if (!options_.insecure_skip_verify) verify_peer();
      continue;
    }
    if (sc != SEC_E_OK) throw Failure(1, "tls read failed", sspi_error_text(sc));

    std::string extra;
    for (int i = 0; i < 4; ++i) {
      if (bufs[i].BufferType == SECBUFFER_DATA && bufs[i].cbBuffer && bufs[i].pvBuffer) {
        plain_.append(static_cast<const char*>(bufs[i].pvBuffer), bufs[i].cbBuffer);
      } else if (bufs[i].BufferType == SECBUFFER_EXTRA && bufs[i].cbBuffer) {
        extra.assign(pending_.data() + pending_.size() - bufs[i].cbBuffer, bufs[i].cbBuffer);
      }
    }
    pending_ = extra;
    /* No application data in this record (for example a post handshake
       NewSessionTicket); loop and decrypt the next one. */
  }
}

void TlsStream::shutdown() {
  if (closed_) return;
  closed_ = true;
  if (sock_ != INVALID_SOCKET) {
    ::shutdown(sock_, SD_SEND);
  }
}

}  // namespace vd::remote::net
