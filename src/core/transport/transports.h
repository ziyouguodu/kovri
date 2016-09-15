/**                                                                                           //
 * Copyright (c) 2013-2016, The Kovri I2P Router Project                                      //
 *                                                                                            //
 * All rights reserved.                                                                       //
 *                                                                                            //
 * Redistribution and use in source and binary forms, with or without modification, are       //
 * permitted provided that the following conditions are met:                                  //
 *                                                                                            //
 * 1. Redistributions of source code must retain the above copyright notice, this list of     //
 *    conditions and the following disclaimer.                                                //
 *                                                                                            //
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list     //
 *    of conditions and the following disclaimer in the documentation and/or other            //
 *    materials provided with the distribution.                                               //
 *                                                                                            //
 * 3. Neither the name of the copyright holder nor the names of its contributors may be       //
 *    used to endorse or promote products derived from this software without specific         //
 *    prior written permission.                                                               //
 *                                                                                            //
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY        //
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF    //
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL     //
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,       //
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,               //
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS    //
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,          //
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF    //
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.               //
 *                                                                                            //
 * Parts of the project are originally copyright (c) 2013-2015 The PurpleI2P Project          //
 */

#ifndef SRC_CORE_TRANSPORT_TRANSPORTS_H_
#define SRC_CORE_TRANSPORT_TRANSPORTS_H_

#include <boost/asio.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "i2np_protocol.h"
#include "identity.h"
#include "ntcp.h"
#include "ntcp_session.h"
#include "router_info.h"
#include "ssu.h"
#include "transport_session.h"

#ifdef USE_UPNP
#include "upnp.h"
#endif

namespace i2p {
namespace transport {

/// @class DHKeysPairSupplier
/// @brief Pregenerates Diffie-Hellman key pairs for use in key exchange
/// TODO(unassigned): Does this really need to run on a separate thread?
class DHKeysPairSupplier {
 public:
  DHKeysPairSupplier(
      std::size_t size);

  ~DHKeysPairSupplier();

  void Start();

  void Stop();

  std::unique_ptr<DHKeysPair> Acquire();

  void Return(
      std::unique_ptr<DHKeysPair> pair);

 private:
  void Run();

  void CreateDHKeysPairs(
      std::size_t num);

 private:
  const std::size_t m_QueueSize;
  bool m_IsRunning;
  std::queue<std::unique_ptr<DHKeysPair>> m_Queue;
  std::unique_ptr<std::thread> m_Thread;
  std::condition_variable m_Acquired;
  std::mutex m_AcquiredMutex;
};

/// @class Peer
/// @brief Stores information about transport peers.
struct Peer {
  std::size_t num_attempts;
  std::shared_ptr<const i2p::data::RouterInfo> router;
  std::list<std::shared_ptr<TransportSession>> sessions;
  std::uint64_t creation_time;
  std::vector<std::shared_ptr<i2p::I2NPMessage>> delayed_messages;

  void Done();
};

const std::size_t SESSION_CREATION_TIMEOUT = 10;  // in seconds
const std::uint32_t LOW_BANDWIDTH_LIMIT = 32 * 1024;  // 32KBps

/// @class Transports
/// @brief Provides functions to pass messages to a given peer.
///        Manages the SSU and NTCP transports.
class Transports {
 public:
  Transports();

  ~Transports();

  /// @brief Starts SSU and NTCP server instances, as well as the cleanup timer.
  ///        If enabled, the UPnP service is also started.
  void Start();

  /// @brief Stops all services ran by this Transports object.
  void Stop();

  /// @return a reference to the boost::asio::io_serivce that is used by this
  ///         Transports object
  boost::asio::io_service& GetService() {
    return m_Service;
  }

  /// @return a pointer to a Diffie-Hellman pair
  std::unique_ptr<i2p::transport::DHKeysPair> GetNextDHKeysPair();

  /// @brief Returns a keypair for reuse.
  // TODO(unassigned): Do not reuse ephemeral keys, the whole point is that they
  //                   are not reused.
  void ReuseDHKeysPair(
      std::unique_ptr<DHKeysPair> pair);

  /// @brief Asynchronously sends a message to a peer.
  /// @param ident the router hash of the remote peer
  /// @param msg the I2NP message to deliver
  void SendMessage(
      const i2p::data::IdentHash& ident,
      std::shared_ptr<i2p::I2NPMessage> msg);

  /// @brief Asynchronously sends one or more messages to a peer.
  /// @param ident the router hash of the remote peer
  /// @param msgs the I2NP messages to deliver
  void SendMessages(
      const i2p::data::IdentHash& ident,
      const std::vector<std::shared_ptr<i2p::I2NPMessage>>& msgs);

  /// @brief Asynchronously close all transport sessions to the given router.
  /// @param router the i2p::data::RouterInfo of the router to disconnect from
  /// @note if router is nullptr, nothing happens
  void CloseSession(
      std::shared_ptr<const i2p::data::RouterInfo> router);

  /// @brief Informs this Transports object that a new peer has connected
  ///        to us
  /// @param session the new session
  void PeerConnected(
      std::shared_ptr<TransportSession> session);

  /// @brief Informs this Transports object that a peer has disconnected
  ///        from us
  /// @param session the session that has ended
  void PeerDisconnected(
      std::shared_ptr<TransportSession> session);

  bool IsConnected(
      const i2p::data::IdentHash& ident) const;

  void UpdateSentBytes(
      std::uint64_t numBytes) {
    m_TotalSentBytes += numBytes;
  }

  void UpdateReceivedBytes(
      std::uint64_t numBytes) {
    m_TotalReceivedBytes += numBytes;
  }

  std::uint64_t GetTotalSentBytes() const {
    return m_TotalSentBytes;
  }

  std::uint64_t GetTotalReceivedBytes() const {
    return m_TotalReceivedBytes;
  }

  // bytes per second
  std::uint32_t GetInBandwidth() const {
    return m_InBandwidth;
  }

  // bytes per second
  std::uint32_t GetOutBandwidth() const {
    return m_OutBandwidth;
  }

  bool IsBandwidthExceeded() const;

  std::size_t GetNumPeers() const {
    return m_Peers.size();
  }

  std::shared_ptr<const i2p::data::RouterInfo> GetRandomPeer() const;

  /// @return Log-formatted string of session info
  std::string GetFormattedSessionInfo(
      std::shared_ptr<const i2p::data::RouterInfo>& router) const;

 private:
  void Run();

  void RequestComplete(
      std::shared_ptr<const i2p::data::RouterInfo> router,
      const i2p::data::IdentHash& ident);

  void HandleRequestComplete(
      std::shared_ptr<const i2p::data::RouterInfo> router,
      const i2p::data::IdentHash& ident);

  void PostMessages(
      i2p::data::IdentHash ident,
      std::vector<std::shared_ptr<i2p::I2NPMessage>> msgs);

  void PostCloseSession(
      std::shared_ptr<const i2p::data::RouterInfo> router);

  bool ConnectToPeer(
      const i2p::data::IdentHash& ident, Peer& peer);

  bool ConnectToPeerNTCP(
      const i2p::data::IdentHash& ident, Peer& peer);

  bool ConnectToPeerSSU(Peer& peer);

  void HandlePeerCleanupTimer(
      const boost::system::error_code& ecode);

  void NTCPResolve(
      const std::string& addr,
      const i2p::data::IdentHash& ident);

  void HandleNTCPResolve(
      const boost::system::error_code& ecode,
      boost::asio::ip::tcp::resolver::iterator it,
      i2p::data::IdentHash ident,
      std::shared_ptr<boost::asio::ip::tcp::resolver> resolver);

  void UpdateBandwidth();

  void DetectExternalIP();

 private:
  bool m_IsRunning;

  std::unique_ptr<std::thread> m_Thread;
  boost::asio::io_service m_Service;
  boost::asio::io_service::work m_Work;
  boost::asio::deadline_timer m_PeerCleanupTimer;

  std::unique_ptr<NTCPServer> m_NTCPServer;
  std::unique_ptr<SSUServer> m_SSUServer;

  std::map<i2p::data::IdentHash, Peer> m_Peers;

  DHKeysPairSupplier m_DHKeysPairSupplier;

  std::atomic<uint64_t> m_TotalSentBytes, m_TotalReceivedBytes;

  std::uint32_t m_InBandwidth, m_OutBandwidth;
  std::uint64_t m_LastInBandwidthUpdateBytes, m_LastOutBandwidthUpdateBytes;
  std::uint64_t m_LastBandwidthUpdateTime;

#ifdef USE_UPNP
  UPnP m_UPnP;
#endif

 public:
  const decltype(m_Peers)& GetPeers() const {
    return m_Peers;
  }
};

extern Transports transports;

}  // namespace transport
}  // namespace i2p

#endif  // SRC_CORE_TRANSPORT_TRANSPORTS_H_

