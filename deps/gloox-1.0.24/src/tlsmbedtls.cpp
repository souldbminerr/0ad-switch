/*
  mbedTLS-backed TLS client for gloox. See tlsmbedtls.h.

  Implements gloox's TLSBase memory-BIO interface on mbedTLS 2.28: gloox feeds
  received ciphertext via decrypt(), and we hand bytes-to-send back through the
  TLSHandler's handleEncryptedData(). mbedTLS drives this via custom BIO callbacks
  over in-memory buffers (no socket of its own).

  This file follows the gloox license (see LICENSE in this distribution).
*/

#include "tlsmbedtls.h"

#ifdef HAVE_MBEDTLS

#include <algorithm>
#include <cstring>

#include <mbedtls/error.h>

namespace gloox
{

  MbedTLSClient::MbedTLSClient( TLSHandler* th, const std::string& server )
    : TLSBase( th, server ), m_haveCACerts( false ), m_haveClientCert( false ),
      m_handshakeDone( false )
  {
    mbedtls_ssl_init( &m_ssl );
    mbedtls_ssl_config_init( &m_conf );
    mbedtls_ctr_drbg_init( &m_ctrDrbg );
    mbedtls_entropy_init( &m_entropy );
    mbedtls_x509_crt_init( &m_caCerts );
    mbedtls_x509_crt_init( &m_clientCert );
    mbedtls_pk_init( &m_clientPk );
  }

  MbedTLSClient::~MbedTLSClient()
  {
    m_handler = 0;
    mbedtls_ssl_free( &m_ssl );
    mbedtls_ssl_config_free( &m_conf );
    mbedtls_ctr_drbg_free( &m_ctrDrbg );
    mbedtls_entropy_free( &m_entropy );
    mbedtls_x509_crt_free( &m_caCerts );
    mbedtls_x509_crt_free( &m_clientCert );
    mbedtls_pk_free( &m_clientPk );
  }

  bool MbedTLSClient::init( const std::string& clientKey,
                            const std::string& clientCerts,
                            const StringList& cacerts )
  {
    static const char* pers = "gloox-mbedtls";
    if( mbedtls_ctr_drbg_seed( &m_ctrDrbg, mbedtls_entropy_func, &m_entropy,
                               reinterpret_cast<const unsigned char*>( pers ),
                               std::strlen( pers ) ) != 0 )
      return false;

    if( mbedtls_ssl_config_defaults( &m_conf, MBEDTLS_SSL_IS_CLIENT,
                                     MBEDTLS_SSL_TRANSPORT_STREAM,
                                     MBEDTLS_SSL_PRESET_DEFAULT ) != 0 )
      return false;

    mbedtls_ssl_conf_rng( &m_conf, mbedtls_ctr_drbg_random, &m_ctrDrbg );

    // Parse CA certs (if any) and the optional client cert, then wire them into
    // the config. With CA certs we verify but don't abort on failure (VERIFY_OPTIONAL)
    // so gloox/0 A.D. can inspect the result in CertInfo; with none we can't verify.
    setCACerts( cacerts );
    setClientCert( clientKey, clientCerts );

    mbedtls_ssl_conf_ca_chain( &m_conf, &m_caCerts, 0 );
    mbedtls_ssl_conf_authmode( &m_conf,
      m_haveCACerts ? MBEDTLS_SSL_VERIFY_OPTIONAL : MBEDTLS_SSL_VERIFY_NONE );

    if( m_haveClientCert )
      mbedtls_ssl_conf_own_cert( &m_conf, &m_clientCert, &m_clientPk );

    if( mbedtls_ssl_setup( &m_ssl, &m_conf ) != 0 )
      return false;

    // SNI + certificate hostname verification.
    if( !m_server.empty() )
      mbedtls_ssl_set_hostname( &m_ssl, m_server.c_str() );

    mbedtls_ssl_set_bio( &m_ssl, this, &MbedTLSClient::sendCB, &MbedTLSClient::recvCB, 0 );

    m_valid = true;
    return true;
  }

  void MbedTLSClient::setCACerts( const StringList& cacerts )
  {
    m_cacerts = cacerts;
    for( StringList::const_iterator it = cacerts.begin(); it != cacerts.end(); ++it )
    {
      if( mbedtls_x509_crt_parse_file( &m_caCerts, (*it).c_str() ) == 0 )
        m_haveCACerts = true;
    }
  }

  void MbedTLSClient::setClientCert( const std::string& clientKey, const std::string& clientCerts )
  {
    m_clientKey = clientKey;
    m_clientCerts = clientCerts;

    if( clientKey.empty() || clientCerts.empty() )
      return;

    if( mbedtls_x509_crt_parse_file( &m_clientCert, clientCerts.c_str() ) == 0 &&
        mbedtls_pk_parse_keyfile( &m_clientPk, clientKey.c_str(), 0 ) == 0 )
      m_haveClientCert = true;
  }

  int MbedTLSClient::sendCB( void* ctx, const unsigned char* buf, size_t len )
  {
    MbedTLSClient* self = static_cast<MbedTLSClient*>( ctx );
    if( self->m_handler )
      self->m_handler->handleEncryptedData( self,
        std::string( reinterpret_cast<const char*>( buf ), len ) );
    return static_cast<int>( len );
  }

  int MbedTLSClient::recvCB( void* ctx, unsigned char* buf, size_t len )
  {
    MbedTLSClient* self = static_cast<MbedTLSClient*>( ctx );
    if( self->m_recvBuffer.empty() )
      return MBEDTLS_ERR_SSL_WANT_READ;

    size_t n = std::min( len, self->m_recvBuffer.size() );
    std::memcpy( buf, self->m_recvBuffer.data(), n );
    self->m_recvBuffer.erase( 0, n );
    return static_cast<int>( n );
  }

  bool MbedTLSClient::pumpHandshake()
  {
    if( m_handshakeDone )
      return true;

    int ret = mbedtls_ssl_handshake( &m_ssl );
    if( ret == 0 )
    {
      m_secure = true;
      m_handshakeDone = true;
      fetchCertInfo();
      if( m_handler )
        m_handler->handleHandshakeResult( this, true, m_certInfo );
      return true;
    }

    if( ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE )
      return true; // need more data from the wire; output already pushed via sendCB

    // Hard failure.
    if( m_handler )
      m_handler->handleHandshakeResult( this, false, m_certInfo );
    return false;
  }

  bool MbedTLSClient::handshake()
  {
    return pumpHandshake();
  }

  bool MbedTLSClient::encrypt( const std::string& data )
  {
    m_sendBuffer += data;

    if( !m_secure )
    {
      pumpHandshake();
      return true;
    }

    while( !m_sendBuffer.empty() )
    {
      int ret = mbedtls_ssl_write( &m_ssl,
        reinterpret_cast<const unsigned char*>( m_sendBuffer.data() ),
        m_sendBuffer.size() );
      if( ret > 0 )
        m_sendBuffer.erase( 0, ret );
      else if( ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE )
        break;
      else
        break; // error; connection will be torn down by the upper layer
    }
    return true;
  }

  int MbedTLSClient::decrypt( const std::string& data )
  {
    m_recvBuffer += data;

    if( !m_secure )
    {
      pumpHandshake();
      return static_cast<int>( data.length() );
    }

    unsigned char buf[4096];
    for( ;; )
    {
      int ret = mbedtls_ssl_read( &m_ssl, buf, sizeof( buf ) );
      if( ret > 0 )
      {
        if( m_handler )
          m_handler->handleDecryptedData( this,
            std::string( reinterpret_cast<char*>( buf ), ret ) );
      }
      else
        break; // WANT_READ (no more buffered), close-notify, or error
    }
    return static_cast<int>( data.length() );
  }

  void MbedTLSClient::fetchCertInfo()
  {
    m_certInfo = CertInfo();

    uint32_t flags = mbedtls_ssl_get_verify_result( &m_ssl );
    if( flags == 0 )
      m_certInfo.status = CertOk;
    else
    {
      m_certInfo.status = CertInvalid;
      if( flags & MBEDTLS_X509_BADCERT_EXPIRED )    m_certInfo.status |= CertExpired;
      if( flags & MBEDTLS_X509_BADCERT_FUTURE )     m_certInfo.status |= CertNotActive;
      if( flags & MBEDTLS_X509_BADCERT_CN_MISMATCH ) m_certInfo.status |= CertWrongPeer;
      if( flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED ) m_certInfo.status |= CertSignerUnknown;
    }

    const char* cipher = mbedtls_ssl_get_ciphersuite( &m_ssl );
    if( cipher )
      m_certInfo.cipher = cipher;
    const char* proto = mbedtls_ssl_get_version( &m_ssl );
    if( proto )
      m_certInfo.protocol = proto;

    const mbedtls_x509_crt* peer = mbedtls_ssl_get_peer_cert( &m_ssl );
    if( peer )
    {
      char cn[256];
      if( mbedtls_x509_dn_gets( cn, sizeof( cn ), &peer->subject ) > 0 )
        m_certInfo.server = cn;
      if( mbedtls_x509_dn_gets( cn, sizeof( cn ), &peer->issuer ) > 0 )
        m_certInfo.issuer = cn;
    }
  }

  void MbedTLSClient::cleanup()
  {
    m_secure = false;
    m_valid = false;
    m_handshakeDone = false;
    m_recvBuffer.clear();
    m_sendBuffer.clear();
    mbedtls_ssl_session_reset( &m_ssl );
  }

}

#endif // HAVE_MBEDTLS
