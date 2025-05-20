/*
 *
 *    Copyright (c) 2025 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *      This file implements a WiFiPAF endpoint abstraction for CHIP over WiFiPAF (CHIPoPAF)
 *      Public Action Frame Transport Protocol (PAFTP).
 *
 */

#include "WiFiPAFEndPoint.h"

#include <cstdint>
#include <cstring>
#include <utility>

#include <lib/support/BitFlags.h>
#include <lib/support/BufferReader.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <system/SystemClock.h>
#include <system/SystemLayer.h>
#include <system/SystemPacketBuffer.h>

#include "WiFiPAFConfig.h"
#include "WiFiPAFError.h"
#include "WiFiPAFLayer.h"
#include "WiFiPAFTP.h"

// Define direct constant for ACK timeout override - use for retransmission logic
#define PAFTP_ACK_TIMEOUT_MS_OVERRIDE (2000) // 2 seconds to allow for retransmission attempt

// Define below to enable extremely verbose, WiFiPAF end point-specific debug logging.
#define CHIP_WIFIPAF_END_POINT_DEBUG_LOGGING_ENABLED
#define CHIP_WIFIPAF_END_POINT_DEBUG_LOGGING_LEVEL 1

#ifdef CHIP_WIFIPAF_END_POINT_DEBUG_LOGGING_ENABLED
#define ChipLogDebugWiFiPAFEndPoint_L0(MOD, MSG, ...) ChipLogDetail(MOD, MSG, ##__VA_ARGS__)
#if (CHIP_WIFIPAF_END_POINT_DEBUG_LOGGING_LEVEL == 0)
#define ChipLogDebugWiFiPAFEndPoint(MOD, MSG, ...)
#else
#define ChipLogDebugWiFiPAFEndPoint(MOD, MSG, ...) ChipLogDetail(MOD, MSG, ##__VA_ARGS__)
#endif // CHIP_WIFIPAF_END_POINT_DEBUG_LOGGING_LEVEL
#define ChipLogDebugBufferWiFiPAFEndPoint(MOD, BUF)                                                                                \
    ChipLogByteSpan(MOD, ByteSpan((BUF)->Start(), ((BUF)->DataLength() < 8 ? (BUF)->DataLength() : 8u)))
#else
#define ChipLogDebugWiFiPAFEndPoint(MOD, MSG, ...)
#define ChipLogDebugBufferWiFiPAFEndPoint(MOD, BUF)
#endif

/**
 *  @def WIFIPAF_CONFIG_IMMEDIATE_ACK_WINDOW_THRESHOLD
 *
 *  @brief
 *    If an end point's receive window drops equal to or below this value, it will send an immediate acknowledgement
 *    packet to re-open its window instead of waiting for the send-ack timer to expire.
 *
 */
#define WIFIPAF_CONFIG_IMMEDIATE_ACK_WINDOW_THRESHOLD 1

#define WIFIPAF_ACK_SEND_TIMEOUT_MS 2500
#define WIFIPAF_WAIT_RES_TIMEOUT_MS 1000
// Drop the connection if network resources remain unavailable for the period.
// Known condition: If the remote side is awaiting an ACK packet, the wait time must not exceed PAFTP_ACK_TIMEOUT_MS.
#define WIFIPAF_MAX_RESOURCE_BLOCK_COUNT (PAFTP_ACK_TIMEOUT_MS / WIFIPAF_WAIT_RES_TIMEOUT_MS)

/**
 *  @def WIFIPAF_WINDOW_NO_ACK_SEND_THRESHOLD
 *
 *  @brief
 *    Data fragments may only be sent without piggybacked acks if receiver's window size is above this threshold.
 *
 */
#define WIFIPAF_WINDOW_NO_ACK_SEND_THRESHOLD 1

/**
 *  @def PAFTP_RETRANSMIT_TIMEOUT_MS
 *
 *  @brief
 *    The amount of time, in milliseconds, to wait before retransmitting a packet
 *    that hasn't been acknowledged.
 *
 */
#define PAFTP_RETRANSMIT_TIMEOUT_MS (2000) // 1 second per retransmission attempt

namespace chip {
namespace WiFiPAF {

CHIP_ERROR WiFiPAFEndPoint::StartConnect()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    PAFTransportCapabilitiesRequestMessage req;
    PacketBufferHandle buf;
    constexpr uint8_t numVersions =
        CHIP_PAF_TRANSPORT_PROTOCOL_MAX_SUPPORTED_VERSION - CHIP_PAF_TRANSPORT_PROTOCOL_MIN_SUPPORTED_VERSION + 1;
    static_assert(numVersions <= NUM_PAFTP_SUPPORTED_PROTOCOL_VERSIONS, "Incompatibly protocol versions");

    // Ensure we're in the correct state.
    VerifyOrExit(mState == kState_Ready, err = CHIP_ERROR_INCORRECT_STATE);
    mState = kState_Connecting;

    // Build PAF transport protocol capabilities request.
    buf = System::PacketBufferHandle::New(System::PacketBuffer::kMaxSize);
    VerifyOrExit(!buf.IsNull(), err = CHIP_ERROR_NO_MEMORY);

    // Zero-initialize PAF transport capabilities request.
    memset(&req, 0, sizeof(req));
    req.mMtu        = CHIP_PAF_DEFAULT_MTU;
    req.mWindowSize = PAF_MAX_RECEIVE_WINDOW_SIZE;

    // Populate request with highest supported protocol versions
    for (uint8_t i = 0; i < numVersions; i++)
    {
        req.SetSupportedProtocolVersion(i, static_cast<uint8_t>(CHIP_PAF_TRANSPORT_PROTOCOL_MAX_SUPPORTED_VERSION - i));
    }

    err = req.Encode(buf);
    SuccessOrExit(err);

    // Start connect timer. Canceled when end point freed or connection established.
    err = StartConnectTimer();
    SuccessOrExit(err);

    // Send PAF transport capabilities request to peripheral.
    // Add reference to message fragment. CHIP retains partial ownership of message fragment's packet buffer,
    // since this is the same buffer as that of the whole message, just with a fragmenter-modified payload offset
    // and data length, by a Retain() on the handle when calling this function.
    err = SendWrite(buf.Retain());
    SuccessOrExit(err);
    // Free request buffer on write confirmation. Stash a reference to it in mSendQueue, which we don't use anyway
    // until the connection has been set up.
    QueueTx(std::move(buf), kType_Data);

exit:
    // If we failed to initiate the connection, close the end point.
    if (err != CHIP_NO_ERROR)
    {
        StopConnectTimer();
        DoClose(kWiFiPAFCloseFlag_AbortTransmission, err);
    }

    return err;
}

CHIP_ERROR WiFiPAFEndPoint::HandleConnectComplete()
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    mState = kState_Connected;
    // Cancel the connect timer.
    StopConnectTimer();

    // We've successfully completed the PAF transport protocol handshake, so let the application know we're open for business.
    if (mWiFiPafLayer != nullptr)
    {
        // Indicate connect complete to next-higher layer.
        mWiFiPafLayer->OnEndPointConnectComplete(this, CHIP_NO_ERROR);
    }
    else
    {
        // If no connect complete callback has been set up, close the end point.
        err = WIFIPAF_ERROR_NO_CONNECT_COMPLETE_CALLBACK;
    }
    return err;
}

bool WiFiPAFEndPoint::IsConnected(uint8_t state) const
{
    return (state == kState_Connected || state == kState_Closing);
}

void WiFiPAFEndPoint::DoClose(uint8_t flags, CHIP_ERROR err)
{
    uint8_t oldState = mState;

    // If end point is not closed or closing, OR end point was closing gracefully, but tx abort has been specified...
    if ((mState != kState_Closed && mState != kState_Closing) ||
        (mState == kState_Closing && (flags & kWiFiPAFCloseFlag_AbortTransmission)))
    {
        // Cancel Connect and ReceiveConnect timers if they are running.
        // Check role first to avoid needless iteration over timer pool.
        if (mRole == kWiFiPafRole_Subscriber)
        {
            StopConnectTimer();
        }

        // Free the packets in re-order queue if ones exist
        for (uint8_t qidx = 0; qidx < PAFTP_REORDER_QUEUE_SIZE; qidx++)
        {
            if (ReorderQueue[qidx] != nullptr)
            {
                ReorderQueue[qidx] = nullptr;
                ItemsInReorderQueue--;
            }
        }

        // If transmit buffer is empty or a transmission abort was specified...
        if (mPafTP.TxState() == WiFiPAFTP::kState_Idle || (flags & kWiFiPAFCloseFlag_AbortTransmission))
        {
            FinalizeClose(oldState, flags, err);
        }
        else
        {
            // Wait for send queue and fragmenter's tx buffer to become empty, to ensure all pending messages have been
            // sent. Only free end point and tell platform it can throw away the underlying connection once all
            // pending messages have been sent and acknowledged by the remote CHIPoPAF stack, or once the remote stack
            // closes the CHIPoPAF connection.
            //
            // In so doing, WiFiPAFEndPoint attempts to emulate the level of reliability afforded by TCPEndPoint and TCP
            // sockets in general with a typical default SO_LINGER option. That said, there is no hard guarantee that
            // pending messages will be sent once (Do)Close() is called, so developers should use application-level
            // messages to confirm the receipt of all data sent prior to a Close() call.
            mState = kState_Closing;

            if ((flags & kWiFiPAFCloseFlag_SuppressCallback) == 0)
            {
                DoCloseCallback(oldState, flags, err);
            }
        }
    }
}

void WiFiPAFEndPoint::FinalizeClose(uint8_t oldState, uint8_t flags, CHIP_ERROR err)
{
    mState = kState_Closed;

    // Ensure transmit queue is empty and set to NULL.
    mSendQueue = nullptr;
    // Clear the session information
    ChipLogProgress(WiFiPAF, "Shutdown PAF session (%" PRIu32 ", %u)", mSessionInfo.id, mSessionInfo.role);
    mWiFiPafLayer->mWiFiPAFTransport->WiFiPAFCloseSession(mSessionInfo);
    memset(&mSessionInfo, 0, sizeof(mSessionInfo));
    // Fire application's close callback if we haven't already, and it's not suppressed.
    if (oldState != kState_Closing && (flags & kWiFiPAFCloseFlag_SuppressCallback) == 0)
    {
        DoCloseCallback(oldState, flags, err);
    }

    // If underlying WiFiPAF connection has closed, connection object is invalid, so just free the end point and return.
    if (err == WIFIPAF_ERROR_REMOTE_DEVICE_DISCONNECTED || err == WIFIPAF_ERROR_APP_CLOSED_CONNECTION)
    {
        Free();
    }
    else // Otherwise, try to signal close to remote device before end point releases WiFiPAF connection and frees itself.
    {
        if (mRole == kWiFiPafRole_Subscriber)
        {
            // Cancel send and receive-ack timers, if running.
            StopAckReceivedTimer();
            StopSendAckTimer();
            StopWaitResourceTimer();
            mConnStateFlags.Set(ConnectionStateFlag::kOperationInFlight);
        }
        else
        {
            Free();
        }
    }
    ClearAll();
}

void WiFiPAFEndPoint::DoCloseCallback(uint8_t state, uint8_t flags, CHIP_ERROR err)
{
    // Callback fires once per end point lifetime.
    mOnPafSubscribeComplete = nullptr;
    mOnPafSubscribeError    = nullptr;
    OnConnectionClosed      = nullptr;
}

void WiFiPAFEndPoint::Free()
{
    // Clear fragmentation and reassembly engine's Tx and Rx buffers. Counters will be reset by next engine init.
    FreePAFtpEngine();

    // Clear pending ack buffer, if any.
    mAckToSend = nullptr;
    
    // Clear saved last tx packet
    mLastTxPacket = nullptr;

    // Cancel all timers.
    StopConnectTimer();
    StopAckReceivedTimer();
    StopSendAckTimer();
    StopWaitResourceTimer();

    // Clear callbacks.
    mOnPafSubscribeComplete = nullptr;
    mOnPafSubscribeError    = nullptr;
    OnMessageReceived       = nullptr;
    OnConnectionClosed      = nullptr;
}

void WiFiPAFEndPoint::FreePAFtpEngine()
{
    // Free transmit disassembly buffer
    mPafTP.ClearTxPacket();

    // Free receive reassembly buffer
    mPafTP.ClearRxPacket();
}

CHIP_ERROR WiFiPAFEndPoint::Init(WiFiPAFLayer * WiFiPafLayer, WiFiPAFSession & SessionInfo)
{
    // Fail if already initialized.
    VerifyOrReturnError(mWiFiPafLayer == nullptr, CHIP_ERROR_INCORRECT_STATE);

    // Validate args.
    VerifyOrReturnError(WiFiPafLayer != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    // If end point plays subscriber role, expect ack as last step of PAFTP handshake.
    // If being publisher, subscriber's handshake indication 'ack's write sent by publisher to kick off the PAFTP handshake.
    bool expectInitialAck = (SessionInfo.role == kWiFiPafRole_Publisher);

    CHIP_ERROR err = mPafTP.Init(this, expectInitialAck);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(WiFiPAF, "WiFiPAFTP init failed");
        return err;
    }

    mWiFiPafLayer = WiFiPafLayer;

    // WiFiPAF EndPoint data members:
    memcpy(&mSessionInfo, &SessionInfo, sizeof(mSessionInfo));
    mRole = SessionInfo.role;
    mTimerStateFlags.ClearAll();
    mLocalReceiveWindowSize  = 0;
    mRemoteReceiveWindowSize = 0;
    mReceiveWindowMaxSize    = 0;
    mSendQueue               = nullptr;
    mAckToSend               = nullptr;
    mLastTxPacket            = nullptr;  // Initialize last tx packet
    mRetransmissionCount     = 0;        // Initialize retransmission counter

    ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "initialized local rx window, size = %u", mLocalReceiveWindowSize);

    // End point is ready.
    mState = kState_Ready;

    return CHIP_NO_ERROR;
}

CHIP_ERROR WiFiPAFEndPoint::SendCharacteristic(PacketBufferHandle && buf)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    SuccessOrExit(err = SendWrite(std::move(buf)));
    // Write succeeded, so shrink remote receive window counter by 1.
    mRemoteReceiveWindowSize = static_cast<SequenceNumber_t>(mRemoteReceiveWindowSize - 1);
    ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "decremented remote rx window, new size = %u", mRemoteReceiveWindowSize);
exit:
    return err;
}

/*
 *  Routine to queue the Tx packet with a packet type
 *  kType_Data(0)       - data packet
 *  kType_Control(1)    - control packet
 */
void WiFiPAFEndPoint::QueueTx(PacketBufferHandle && data, PacketType_t type)
{
    if (mSendQueue.IsNull())
    {
        mSendQueue = std::move(data);
        ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "%s: Set data as new mSendQueue %p, type %d", __FUNCTION__, mSendQueue->Start(), type);
    }
    else
    {
        mSendQueue->AddToEnd(std::move(data));
        ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "%s: Append data to mSendQueue %p, type %d", __FUNCTION__, mSendQueue->Start(), type);
    }
}

CHIP_ERROR WiFiPAFEndPoint::Send(PacketBufferHandle && data)
{
    CHIP_ERROR err = CHIP_NO_ERROR;

    VerifyOrExit(!data.IsNull(), err = CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrExit(IsConnected(mState), err = CHIP_ERROR_INCORRECT_STATE);

    // Ensure outgoing message fits in a single contiguous packet buffer, as currently required by the
    // message fragmentation and reassembly engine.
    if (data->HasChainedBuffer())
    {
        data->CompactHead();

        if (data->HasChainedBuffer())
        {
            err = CHIP_ERROR_OUTBOUND_MESSAGE_TOO_BIG;
            ExitNow();
        }
    }

    // Add new message to send queue.
    QueueTx(std::move(data), kType_Data);

    // Send first fragment of new message, if we can.
    err = DriveSending();
    SuccessOrExit(err);
exit:
    if (err != CHIP_NO_ERROR)
    {
        DoClose(kWiFiPAFCloseFlag_AbortTransmission, err);
    }

    return err;
}

bool WiFiPAFEndPoint::PrepareNextFragment(PacketBufferHandle && data, bool & sentAck)
{
    // If we have a pending fragment acknowledgement to send, piggyback it on the fragment we're about to transmit.
    if (mTimerStateFlags.Has(TimerStateFlag::kSendAckTimerRunning))
    {
        // Reset local receive window counter.
        mLocalReceiveWindowSize = mReceiveWindowMaxSize;
        ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "reset local rx window on piggyback ack tx, size = %u", mLocalReceiveWindowSize);

        // Tell caller AND fragmenter we have an ack to piggyback.
        sentAck = true;
    }
    else
    {
        // No ack to piggyback.
        sentAck = false;
    }

    return mPafTP.HandleCharacteristicSend(std::move(data), sentAck);
}

CHIP_ERROR WiFiPAFEndPoint::SendNextMessage()
{
    // Get the first queued packet to send
    PacketBufferHandle data = mSendQueue.PopHead();

    // Hand whole message payload to the fragmenter.
    bool sentAck;
    VerifyOrReturnError(PrepareNextFragment(std::move(data), sentAck), WIFIPAF_ERROR_CHIPPAF_PROTOCOL_ABORT);

    ReturnErrorOnFailure(SendCharacteristic(mPafTP.BorrowTxPacket()));

    if (sentAck)
    {
        // If sent piggybacked ack, stop send-ack timer.
        StopSendAckTimer();
    }

    // Start ack received timer, if it's not already running.
    return StartAckReceivedTimer();
}

CHIP_ERROR WiFiPAFEndPoint::ContinueMessageSend()
{
    bool sentAck;

    if (!PrepareNextFragment(nullptr, sentAck))
    {
        // Log PAFTP error
        ChipLogError(WiFiPAF, "paftp fragmenter error on send!");
        mPafTP.LogState();

        return WIFIPAF_ERROR_CHIPPAF_PROTOCOL_ABORT;
    }

    ReturnErrorOnFailure(SendCharacteristic(mPafTP.BorrowTxPacket()));

    if (sentAck)
    {
        // If sent piggybacked ack, stop send-ack timer.
        StopSendAckTimer();
    }

    // Start ack received timer, if it's not already running.
    return StartAckReceivedTimer();
}

CHIP_ERROR WiFiPAFEndPoint::HandleHandshakeConfirmationReceived()
{
    // Free capabilities request/response payload.
    mSendQueue.FreeHead();

    return CHIP_NO_ERROR;
}

CHIP_ERROR WiFiPAFEndPoint::HandleFragmentConfirmationReceived(bool result)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    // Ensure we're in correct state to receive confirmation of non-handshake GATT send.
    VerifyOrExit(IsConnected(mState), err = CHIP_ERROR_INCORRECT_STATE);

    if (mConnStateFlags.Has(ConnectionStateFlag::kStandAloneAckInFlight))
    {
        // If confirmation was received for stand-alone ack, free its tx buffer.
        mAckToSend = nullptr;
        mConnStateFlags.Clear(ConnectionStateFlag::kStandAloneAckInFlight);
    }

    // Log ACK received with result
    ChipLogDetail(WiFiPAF, "WiFiPAF RECEIVED ACK: result=%s, session_id=%" PRIu32 ", peer_id=%" PRIu32 ", peer_mac=%02x:%02x:%02x:%02x:%02x:%02x, seq=%u",
                   result ? "SUCCESS" : "FAILURE", 
                   mSessionInfo.id, 
                   mSessionInfo.peer_id,
                   mSessionInfo.peer_addr[0], mSessionInfo.peer_addr[1], mSessionInfo.peer_addr[2],
                   mSessionInfo.peer_addr[3], mSessionInfo.peer_addr[4], mSessionInfo.peer_addr[5],
                   mPafTP.GetNewestUnackedSentSequenceNumber());

    // Stop the retransmit timer as we've received an acknowledgment
    StopRetransmitTimer();

    if (result != true)
    {
        // Something wrong in writing packets
        ChipLogError(WiFiPAF, "Failed to send PAF packet");
        err = CHIP_ERROR_SENDING_BLOCKED;
        StopAckReceivedTimer();
        SuccessOrExit(err);
    }

    // If local receive window size has shrunk to or below immediate ack threshold, AND a message fragment is not
    // pending on which to piggyback an ack, send immediate stand-alone ack.
    //
    // This check covers the case where the local receive window has shrunk between transmission and confirmation of
    // the stand-alone ack, and also the case where a window size < the immediate ack threshold was detected in
    // Receive(), but the stand-alone ack was deferred due to a pending outbound message fragment.
    if (mLocalReceiveWindowSize <= WIFIPAF_CONFIG_IMMEDIATE_ACK_WINDOW_THRESHOLD && mSendQueue.IsNull() &&
        mPafTP.TxState() != WiFiPAFTP::kState_InProgress)
    {
        err = DriveStandAloneAck(); // Encode stand-alone ack and drive sending.
        SuccessOrExit(err);
    }
    else
    {
        err = DriveSending();
        SuccessOrExit(err);
    }

exit:
    if (err != CHIP_NO_ERROR)
    {
        DoClose(kWiFiPAFCloseFlag_AbortTransmission, err);
    }

    return err;
}

CHIP_ERROR WiFiPAFEndPoint::HandleSendConfirmationReceived(bool result)
{
    // Mark outstanding operation as finished.
    mConnStateFlags.Clear(ConnectionStateFlag::kOperationInFlight);

    // Log ACK received at the send confirmation level
    ChipLogDetail(WiFiPAF, "WiFiPAF SEND CONFIRMATION: result=%s, handshake=%s, session_id=%" PRIu32 ", peer_id=%" PRIu32 ", peer_mac=%02x:%02x:%02x:%02x:%02x:%02x, seq=%u",
                   result ? "SUCCESS" : "FAILURE", 
                   !mConnStateFlags.Has(ConnectionStateFlag::kCapabilitiesConfReceived) ? "YES" : "NO",
                   mSessionInfo.id, 
                   mSessionInfo.peer_id,
                   mSessionInfo.peer_addr[0], mSessionInfo.peer_addr[1], mSessionInfo.peer_addr[2],
                   mSessionInfo.peer_addr[3], mSessionInfo.peer_addr[4], mSessionInfo.peer_addr[5],
                   mPafTP.GetNewestUnackedSentSequenceNumber());

    // If confirmation was for outbound portion of PAFTP connect handshake...
    if (!mConnStateFlags.Has(ConnectionStateFlag::kCapabilitiesConfReceived))
    {
        mConnStateFlags.Set(ConnectionStateFlag::kCapabilitiesConfReceived);
        return HandleHandshakeConfirmationReceived();
    }

    return HandleFragmentConfirmationReceived(result);
}

CHIP_ERROR WiFiPAFEndPoint::DriveStandAloneAck()
{
    // Stop send-ack timer if running.
    StopSendAckTimer();

    // If stand-alone ack not already pending, allocate new payload buffer here.
    if (mAckToSend.IsNull())
    {
        mAckToSend = System::PacketBufferHandle::New(kTransferProtocolStandaloneAckHeaderSize);
        VerifyOrReturnError(!mAckToSend.IsNull(), CHIP_ERROR_NO_MEMORY);
    }

    // Attempt to send stand-alone ack.
    return DriveSending();
}

CHIP_ERROR WiFiPAFEndPoint::DoSendStandAloneAck()
{
    ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "sending stand-alone ack");

    // Encode and transmit stand-alone ack.
    mPafTP.EncodeStandAloneAck(mAckToSend);
    ReturnErrorOnFailure(SendCharacteristic(mAckToSend.Retain()));

    // Reset local receive window counter.
    mLocalReceiveWindowSize = mReceiveWindowMaxSize;
    ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "reset local rx window on stand-alone ack tx, size = %u", mLocalReceiveWindowSize);

    mConnStateFlags.Set(ConnectionStateFlag::kStandAloneAckInFlight);

    // Start ack received timer, if it's not already running.
    return StartAckReceivedTimer();
}

CHIP_ERROR WiFiPAFEndPoint::DriveSending()
{
    // If receiver's window is almost closed and we don't have an ack to send, OR we do have an ack to send but
    // receiver's window is completely empty, OR another operation is in flight, awaiting confirmation...
    if ((mRemoteReceiveWindowSize <= WIFIPAF_WINDOW_NO_ACK_SEND_THRESHOLD &&
         !mTimerStateFlags.Has(TimerStateFlag::kSendAckTimerRunning) && mAckToSend.IsNull()) ||
        (mRemoteReceiveWindowSize == 0) || (mConnStateFlags.Has(ConnectionStateFlag::kOperationInFlight)))
    {
        if (mRemoteReceiveWindowSize <= WIFIPAF_WINDOW_NO_ACK_SEND_THRESHOLD &&
            !mTimerStateFlags.Has(TimerStateFlag::kSendAckTimerRunning) && mAckToSend.IsNull())
        {
            ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "NO SEND: receive window almost closed, and no ack to send");
        }

        if (mRemoteReceiveWindowSize == 0)
        {
            ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "NO SEND: remote receive window closed");
        }

        if (mConnStateFlags.Has(ConnectionStateFlag::kOperationInFlight))
        {
            ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "NO SEND: Operation in flight");
        }
        // Can't send anything.
        return CHIP_NO_ERROR;
    }

    if (!mWiFiPafLayer->mWiFiPAFTransport->WiFiPAFResourceAvailable())
    {
        // Resource is currently unavailable, send packets later
        StartWaitResourceTimer();
        return CHIP_NO_ERROR;
    }
    mResourceWaitCount = 0;

    // Otherwise, let's see what we can send.
    if ((!mAckToSend.IsNull()) && !mConnStateFlags.Has(ConnectionStateFlag::kStandAloneAckInFlight))
    {
        // If immediate, stand-alone ack is pending, send it.
        ChipLogProgress(WiFiPAF, "Send the pending stand-alone ack");
        ReturnErrorOnFailure(DoSendStandAloneAck());
    }
    else if (mPafTP.TxState() == WiFiPAFTP::kState_Idle) // Else send next message fragment, if any.
    {
        // Fragmenter's idle, let's see what's in the send queue...
        if (!mSendQueue.IsNull())
        {
            // Transmit first fragment of next whole message in send queue.
            ReturnErrorOnFailure(SendNextMessage());
        }
        else
        {
            // Nothing to send!
            ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "=> No pending packets, nothing to send!");
        }
    }
    else if (mPafTP.TxState() == WiFiPAFTP::kState_InProgress)
    {
        // Send next fragment of message currently held by fragmenter.
        ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "Send the next fragment");
        ReturnErrorOnFailure(ContinueMessageSend());
    }
    else if (mPafTP.TxState() == WiFiPAFTP::kState_Complete)
    {
        // Clear fragmenter's pointer to sent message buffer and reset its Tx state.
        // Buffer will be freed at scope exit.
        PacketBufferHandle sentBuf = mPafTP.TakeTxPacket();

        if (!mSendQueue.IsNull())
        {
            ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "Send the next pkt");
            // Transmit first fragment of next whole message in send queue.
            ReturnErrorOnFailure(SendNextMessage());
        }
        else if (mState == kState_Closing && !mPafTP.ExpectingAck()) // and mSendQueue is NULL, per above...
        {
            ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "Closing and no expect ack!");
            // If end point closing, got last ack, and got out-of-order confirmation for last send, finalize close.
            FinalizeClose(mState, kWiFiPAFCloseFlag_SuppressCallback, CHIP_NO_ERROR);
        }
        else
        {
            // Nothing to send!
            ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "No more packets to send");
        }
    }
    else
    {
        ChipLogError(WiFiPAF, "Unknown TxState: %u", mPafTP.TxState());
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR WiFiPAFEndPoint::HandleCapabilitiesRequestReceived(PacketBufferHandle && data)
{
    PAFTransportCapabilitiesRequestMessage req;
    PAFTransportCapabilitiesResponseMessage resp;
    uint16_t mtu;

    VerifyOrReturnError(!data.IsNull(), CHIP_ERROR_INVALID_ARGUMENT);

    mState = kState_Connecting;

    // Decode PAFTP capabilities request.
    ReturnErrorOnFailure(PAFTransportCapabilitiesRequestMessage::Decode(data, req));

    PacketBufferHandle responseBuf = System::PacketBufferHandle::New(kCapabilitiesResponseLength);
    VerifyOrReturnError(!responseBuf.IsNull(), CHIP_ERROR_NO_MEMORY);

    if (req.mMtu > 0) // If MTU was observed and provided by central...
    {
        mtu = req.mMtu; // Accept central's observation of the MTU.
    }
    else
    {
        mtu = CHIP_PAF_DEFAULT_MTU;
    }

    // Select fragment size for connection based on MTU.
    resp.mFragmentSize = std::min(static_cast<uint16_t>(mtu), WiFiPAFTP::sMaxFragmentSize);

    // Select local and remote max receive window size based on local resources available for both incoming writes
    mRemoteReceiveWindowSize = mLocalReceiveWindowSize = mReceiveWindowMaxSize =
        std::min(req.mWindowSize, static_cast<uint8_t>(PAF_MAX_RECEIVE_WINDOW_SIZE));
    resp.mWindowSize = mReceiveWindowMaxSize;
    ChipLogProgress(WiFiPAF, "local and remote recv window sizes = %u", resp.mWindowSize);

    // Select PAF transport protocol version from those supported by central, or none if no supported version found.
    resp.mSelectedProtocolVersion = WiFiPAFLayer::GetHighestSupportedProtocolVersion(req);
    ChipLogProgress(WiFiPAF, "selected PAFTP version %d", resp.mSelectedProtocolVersion);

    if (resp.mSelectedProtocolVersion == kWiFiPAFTransportProtocolVersion_None)
    {
        // If WiFiPAF transport protocol versions incompatible, prepare to close connection after capabilities response
        // has been sent.
        ChipLogError(WiFiPAF, "incompatible PAFTP versions; peripheral expected between %d and %d",
                     CHIP_PAF_TRANSPORT_PROTOCOL_MIN_SUPPORTED_VERSION, CHIP_PAF_TRANSPORT_PROTOCOL_MAX_SUPPORTED_VERSION);
        mState = kState_Aborting;
    }
    else
    {
        // Set Rx and Tx fragment sizes to the same value
        mPafTP.SetRxFragmentSize(resp.mFragmentSize);
        mPafTP.SetTxFragmentSize(resp.mFragmentSize);
    }

    ChipLogProgress(WiFiPAF, "using PAFTP fragment sizes rx %d / tx %d.", mPafTP.GetRxFragmentSize(), mPafTP.GetTxFragmentSize());
    ReturnErrorOnFailure(resp.Encode(responseBuf));

    CHIP_ERROR err;
    err = SendWrite(responseBuf.Retain());
    SuccessOrExit(err);

    // Stash capabilities response payload
    QueueTx(std::move(responseBuf), kType_Data);

    // Response has been sent
    return HandleConnectComplete();
exit:
    return err;
}

CHIP_ERROR WiFiPAFEndPoint::HandleCapabilitiesResponseReceived(PacketBufferHandle && data)
{
    PAFTransportCapabilitiesResponseMessage resp;

    VerifyOrReturnError(!data.IsNull(), CHIP_ERROR_INVALID_ARGUMENT);

    // Decode PAFTP capabilities response.
    ReturnErrorOnFailure(PAFTransportCapabilitiesResponseMessage::Decode(data, resp));

    VerifyOrReturnError(resp.mFragmentSize > 0, WIFIPAF_ERROR_INVALID_FRAGMENT_SIZE);

    ChipLogProgress(WiFiPAF, "Publisher chose PAFTP version %d; subscriber expected between %d and %d",
                    resp.mSelectedProtocolVersion, CHIP_PAF_TRANSPORT_PROTOCOL_MIN_SUPPORTED_VERSION,
                    CHIP_PAF_TRANSPORT_PROTOCOL_MAX_SUPPORTED_VERSION);

    if ((resp.mSelectedProtocolVersion < CHIP_PAF_TRANSPORT_PROTOCOL_MIN_SUPPORTED_VERSION) ||
        (resp.mSelectedProtocolVersion > CHIP_PAF_TRANSPORT_PROTOCOL_MAX_SUPPORTED_VERSION))
    {
        return WIFIPAF_ERROR_INCOMPATIBLE_PROTOCOL_VERSIONS;
    }

    // Set fragment size as minimum of (reported ATT MTU, BTP characteristic size)
    resp.mFragmentSize = std::min(resp.mFragmentSize, WiFiPAFTP::sMaxFragmentSize);

    mPafTP.SetRxFragmentSize(resp.mFragmentSize);
    mPafTP.SetTxFragmentSize(resp.mFragmentSize);

    ChipLogProgress(WiFiPAF, "using PAFTP fragment sizes rx %d / tx %d.", mPafTP.GetRxFragmentSize(), mPafTP.GetTxFragmentSize());

    // Select local and remote max receive window size based on local resources available for both incoming indications
    mRemoteReceiveWindowSize = mLocalReceiveWindowSize = mReceiveWindowMaxSize = resp.mWindowSize;

    ChipLogProgress(WiFiPAF, "local and remote recv window size = %u", resp.mWindowSize);

    // Shrink local receive window counter by 1, since connect handshake indication requires acknowledgement.
    mLocalReceiveWindowSize = static_cast<SequenceNumber_t>(mLocalReceiveWindowSize - 1);
    ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "decremented local rx window, new size = %u", mLocalReceiveWindowSize);

    // Send ack for connection handshake indication when timer expires. Sequence numbers always start at 0,
    // and the reassembler's "last received seq num" is initialized to 0 and updated when new fragments are
    // received from the peripheral, so we don't need to explicitly mark the ack num to send here.
    ReturnErrorOnFailure(StartSendAckTimer());

    // We've sent a capabilities request write and received a compatible response, so the connect
    // operation has completed successfully.
    return HandleConnectComplete();
}

// Returns number of open slots in remote receive window given the input values.
SequenceNumber_t WiFiPAFEndPoint::AdjustRemoteReceiveWindow(SequenceNumber_t lastReceivedAck, SequenceNumber_t maxRemoteWindowSize,
                                                            SequenceNumber_t newestUnackedSentSeqNum)
{
    // Assumption: SequenceNumber_t is uint8_t.
    // Assumption: Maximum possible sequence number value is UINT8_MAX.
    // Assumption: Sequence numbers incremented past maximum value wrap to 0.
    // Assumption: newest unacked sent sequence number never exceeds current (and by extension, new and un-wrapped)
    //             window boundary, so it never wraps relative to last received ack, if new window boundary would not
    //             also wrap.

    // Define new window boundary (inclusive) as uint16_t, so its value can temporarily exceed UINT8_MAX.
    uint16_t newRemoteWindowBoundary = static_cast<uint16_t>(lastReceivedAck + maxRemoteWindowSize);

    if (newRemoteWindowBoundary > UINT8_MAX && newestUnackedSentSeqNum < lastReceivedAck)
    {
        // New window boundary WOULD wrap, and latest unacked seq num already HAS wrapped, so add offset to difference.
        return static_cast<uint8_t>(newRemoteWindowBoundary - (newestUnackedSentSeqNum + UINT8_MAX));
    }

    // Neither values would or have wrapped, OR new boundary WOULD wrap but latest unacked seq num does not, so no
    // offset required.
    return static_cast<uint8_t>(newRemoteWindowBoundary - newestUnackedSentSeqNum);
}

CHIP_ERROR WiFiPAFEndPoint::GetPktSn(Encoding::LittleEndian::Reader & reader, uint8_t * pHead, SequenceNumber_t & seqNum)
{
    CHIP_ERROR err;
    BitFlags<WiFiPAFTP::HeaderFlags> rx_flags;
    size_t SnOffset = 0;
    SequenceNumber_t * pSn;
    err = reader.Read8(rx_flags.RawStorage()).StatusCode();
    if (rx_flags.Has(WiFiPAFTP::HeaderFlags::kHankshake))
    {
        // Handkshake message => No ack/sn
        return CHIP_ERROR_INTERNAL;
    }
    // Always has header flag
    SnOffset += kTransferProtocolHeaderFlagsSize;
    if (rx_flags.Has(WiFiPAFTP::HeaderFlags::kManagementOpcode)) // Has Mgmt_Op
    {
        SnOffset += kTransferProtocolMgmtOpSize;
    }
    if (rx_flags.Has(WiFiPAFTP::HeaderFlags::kFragmentAck)) // Has ack
    {
        SnOffset += kTransferProtocolAckSize;
    }
    pSn    = pHead + SnOffset;
    seqNum = *pSn;

    return CHIP_NO_ERROR;
}

CHIP_ERROR WiFiPAFEndPoint::DebugPktAckSn(const PktDirect_t PktDirect, Encoding::LittleEndian::Reader & reader, uint8_t * pHead)
{
#ifdef CHIP_WIFIPAF_END_POINT_DEBUG_LOGGING_ENABLED
    BitFlags<WiFiPAFTP::HeaderFlags> rx_flags;
    CHIP_ERROR err;
    uint8_t * pAct = nullptr;
    char AckBuff[4];
    uint8_t * pSn;
    size_t SnOffset = 0;

    err = reader.Read8(rx_flags.RawStorage()).StatusCode();
    SuccessOrExit(err);
    if (rx_flags.Has(WiFiPAFTP::HeaderFlags::kHankshake))
    {
        // Handkshake message => No ack/sn
        return CHIP_NO_ERROR;
    }
    // Always has header flag
    SnOffset += kTransferProtocolHeaderFlagsSize;
    if (rx_flags.Has(WiFiPAFTP::HeaderFlags::kManagementOpcode)) // Has Mgmt_Op
    {
        SnOffset += kTransferProtocolMgmtOpSize;
    }
    if (rx_flags.Has(WiFiPAFTP::HeaderFlags::kFragmentAck)) // Has ack
    {
        pAct = pHead + kTransferProtocolHeaderFlagsSize;
        SnOffset += kTransferProtocolAckSize;
    }
    pSn = pHead + SnOffset;
    if (pAct == nullptr)
    {
        strcpy(AckBuff, "  ");
    }
    else
    {
        snprintf(AckBuff, sizeof(AckBuff), "%02hhu", *pAct);
    }
    if (PktDirect == PktDirect_t::kTx)
    {
        ChipLogDebugWiFiPAFEndPoint_L0(WiFiPAF, "==>[tx] [Sn, Ack] = [   %02u, -- %s]", *pSn, AckBuff);
    }
    else if (PktDirect == PktDirect_t::kRx)
    {
        ChipLogDebugWiFiPAFEndPoint_L0(WiFiPAF, "<==[rx] [Ack, Sn] = [-- %s,    %02u]", AckBuff, *pSn);
    }
exit:
    return err;
#else
    return CHIP_NO_ERROR;
#endif
}

CHIP_ERROR WiFiPAFEndPoint::Receive(PacketBufferHandle && data)
{
    SequenceNumber_t ExpRxNextSeqNum = mPafTP.GetRxNextSeqNum();
    SequenceNumber_t seqNum;
    Encoding::LittleEndian::Reader reader(data->Start(), data->DataLength());
    CHIP_ERROR err = CHIP_NO_ERROR;

    err = GetPktSn(reader, data->Start(), seqNum);
    if (err != CHIP_NO_ERROR)
    {
        // Failed to get SeqNum. => Pass down to PAFTP engine directly
        ChipLogError(WiFiPAF, "WiFiPAF RECEIVE: Failed to get SeqNum - passing to PAFTP engine directly");
        return RxPacketProcess(std::move(data));
    }

    // Log the sequence numbers to help with debugging
    ChipLogProgress(WiFiPAF, "WiFiPAF RECEIVE: Got packet with seqNum=%u, expected=%u", seqNum, ExpRxNextSeqNum);
    
    /*
        If reorder-queue is not empty => Need to queue the packet whose SeqNum is the next one at
        offset 0 to fill the hole.
    */
    if ((ExpRxNextSeqNum == seqNum) && (ItemsInReorderQueue == 0)) {
        ChipLogProgress(WiFiPAF, "WiFiPAF RECEIVE: Expected sequence number received - processing directly");
        return RxPacketProcess(std::move(data));
    }

    // Start reordering packets
    ChipLogDetail(WiFiPAF, "WiFiPAF RECEIVE: Reordering packet [expected=%u, received=%u]", ExpRxNextSeqNum, seqNum);
    
    // Calculate offset, handling wrap-around cases
    uint16_t offset;
    
    // If the received sequence number is smaller than expected, it may have wrapped around
    if (seqNum < ExpRxNextSeqNum)
    {
        // Calculate as if wrapped around (e.g., seqNum=0, expected=255 should be offset 1)
        unsigned int temp1 = static_cast<unsigned int>(seqNum);
        unsigned int temp2 = 256; // Use 256 to handle wrap-around
        unsigned int temp3 = static_cast<unsigned int>(ExpRxNextSeqNum);
        unsigned int tempResult = temp1 + temp2 - temp3;
        offset = static_cast<uint16_t>(tempResult);
        
        // If offset is too large, it's likely not a wrap-around but an old/duplicate packet
        if (offset > PAFTP_REORDER_QUEUE_SIZE)
        {
            // It's likely a retransmission or old packet - handle directly
            ChipLogDetail(WiFiPAF, "WiFiPAF RECEIVE: Received old/retransmitted packet - processing directly");
            return RxPacketProcess(std::move(data));
        }
    }
    else
    {
        // Normal case: received sequence number is higher than expected
        offset = seqNum - ExpRxNextSeqNum;
    }
    
    // Ensure offset is within queue range
    if (offset >= PAFTP_REORDER_QUEUE_SIZE)
    {
        // Offset is too big
        // => It may be the unexpected packet or duplicate packet => process directly
        ChipLogDetail(WiFiPAF, "WiFiPAF RECEIVE: Offset (%u) is too big - processing directly instead of dropping", offset);
        return RxPacketProcess(std::move(data));
    }

    // Save the packet to the reorder-queue
    if (ReorderQueue[offset] == nullptr)
    {
        ChipLogDetail(WiFiPAF, "WiFiPAF RECEIVE: Queuing packet at offset %u", offset);
        ReorderQueue[offset] = std::move(data).UnsafeRelease();
        ItemsInReorderQueue++;
    }
    else
    {
        ChipLogDetail(WiFiPAF, "WiFiPAF RECEIVE: Queue slot already occupied at offset %u - processing directly", offset);
        return RxPacketProcess(std::move(data));
    }

    // Consume the packets in the reorder queue if no hole exists
    if (ReorderQueue[0] == nullptr)
    {
        // The hole still exists => Can't continue
        ChipLogDetail(WiFiPAF, "WiFiPAF RECEIVE: Hole still exists at offset 0. Packets in reorder-queue: %u", ItemsInReorderQueue);
        return CHIP_NO_ERROR;
    }
    
    ChipLogDetail(WiFiPAF, "WiFiPAF RECEIVE: Processing packets from reorder queue");
    uint8_t qidx;
    for (qidx = 0; qidx < PAFTP_REORDER_QUEUE_SIZE; qidx++)
    {
        // The head slots should have been filled. => Do rx processing
        if (ReorderQueue[qidx] == nullptr)
        {
            // Stop consuming packets until the hole or no packets
            break;
        }
        // Consume the saved packets
        ChipLogDetail(WiFiPAF, "WiFiPAF RECEIVE: Processing packet from reorder queue [%u]", qidx);
        err                = RxPacketProcess(System::PacketBufferHandle::Adopt(ReorderQueue[qidx]));
        ReorderQueue[qidx] = nullptr;
        ItemsInReorderQueue--;
    }
    // Has reached the 1st hole in the queue => move the rest items forward
    // Note: It's to continue => No need to reinit "i"
    for (uint8_t newId = 0; qidx < PAFTP_REORDER_QUEUE_SIZE; qidx++, newId++)
    {
        if (ReorderQueue[qidx] != nullptr)
        {
            ChipLogDetail(WiFiPAF, "WiFiPAF RECEIVE: Moving packet from offset %u to %u", qidx, newId);
            ReorderQueue[newId] = ReorderQueue[qidx];
            ReorderQueue[qidx]  = nullptr;
        }
    }
    
    ChipLogDetail(WiFiPAF, "WiFiPAF RECEIVE: Reordering complete, err: %" CHIP_ERROR_FORMAT, err.Format());
    return err;
}

CHIP_ERROR WiFiPAFEndPoint::RxPacketProcess(PacketBufferHandle && data)
{
    ChipLogDebugBufferWiFiPAFEndPoint(WiFiPAF, data);

    CHIP_ERROR err               = CHIP_NO_ERROR;
    SequenceNumber_t receivedAck = 0;
    uint8_t closeFlags           = kWiFiPAFCloseFlag_AbortTransmission;
    bool didReceiveAck           = false;
    BitFlags<WiFiPAFTP::HeaderFlags> rx_flags;
    Encoding::LittleEndian::Reader reader(data->Start(), data->DataLength());
    DebugPktAckSn(PktDirect_t::kRx, reader, data->Start());

    // Log the packet we're processing
    ChipLogDetail(WiFiPAF, "WiFiPAF RX PROCESS: Processing packet of length %u", static_cast<unsigned>(data->DataLength()));

    { // This is a special handling on the first CHIPoPAF data packet, the CapabilitiesRequest.
        // If we're receiving the first inbound packet of a PAF transport connection handshake...
        if (!mConnStateFlags.Has(ConnectionStateFlag::kCapabilitiesMsgReceived))
        {
            if (mRole == kWiFiPafRole_Subscriber) // If we're a central receiving a capabilities response indication...
            {
                // Ensure end point's in the right state before continuing.
                VerifyOrExit(mState == kState_Connecting, err = CHIP_ERROR_INCORRECT_STATE);
                mConnStateFlags.Set(ConnectionStateFlag::kCapabilitiesMsgReceived);
                err = HandleCapabilitiesResponseReceived(std::move(data));
                SuccessOrExit(err);
            }
            else // Or, a peripheral receiving a capabilities request write...
            {
                // Ensure end point's in the right state before continuing.
                VerifyOrExit(mState == kState_Ready, err = CHIP_ERROR_INCORRECT_STATE);
                mConnStateFlags.Set(ConnectionStateFlag::kCapabilitiesMsgReceived);
                err = HandleCapabilitiesRequestReceived(std::move(data));
                if (err != CHIP_NO_ERROR)
                {
                    // If an error occurred decoding and handling the capabilities request, release the BLE connection.
                    // Central's connect attempt will time out if peripheral's application decides to keep the BLE
                    // connection open, or fail immediately if the application closes the connection.
                    closeFlags = closeFlags | kWiFiPAFCloseFlag_SuppressCallback;
                    ExitNow();
                }
            }
            // If received data was handshake packet, don't feed it to message reassembler.
            ExitNow();
        }
    } // End handling the CapabilitiesRequest

    err = reader.Read8(rx_flags.RawStorage()).StatusCode();
    SuccessOrExit(err);
    if (rx_flags.Has(WiFiPAFTP::HeaderFlags::kHankshake))
    {
        ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "Unexpected handshake packet => drop");
        ExitNow();
    }

    ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "PAFTP about to rx characteristic, state before:");
    mPafTP.LogStateDebug();

    // Pass received packet into PAFTP protocol engine.
    err = mPafTP.HandleCharacteristicReceived(std::move(data), receivedAck, didReceiveAck);
    
    // Log the result of the protocol engine handling the packet
    ChipLogDetail(WiFiPAF, "WiFiPAF RX PROCESS: PAFTP engine result: %s, didReceiveAck=%d, receivedAck=%u", 
                chip::ErrorStr(err), didReceiveAck, receivedAck);

    ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "PAFTP rx'd characteristic, state after:");
    mPafTP.LogStateDebug();
    
    // Handle specific error cases for received packets
    if (err == WIFIPAF_ERROR_INVALID_PAFTP_SEQUENCE_NUMBER)
    {
        // This might be a duplicate packet, which we can safely ignore
        ChipLogDetail(WiFiPAF, "WiFiPAF RX PROCESS: Ignoring duplicate packet with invalid sequence number");
        err = CHIP_NO_ERROR;
        ExitNow();
    }
    else if (err != CHIP_NO_ERROR)
    {
        // For other errors, log but continue processing if possible
        ChipLogDetail(WiFiPAF, "WiFiPAF RX PROCESS: Error handling packet: %s", chip::ErrorStr(err));
        SuccessOrExit(err);
    }

    // Protocol engine accepted the fragment, so shrink local receive window counter by 1.
    mLocalReceiveWindowSize = static_cast<SequenceNumber_t>(mLocalReceiveWindowSize - 1);
    ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "decremented local rx window, new size = %u", mLocalReceiveWindowSize);

    // Respond to received ack, if any.
    if (didReceiveAck)
    {
        ChipLogDetail(WiFiPAF, "WiFiPAF RX PROCESS: Got ACK=%u in packet", receivedAck);
        
        // Reset retransmission counter on successful ACK
        mRetransmissionCount = 0;

        // If ack was rx'd for newest unacked sent fragment, stop ack received timer.
        if (!mPafTP.ExpectingAck())
        {
            ChipLogDetail(WiFiPAF, "WiFiPAF RX PROCESS: Got ACK for last outstanding fragment");
            StopAckReceivedTimer();
            // Also stop the retransmit timer since we received the ack
            StopRetransmitTimer();

            if (mState == kState_Closing && mSendQueue.IsNull() && mPafTP.TxState() == WiFiPAFTP::kState_Idle)
            {
                // If end point closing, got confirmation for last send, and waiting for last ack, finalize close.
                FinalizeClose(mState, kWiFiPAFCloseFlag_SuppressCallback, CHIP_NO_ERROR);
                ExitNow();
            }
        }
        else // Else there are still sent fragments for which acks are expected, so restart ack received timer.
        {
            ChipLogDetail(WiFiPAF, "WiFiPAF RX PROCESS: Still expecting ACKs, restarting timers");
            err = RestartAckReceivedTimer();
            // Also restart retransmit timer since we're still waiting for acks
            StopRetransmitTimer();
            StartRetransmitTimer();
            SuccessOrExit(err);
        }

        ChipLogDebugWiFiPAFEndPoint(
            WiFiPAF, "about to adjust remote rx window; got ack num = %u, newest unacked sent seq num = %u, \
                old window size = %u, max window size = %u",
            receivedAck, mPafTP.GetNewestUnackedSentSequenceNumber(), mRemoteReceiveWindowSize, mReceiveWindowMaxSize);

        // Open remote device's receive window according to sequence number it just acknowledged.
        mRemoteReceiveWindowSize =
            AdjustRemoteReceiveWindow(receivedAck, mReceiveWindowMaxSize, mPafTP.GetNewestUnackedSentSequenceNumber());

        ChipLogDetail(WiFiPAF, "WiFiPAF RX PROCESS: Adjusted remote rx window, new size = %u", mRemoteReceiveWindowSize);

        // Restart message transmission if it was previously paused due to window exhaustion.
        err = DriveSending();
        SuccessOrExit(err);
    }

    // The previous DriveSending() might have generated a piggyback acknowledgement if there was
    // previously un-acked data.  Otherwise, prepare to send acknowledgement for newly received fragment.
    //
    // If local receive window is below immediate ack threshold, AND there is no previous stand-alone ack in
    // flight, AND there is no pending outbound message fragment on which the ack can and will be piggybacked,
    // send immediate stand-alone ack to reopen window for sender.
    //
    // The "operation in flight" check below covers "pending outbound message fragment" by extension, as when
    // a message has been passed to the end point via Send(), its next outbound fragment must either be in flight
    // itself, or awaiting the completion of another in-flight operation.
    //
    // If any operation is in flight that is NOT a stand-alone ack, the window size will be checked against
    // this threshold again when the operation is confirmed.
    if (mPafTP.HasUnackedData())
    {
        if (mLocalReceiveWindowSize <= WIFIPAF_CONFIG_IMMEDIATE_ACK_WINDOW_THRESHOLD &&
            !mConnStateFlags.Has(ConnectionStateFlag::kOperationInFlight))
        {
            ChipLogDetail(WiFiPAF, "WiFiPAF RX PROCESS: Sending immediate ACK");
            err = DriveStandAloneAck();
            SuccessOrExit(err);
        }
        else
        {
            ChipLogDetail(WiFiPAF, "WiFiPAF RX PROCESS: Starting send-ACK timer");

            // Send ack when timer expires.
            err = StartSendAckTimer();
            SuccessOrExit(err);
        }
    }

    // If we've reassembled a whole message...
    if (mPafTP.RxState() == WiFiPAFTP::kState_Complete)
    {
        // Take ownership of message buffer
        System::PacketBufferHandle full_packet = mPafTP.TakeRxPacket();

        // Log message reassembly completion with detailed information
        ChipLogDetail(WiFiPAF, "WiFiPAF REASSEMBLED COMPLETE MESSAGE: len=%u, session_id=%" PRIu32 ", peer_id=%" PRIu32 ", peer_mac=%02x:%02x:%02x:%02x:%02x:%02x",
                       static_cast<unsigned int>(full_packet->DataLength()), 
                       mSessionInfo.id, 
                       mSessionInfo.peer_id,
                       mSessionInfo.peer_addr[0], mSessionInfo.peer_addr[1], mSessionInfo.peer_addr[2],
                       mSessionInfo.peer_addr[3], mSessionInfo.peer_addr[4], mSessionInfo.peer_addr[5]);

        ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "reassembled whole msg, len = %u", static_cast<unsigned>(full_packet->DataLength()));

        // If we have a message received callback, and end point is not closing...
        if (mWiFiPafLayer != nullptr && mState != kState_Closing)
        {
            // Pass received message up the stack.
            err = mWiFiPafLayer->OnWiFiPAFMsgRxComplete(mSessionInfo, std::move(full_packet));
        }
    }

exit:
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(WiFiPAF, "WiFiPAF RX PROCESS: Error during processing: %s", chip::ErrorStr(err));
        DoClose(closeFlags, err);
    }

    return err;
}

CHIP_ERROR WiFiPAFEndPoint::StartRetransmitTimer()
{
    // Make sure we have a valid layer and system layer
    VerifyOrReturnError(mWiFiPafLayer != nullptr, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mWiFiPafLayer->mSystemLayer != nullptr, CHIP_ERROR_INCORRECT_STATE);
    
    if (!mTimerStateFlags.Has(TimerStateFlag::kRetransmitTimerRunning))
    {
        ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "starting RetransmitTimer");
        const CHIP_ERROR timerErr = mWiFiPafLayer->mSystemLayer->StartTimer(
            System::Clock::Milliseconds32(PAFTP_RETRANSMIT_TIMEOUT_MS), HandleRetransmitTimeout, this);
        ReturnErrorOnFailure(timerErr);

        mTimerStateFlags.Set(TimerStateFlag::kRetransmitTimerRunning);
    }

    return CHIP_NO_ERROR;
}

void WiFiPAFEndPoint::StopRetransmitTimer()
{
    // Make sure mWiFiPafLayer exists
    if (mWiFiPafLayer != nullptr)
    {
        // Cancel any existing retransmit timer.
        mWiFiPafLayer->mSystemLayer->CancelTimer(HandleRetransmitTimeout, this);
    }
    mTimerStateFlags.Clear(TimerStateFlag::kRetransmitTimerRunning);
}

void WiFiPAFEndPoint::HandleRetransmitTimeout(chip::System::Layer * systemLayer, void * appState)
{
    WiFiPAFEndPoint * ep = static_cast<WiFiPAFEndPoint *>(appState);

    // Check for event-based timer race condition.
    if (ep->mTimerStateFlags.Has(TimerStateFlag::kRetransmitTimerRunning))
    {
        ep->mTimerStateFlags.Clear(TimerStateFlag::kRetransmitTimerRunning);

        // Log current state before retransmission attempt
        ChipLogDetail(WiFiPAF, "WiFiPAF RETRANSMIT TIMER FIRED: state=%d, current_count=%u, max_count=%u",
                    ep->mState, ep->mRetransmitCount, ep->kMaxRetransmitCount);
        
        if (ep->mRetransmitCount < ep->kMaxRetransmitCount)
        {
            // We still have retransmission attempts remaining
            ChipLogDetail(WiFiPAF, "WiFiPAF RETRANSMIT: No ACK received after 3 seconds, retransmitting packet (attempt %u of %u)", 
                        ep->mRetransmitCount + 1, ep->kMaxRetransmitCount);
            
            // Log what we're retransmitting
            if (!ep->mLastTxPacket.IsNull())
            {
                ChipLogDetail(WiFiPAF, "WiFiPAF RETRANSMIT: packet length=%u bytes", 
                           static_cast<unsigned int>(ep->mLastTxPacket->DataLength()));
            }
            
            CHIP_ERROR retransmitErr = ep->RetransmitLastPacket();
            if (retransmitErr != CHIP_NO_ERROR)
            {
                ChipLogError(WiFiPAF, "WiFiPAF RETRANSMIT FAILED: %s", chip::ErrorStr(retransmitErr));
                ep->DoClose(kWiFiPAFCloseFlag_AbortTransmission, retransmitErr);
            }
        }
        else
        {
            // We've reached the maximum number of retransmission attempts
            ChipLogDetail(WiFiPAF, "WiFiPAF RETRANSMIT: Max retransmission attempts (%u) reached, giving up", ep->kMaxRetransmitCount);
            ep->DoClose(kWiFiPAFCloseFlag_AbortTransmission, WIFIPAF_ERROR_MAX_RETRANSMIT_ATTEMPTS_REACHED);
        }
    }
}

CHIP_ERROR WiFiPAFEndPoint::RetransmitLastPacket()
{
    // Verify that we have a packet to retransmit
    VerifyOrReturnError(!mLastTxPacket.IsNull(), CHIP_ERROR_INCORRECT_STATE);
    
    // Verify that we have valid layer and transport objects
    VerifyOrReturnError(mWiFiPafLayer != nullptr, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mWiFiPafLayer->mWiFiPAFTransport != nullptr, CHIP_ERROR_INCORRECT_STATE);

    // Increment the retransmit count
    mRetransmissionCount++;

    // Log that we're retransmitting the packet
    ChipLogDetail(WiFiPAF, "WiFiPAF RETRANSMITTING PACKET: len=%u, session_id=%" PRIu32 ", peer_id=%" PRIu32 ", attempt=%u/%u",
                static_cast<unsigned int>(mLastTxPacket->DataLength()), 
                mSessionInfo.id, 
                mSessionInfo.peer_id,
                mRetransmissionCount,
                kMaxRetransmitCount);

    // Start a new operation to resend the packet
    mConnStateFlags.Set(ConnectionStateFlag::kOperationInFlight);
    
    // Send a duplicate of the last sent packet
    mWiFiPafLayer->mWiFiPAFTransport->WiFiPAFMessageSend(mSessionInfo, mLastTxPacket.Retain());
    
    // Start the retransmit timer again for the next attempt
    return StartRetransmitTimer();
}

CHIP_ERROR WiFiPAFEndPoint::SendWrite(PacketBufferHandle && buf)
{
    // Verify we have valid layer and transport objects
    VerifyOrReturnError(mWiFiPafLayer != nullptr, CHIP_ERROR_INCORRECT_STATE);
    VerifyOrReturnError(mWiFiPafLayer->mWiFiPAFTransport != nullptr, CHIP_ERROR_INCORRECT_STATE);
    
    mConnStateFlags.Set(ConnectionStateFlag::kOperationInFlight);

    ChipLogDebugBufferWiFiPAFEndPoint(WiFiPAF, buf);
    Encoding::LittleEndian::Reader reader(buf->Start(), buf->DataLength());
    DebugPktAckSn(PktDirect_t::kTx, reader, buf->Start());
    
    // Log packet send with detailed information
    ChipLogDetail(WiFiPAF, "WiFiPAF SENDING PACKET: len=%u, session_id=%" PRIu32 ", peer_id=%" PRIu32 ", peer_mac=%02x:%02x:%02x:%02x:%02x:%02x",
                   static_cast<unsigned int>(buf->DataLength()), 
                   mSessionInfo.id, 
                   mSessionInfo.peer_id,
                   mSessionInfo.peer_addr[0], mSessionInfo.peer_addr[1], mSessionInfo.peer_addr[2],
                   mSessionInfo.peer_addr[3], mSessionInfo.peer_addr[4], mSessionInfo.peer_addr[5]);
    
    // Reset retransmission counter when sending a new packet
    mRetransmissionCount = 0;
    
    // Save a copy of the packet for potential retransmission
    // First, clear any existing saved packet
    mLastTxPacket = nullptr;
    
    // Then make a copy of the current packet
    mLastTxPacket = buf.Retain();
    
    // Add a clear debug print when sending a packet
    ChipLogDetail(WiFiPAF, "PAF SEND: Sending packet with size %u bytes", static_cast<unsigned int>(buf->DataLength()));
    
    mWiFiPafLayer->mWiFiPAFTransport->WiFiPAFMessageSend(mSessionInfo, std::move(buf));

    return CHIP_NO_ERROR;
}

CHIP_ERROR WiFiPAFEndPoint::StartConnectTimer()
{
    const CHIP_ERROR timerErr = mWiFiPafLayer->mSystemLayer->StartTimer(System::Clock::Milliseconds32(PAFTP_CONN_RSP_TIMEOUT_MS),
                                                                        HandleConnectTimeout, this);
    ReturnErrorOnFailure(timerErr);
    mTimerStateFlags.Set(TimerStateFlag::kConnectTimerRunning);

    return CHIP_NO_ERROR;
}

CHIP_ERROR WiFiPAFEndPoint::StartAckReceivedTimer()
{
    if (!mTimerStateFlags.Has(TimerStateFlag::kAckReceivedTimerRunning))
    {
        ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "starting AckReceivedTimer");
        
        // Use PAFTP_ACK_TIMEOUT_MS_OVERRIDE to provide enough time for retransmissions
        CHIP_ERROR err = mWiFiPafLayer->mSystemLayer->StartTimer(System::Clock::Milliseconds32(PAFTP_ACK_TIMEOUT_MS_OVERRIDE),
                                                                HandleAckReceivedTimeout, this);
        if (err != CHIP_NO_ERROR)
        {
            return err;
        }
        mTimerStateFlags.Set(TimerStateFlag::kAckReceivedTimerRunning);
    }
    return CHIP_NO_ERROR;
}

CHIP_ERROR WiFiPAFEndPoint::RestartAckReceivedTimer()
{
    VerifyOrReturnError(mTimerStateFlags.Has(TimerStateFlag::kAckReceivedTimerRunning), CHIP_ERROR_INCORRECT_STATE);

    StopAckReceivedTimer();

    return StartAckReceivedTimer();
}

CHIP_ERROR WiFiPAFEndPoint::StartSendAckTimer()
{
    if (!mTimerStateFlags.Has(TimerStateFlag::kSendAckTimerRunning))
    {
        ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "starting new SendAckTimer");
        const CHIP_ERROR timerErr = mWiFiPafLayer->mSystemLayer->StartTimer(
            System::Clock::Milliseconds32(WIFIPAF_ACK_SEND_TIMEOUT_MS), HandleSendAckTimeout, this);
        ReturnErrorOnFailure(timerErr);

        mTimerStateFlags.Set(TimerStateFlag::kSendAckTimerRunning);
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR WiFiPAFEndPoint::StartWaitResourceTimer()
{
    mResourceWaitCount++;
    if (mResourceWaitCount >= WIFIPAF_MAX_RESOURCE_BLOCK_COUNT)
    {
        ChipLogError(WiFiPAF, "Network resource has been unavailable for a long time");
        mResourceWaitCount = 0;
        DoClose(kWiFiPAFCloseFlag_AbortTransmission, CHIP_ERROR_NOT_CONNECTED);
        return CHIP_NO_ERROR;
    }
    if (!mTimerStateFlags.Has(TimerStateFlag::kWaitResTimerRunning))
    {
        ChipLogDebugWiFiPAFEndPoint(WiFiPAF, "starting new SendAckTimer");
        const CHIP_ERROR timerErr = mWiFiPafLayer->mSystemLayer->StartTimer(
            System::Clock::Milliseconds32(WIFIPAF_WAIT_RES_TIMEOUT_MS), HandleWaitResourceTimeout, this);
        ReturnErrorOnFailure(timerErr);
        mTimerStateFlags.Set(TimerStateFlag::kWaitResTimerRunning);
    }
    return CHIP_NO_ERROR;
}

void WiFiPAFEndPoint::StopConnectTimer()
{
    // Cancel any existing connect timer.
    mWiFiPafLayer->mSystemLayer->CancelTimer(HandleConnectTimeout, this);
    mTimerStateFlags.Clear(TimerStateFlag::kConnectTimerRunning);
}

void WiFiPAFEndPoint::StopAckReceivedTimer()
{
    // Cancel any existing ack-received timer.
    mWiFiPafLayer->mSystemLayer->CancelTimer(HandleAckReceivedTimeout, this);
    mTimerStateFlags.Clear(TimerStateFlag::kAckReceivedTimerRunning);
    
    // When stopping the timer normally (such as when an ACK is received),
    // we can clear the last tx packet
    if (mRetransmissionCount == 0) {
        mLastTxPacket = nullptr;
    }
}

void WiFiPAFEndPoint::StopSendAckTimer()
{
    // Cancel any existing send-ack timer.
    mWiFiPafLayer->mSystemLayer->CancelTimer(HandleSendAckTimeout, this);
    mTimerStateFlags.Clear(TimerStateFlag::kSendAckTimerRunning);
}

void WiFiPAFEndPoint::StopWaitResourceTimer()
{
    // Cancel any existing wait-resource timer.
    mWiFiPafLayer->mSystemLayer->CancelTimer(HandleWaitResourceTimeout, this);
    mTimerStateFlags.Clear(TimerStateFlag::kWaitResTimerRunning);
}

void WiFiPAFEndPoint::HandleConnectTimeout(chip::System::Layer * systemLayer, void * appState)
{
    WiFiPAFEndPoint * ep = static_cast<WiFiPAFEndPoint *>(appState);

    // Check for event-based timer race condition.
    if (ep->mTimerStateFlags.Has(TimerStateFlag::kConnectTimerRunning))
    {
        ChipLogError(WiFiPAF, "connect handshake timed out, closing ep %p", ep);
        ep->mTimerStateFlags.Clear(TimerStateFlag::kConnectTimerRunning);
        ep->DoClose(kWiFiPAFCloseFlag_AbortTransmission, WIFIPAF_ERROR_CONNECT_TIMED_OUT);
    }
}

void WiFiPAFEndPoint::HandleAckReceivedTimeout(chip::System::Layer * systemLayer, void * appState)
{
    WiFiPAFEndPoint * ep = static_cast<WiFiPAFEndPoint *>(appState);

    // Check for event-based timer race condition.
    if (ep->mTimerStateFlags.Has(TimerStateFlag::kAckReceivedTimerRunning))
    {
        // Mark timer as stopped
        ep->mTimerStateFlags.Clear(TimerStateFlag::kAckReceivedTimerRunning);
        
        // Increment retransmission counter
        ep->mRetransmissionCount++;
        
        // Check if we've exceeded the maximum number of retransmission attempts
        if (ep->mRetransmissionCount >= kMaxRetransmissionAttempts)
        {
            // Too many retransmission attempts, close the endpoint
            ChipLogError(WiFiPAF, "PAF ACK TIMEOUT: Max retransmission attempts (%d) reached, closing endpoint", 
                         kMaxRetransmissionAttempts);
            ChipLogError(WiFiPAF, "ack recv timeout, closing ep %p", ep);
            ep->mPafTP.LogStateDebug();
            ep->DoClose(kWiFiPAFCloseFlag_AbortTransmission, WIFIPAF_ERROR_FRAGMENT_ACK_TIMED_OUT);
            return;
        }
        
        // Attempt to retransmit the message
        ChipLogError(WiFiPAF, "PAF ACK TIMEOUT: No acknowledgment received, retransmission attempt %d/%d", 
                     ep->mRetransmissionCount, kMaxRetransmissionAttempts);
        
        CHIP_ERROR err = CHIP_NO_ERROR;
        
        // Try to send the last packet that was saved
        if (!ep->mLastTxPacket.IsNull())
        {
            ChipLogProgress(WiFiPAF, "PAF RETRANSMIT: Resending last saved packet");
            
            // Verify that we have valid layer and transport objects
            if (ep->mWiFiPafLayer != nullptr && ep->mWiFiPafLayer->mWiFiPAFTransport != nullptr)
            {
                // Resend the packet by sending a copy of our saved packet
                err = ep->mWiFiPafLayer->mWiFiPAFTransport->WiFiPAFMessageSend(ep->mSessionInfo, ep->mLastTxPacket.Retain());
                
                if (err == CHIP_NO_ERROR)
                {
                    // Restart the ACK received timer
                    err = ep->StartAckReceivedTimer();
                    if (err != CHIP_NO_ERROR)
                    {
                        ChipLogError(WiFiPAF, "PAF RETRANSMIT: Failed to restart ACK timer: %" CHIP_ERROR_FORMAT, err.Format());
                        ep->DoClose(kWiFiPAFCloseFlag_AbortTransmission, err);
                    }
                }
                else
                {
                    ChipLogError(WiFiPAF, "PAF RETRANSMIT: Failed to resend packet: %" CHIP_ERROR_FORMAT, err.Format());
                    ep->DoClose(kWiFiPAFCloseFlag_AbortTransmission, err);
                }
            }
            else
            {
                ChipLogError(WiFiPAF, "PAF RETRANSMIT: Missing WiFiPAFLayer or WiFiPAFTransport");
                ep->DoClose(kWiFiPAFCloseFlag_AbortTransmission, CHIP_ERROR_INCORRECT_STATE);
            }
        }
        else
        {
            ChipLogError(WiFiPAF, "PAF RETRANSMIT: No packet available to retransmit");
            ep->DoClose(kWiFiPAFCloseFlag_AbortTransmission, WIFIPAF_ERROR_FRAGMENT_ACK_TIMED_OUT);
        }
    }
}

void WiFiPAFEndPoint::HandleSendAckTimeout(chip::System::Layer * systemLayer, void * appState)
{
    WiFiPAFEndPoint * ep = static_cast<WiFiPAFEndPoint *>(appState);

    // Check for event-based timer race condition.
    if (ep->mTimerStateFlags.Has(TimerStateFlag::kSendAckTimerRunning))
    {
        ep->mTimerStateFlags.Clear(TimerStateFlag::kSendAckTimerRunning);

        // If previous stand-alone ack isn't still in flight...
        if (!ep->mConnStateFlags.Has(ConnectionStateFlag::kStandAloneAckInFlight))
        {
            CHIP_ERROR sendErr = ep->DriveStandAloneAck();

            if (sendErr != CHIP_NO_ERROR)
            {
                ep->DoClose(kWiFiPAFCloseFlag_AbortTransmission, sendErr);
            }
        }
    }
}

void WiFiPAFEndPoint::HandleWaitResourceTimeout(chip::System::Layer * systemLayer, void * appState)
{
    WiFiPAFEndPoint * ep = static_cast<WiFiPAFEndPoint *>(appState);

    // Check for event-based timer race condition.
    if (ep->mTimerStateFlags.Has(TimerStateFlag::kWaitResTimerRunning))
    {
        ep->mTimerStateFlags.Clear(TimerStateFlag::kWaitResTimerRunning);
        CHIP_ERROR sendErr = ep->DriveSending();
        if (sendErr != CHIP_NO_ERROR)
        {
            ep->DoClose(kWiFiPAFCloseFlag_AbortTransmission, sendErr);
        }
    }
}

void WiFiPAFEndPoint::ClearAll()
{
    // Free the last sent packet buffer before clearing everything
    mLastTxPacket = nullptr;
    
    // Reset retransmit count
    mRetransmissionCount = 0;
    
    // Proceed with the original clear operation
    memset(reinterpret_cast<uint8_t *>(this), 0, sizeof(WiFiPAFEndPoint));
    return;
}

} /* namespace WiFiPAF */
} /* namespace chip */
