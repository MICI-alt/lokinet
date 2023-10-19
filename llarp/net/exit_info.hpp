#pragma once

#include <llarp/crypto/types.hpp>
#include <llarp/util/bencode.hpp>
#include <iosfwd>

#include "ip_address.hpp"

/**
 * exit_info.h
 *
 * utilities for handling exits on the llarp network
 */

/// Exit info model
namespace llarp
{
  /// deprecated don't use me , this is only for backwards compat
  struct ExitInfo
  {
    IpAddress ipAddress;
    IpAddress netmask;
    PubKey pubkey;
    uint64_t version = llarp::constants::proto_version;

    ExitInfo() = default;

    ExitInfo(const PubKey& pk, const IpAddress& address) : ipAddress(address), pubkey(pk)
    {}

    bool
    bt_encode(llarp_buffer_t* buf) const;

    bool
    BDecode(llarp_buffer_t* buf)
    {
      return bencode_decode_dict(*this, buf);
    }

    bool
    decode_key(const llarp_buffer_t& k, llarp_buffer_t* buf);

    std::string
    ToString() const;
  };

  template <>
  constexpr inline bool IsToStringFormattable<ExitInfo> = true;

}  // namespace llarp
