/*
  mbedTLS-backed TLS client for gloox.

  Added for the 0 A.D. Nintendo Switch port: the Switch only ships mbedTLS (no
  GnuTLS/OpenSSL), which upstream gloox does not support. This implements gloox's
  TLSBase memory-BIO interface on top of mbedTLS 2.28 so the multiplayer lobby can
  use a TLS connection. Client (VerifyingClient) only.

  This file follows the gloox license (see LICENSE in this distribution).
*/

#ifndef TLSMBEDTLS_H__
#define TLSMBEDTLS_H__

#include "tlsbase.h"

#ifdef HAVE_MBEDTLS

#include <string>

#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>

namespace gloox
{

  /**
   * @brief This class implements a TLS client backend using mbedTLS.
   */
  class MbedTLSClient : public TLSBase
  {
    public:
      MbedTLSClient( TLSHandler* th, const std::string& server );
      virtual ~MbedTLSClient();

      virtual bool init( const std::string& clientKey = EmptyString,
                         const std::string& clientCerts = EmptyString,
                         const StringList& cacerts = StringList() );

      virtual bool encrypt( const std::string& data );
      virtual int decrypt( const std::string& data );
      virtual void cleanup();
      virtual bool handshake();

      virtual bool hasChannelBinding() const { return false; }

      virtual void setCACerts( const StringList& cacerts );
      virtual void setClientCert( const std::string& clientKey, const std::string& clientCerts );

    private:
      // mbedTLS BIO callbacks. ctx is the MbedTLSClient instance. send pushes
      // ciphertext to the TLSHandler (to go over the wire); recv pulls ciphertext
      // out of m_recvBuffer (fed by decrypt()), returning WANT_READ when empty.
      static int sendCB( void* ctx, const unsigned char* buf, size_t len );
      static int recvCB( void* ctx, unsigned char* buf, size_t len );

      // Run the handshake state machine over whatever input is buffered; reports
      // the result to the handler when it completes or fails.
      bool pumpHandshake();
      // Fill m_certInfo from the established session.
      void fetchCertInfo();

      mbedtls_ssl_context m_ssl;
      mbedtls_ssl_config m_conf;
      mbedtls_ctr_drbg_context m_ctrDrbg;
      mbedtls_entropy_context m_entropy;
      mbedtls_x509_crt m_caCerts;
      mbedtls_x509_crt m_clientCert;
      mbedtls_pk_context m_clientPk;

      std::string m_recvBuffer; // ciphertext received from the network
      std::string m_sendBuffer; // plaintext waiting to be encrypted

      bool m_haveCACerts;
      bool m_haveClientCert;
      bool m_handshakeDone;
  };

}

#endif // HAVE_MBEDTLS

#endif // TLSMBEDTLS_H__
