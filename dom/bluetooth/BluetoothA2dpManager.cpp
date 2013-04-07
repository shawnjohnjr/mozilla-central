/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothA2dpManager.h"
#include "BluetoothReplyRunnable.h"
#include "BluetoothService.h"
#include "AudioSystem.h"
#include "nsThreadUtils.h"
#include "BluetoothUtils.h"
#include <utils/String8.h>
#include <android/log.h>

#include "nsIAudioManager.h"
#include "nsIObserverService.h"
#include "mozilla/dom/bluetooth/BluetoothTypes.h"
#include "nsRadioInterfaceLayer.h"
#define BLUETOOTH_A2DP_STATUS_CHANGED "bluetooth-a2dp-status-changed"

USING_BLUETOOTH_NAMESPACE

namespace {
  static nsAutoPtr<BluetoothA2dpManager> gBluetoothA2dpManager;
} // anonymous namespace

BluetoothA2dpManager::BluetoothA2dpManager()
{
  mDeviceAddress.Truncate();
  mListener = new BluetoothTelephonyListener();

  if (!mListener->StartListening()) {
    NS_WARNING("Failed to start listening RIL");
  }

}

BluetoothA2dpManager::~BluetoothA2dpManager()
{
  if (!mListener->StopListening()) {
    NS_WARNING("Failed to stop listening RIL");
  }
  mListener = nullptr;

}

/*static*/
BluetoothA2dpManager*
BluetoothA2dpManager::Get()
{
  MOZ_ASSERT(NS_IsMainThread());

  // If we already exist, exit early
  if (gBluetoothA2dpManager) {
    BT_LOG("return existing BluetoothA2dpManager");
    return gBluetoothA2dpManager;
  }

  gBluetoothA2dpManager = new BluetoothA2dpManager();
  BT_LOG("A new BluetoothA2dpManager");
  return gBluetoothA2dpManager;
};

static BluetoothA2dpState
ConvertSinkStringToState(const nsAString& aNewState)
{
  if (aNewState.EqualsLiteral("disonnected"))
    return BluetoothA2dpState::SINK_DISCONNECTED;
  if (aNewState.EqualsLiteral("connecting"))
    return BluetoothA2dpState::SINK_CONNECTING;
  if (aNewState.EqualsLiteral("connected"))
    return BluetoothA2dpState::SINK_CONNECTED;
  if (aNewState.EqualsLiteral("playing"))
    return BluetoothA2dpState::SINK_PLAYING;
    return BluetoothA2dpState::SINK_DISCONNECTED;
}

static void
SetParameter(const nsAString& aParameter)
{
  android::String8 cmd;
  cmd.appendFormat(NS_ConvertUTF16toUTF8(aParameter).get());
  android::AudioSystem::setParameters(0, cmd);
}

void
BluetoothA2dpManager::DispatchConnectionStatus(const nsAString& aDeviceAddress,
                                               bool aIsConnected)
{
  MOZ_ASSERT(NS_IsMainThread());

  mDeviceAddress = aDeviceAddress;
  mConnected = aIsConnected;

  InfallibleTArray<BluetoothNamedValue> data;
  data.AppendElement(BluetoothNamedValue(
    NS_LITERAL_STRING("address"), nsString(aDeviceAddress)));
  data.AppendElement(BluetoothNamedValue(
    NS_LITERAL_STRING("status"), aIsConnected));

  BluetoothSignal signal(NS_LITERAL_STRING("A2dpStatusChanged"),
                         NS_LITERAL_STRING(KEY_MANAGER), data);
  BluetoothService* bs = BluetoothService::Get();
  NS_ENSURE_TRUE_VOID(bs);
  bs->DistributeSignal(signal);
}

void
BluetoothA2dpManager::NotifyAudioManager(const nsAString& aAddress)
{
  BT_LOG("[A2DP] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIObserverService> obs =
    do_GetService("@mozilla.org/observer-service;1");
  NS_ENSURE_TRUE_VOID(obs);

  nsCOMPtr<nsIAudioManager> am =
    do_GetService("@mozilla.org/telephony/audiomanager;1");
  NS_ENSURE_TRUE_VOID(am);

  int force;
  if (aAddress.IsEmpty()) {
    if (NS_FAILED(obs->NotifyObservers(nullptr,
                                       BLUETOOTH_A2DP_STATUS_CHANGED,
                                       nullptr))) {
      NS_WARNING("Failed to notify bluetooth-a2dp-status-changed observsers!");
      return;
    }
    force = am->FORCE_BT_A2DP;
  } else {
    if (NS_FAILED(obs->NotifyObservers(nullptr,
                                       BLUETOOTH_A2DP_STATUS_CHANGED,
                                       aAddress.BeginReading()))) {
      NS_WARNING("Failed to notify bluetooth-a2dp-status-changed observsers!");
      return;
    }
    force = am->FORCE_NONE;
  }
  am->SetForceForUse(am->USE_MEDIA, force);
}

/* HandleSinkStatusChanged stores current A2DP state
 * Possible values: "disconnected", "connecting","connected", "playing"
 * 1. "disconnected" -> "connecting"
 *  Either an incoming or outgoing connection
 *  attempt ongoing.
 * 2. "connecting" -> "disconnected"
 * Connection attempt failed
 * 3. "connecting" -> "connected"
 *     Successfully connected
 * 4. "connected" -> "playing"
 *     SCO audio connection successfully opened
 * 5. "playing" -> "connected"
 *     SCO audio connection closed
 * 6. "connected" -> "disconnected"
 * 7. "playing" -> "disconnected"
 *     Disconnected from the remote device
 */
void
BluetoothA2dpManager::HandleSinkStatusChanged(const nsAString& aDeviceAddress,
                                              const nsAString& aState)
{
  if (aState.EqualsLiteral("connected")) {
    BT_LOG("A2DP connected!! Route path to a2dp");
    BT_LOG("Currnet device: %s",NS_ConvertUTF16toUTF8(mDeviceAddress).get());
    NotifyAudioManager(aDeviceAddress);
  } else if (aState.EqualsLiteral("playing")) {
    BT_LOG("Start streaming Route path to a2dp");
  }
  mCurrentSinkState = ConvertSinkStringToState(aState);
  //TODO: Need to check Sink state and do more stuffs
}

bool
BluetoothA2dpManager::Connect(const nsAString& aDeviceAddress)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (mConnected && (mDeviceAddress != aDeviceAddress)) {
    NS_WARNING("BluetoothA2dpManager has connected/is connecting to a device!");
    return false;
  }

  BluetoothService* bs = BluetoothService::Get();
  NS_ENSURE_TRUE(bs, false);

  if (!bs->ConnectSink(aDeviceAddress)) {
    BT_LOG("[A2DP] Failed to start connecting to sink");
    DispatchConnectionStatus(aDeviceAddress, false);
    return false;
  }

  return true;
}

void
BluetoothA2dpManager::Disconnect(const nsAString& aDeviceAddress)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (!mConnected) {
    NS_WARNING("BluetoothA2dpManager has been disconnected");
    return;
  }

  BluetoothService* bs = BluetoothService::Get();
  if (!bs->DisconnectSink(aDeviceAddress)) {
    BT_LOG("[A2DP] Disconnect failed!");
    return;
  }

  NotifyAudioManager(NS_LITERAL_STRING("")); 
  BT_LOG("[A2DP] Disconnect successfully!");
}

void
BluetoothA2dpManager::NotifyMusicPlayStatus()
{
  BT_LOG("%s", __FUNCTION__);

  nsAutoString type, name;
  InfallibleTArray<BluetoothNamedValue> parameters;
  type.AssignLiteral("bluetooth-avrcp-playstatus");

  if (!BroadcastSystemMessage(type, parameters)) {
    BT_LOG("fail to send system message");
  }
}

void
BluetoothA2dpManager::GetConnectedSinkAddress(nsAString& aDeviceAddress)
{
  BT_LOG("mDeviceAddress: %s", NS_ConvertUTF16toUTF8(mDeviceAddress).get());
  aDeviceAddress = mDeviceAddress;
}

void
BluetoothA2dpManager::HandleCallStateChanged(uint16_t aCallState)
{
  switch (aCallState) {
    case nsITelephonyProvider::CALL_STATE_INCOMING:
    case nsITelephonyProvider::CALL_STATE_CONNECTED:
      BT_LOG("BluetoothA2dpManager: CALL_STATE_INCOMING/CALL_STATE_CONNECTED, disable AVRCP");
      // PHONE_STATE RINGING or OFFHOOK
      // We have to suspend a2dp stream, it is compliant with the Bluetooth SIG AV+HFP
      // Whitepaper. We shall not have A2DP in streaming state while HFP manager
      // is trying to send AT command "RING".
      SetParameter(NS_LITERAL_STRING("A2dpSuspended=true"));
      break;
      //PHONE_STATE IDLE
    case nsITelephonyProvider::CALL_STATE_DISCONNECTED:
      // IDLE state, we shall do delay resuming sink, there are many chipsets on
      // Bluetooth headsets cannot handle A2DP sink resuming stream too fast,
      // while SCO state is in disconnecting
      BT_LOG("BluetoothA2dpManager: CALL_STATE_DISCONNECTED, delay resume sink");
      SetParameter(NS_LITERAL_STRING("A2dpSuspended=false"));
      break;
    case nsITelephonyProvider::CALL_STATE_DIALING:
    case nsITelephonyProvider::CALL_STATE_ALERTING:
      break;
    default:
      break;
  }
}

bool
BluetoothA2dpManager::GetConnectionStatus()
{
  return mCurrentSinkState == BluetoothA2dpState::SINK_CONNECTED;
}

/*bool
BluetoothA2dpManager::Listen()
{
  BT_LOG("[A2DP] Listen");
  return true;
}*/

