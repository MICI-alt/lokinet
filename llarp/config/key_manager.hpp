#ifndef LLARP_KEY_MANAGER_HPP
#define LLARP_KEY_MANAGER_HPP

#include <config/config.hpp>
#include <crypto/types.hpp>
#include <router_contact.hpp>

namespace llarp
{

  /// KeyManager manages the cryptographic keys stored on disk for the local node.
  /// This includes private keys as well as the self-signed router contact file
  /// (e.g. "self.signed").
  ///
  /// Keys are either read from disk if they exist and are valid (see below) or are
  /// generated and written to disk.
  /// 
  /// In addition, the KeyManager detects when the keys obsolete (e.g. as a result
  /// of a software upgrade) and backs up existing keys before writing out new ones.

  struct KeyManager {

    /// Constructor
    ///
    /// @param config should be a prepared config object
    KeyManager(const llarp::Config& config);

    /// Initializes from disk. This reads enough from disk to understand the current
    /// state of the stored keys.
    ///
    /// NOTE: Must be called prior to obtaining any keys.
    ///
    /// @param genIfAbsent determines whether or not we will create files if they
    ///        do not exist.
    /// @return true on success, false otherwise
    bool
    initializeFromDisk(bool genIfAbsent);

    /// Obtain the identity key (e.g. ~/.lokinet/identity.private)
    ///
    /// @param key (out) will be modified to contain the identity key
    /// @return true on success, false otherwise
    bool
    getIdentityKey(llarp::SecretKey &key) const;

    /// Obtain the encryption key (e.g. ~/.lokinet/encryption.private)
    ///
    /// @param key (out) will be modified to contain the encryption key
    /// @return true on success, false otherwise
    bool
    getEncryptionKey(llarp::SecretKey &key) const;

    /// Obtain the transport key (e.g. ~/.lokinet/transport.private)
    ///
    /// @param key (out) will be modified to contain the transport key
    /// @return true on success, false otherwise
    bool
    getTransportKey(llarp::SecretKey &key) const;

    /// Obtain the self-signed RouterContact
    ///
    /// @param rc (out) will be modified to contian the RouterContact
    /// @return true on success, false otherwise
    bool
    getRouterContact(llarp::RouterContact& rc) const;

  private:

    std::string m_rcPath;
    std::string m_snKeyPath;
    std::string m_idKeyPath;
    std::string m_encKeyPath;
    std::string m_transportKeyPath;
  };

}  // namespace llarp

#endif
