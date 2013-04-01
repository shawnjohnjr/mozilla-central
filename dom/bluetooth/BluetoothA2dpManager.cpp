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

#define BLUETOOTH_A2DP_STATUS_CHANGED "bluetooth-a2dp-status-changed"

USING_BLUETOOTH_NAMESPACE

namespace {
  static nsAutoPtr<BluetoothA2dpManager> gBluetoothA2dpManager;
} // anonymous namespace

BluetoothA2dpManager::BluetoothA2dpManager()
{
  mConnectedDeviceAddress.AssignLiteral(BLUETOOTH_INVALID_ADDRESS);
}

BluetoothA2dpManager::~BluetoothA2dpManager()
{

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
NotifyAudioManager(const nsAString& aAddress)
{
  BT_LOG("[A2DP] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIObserverService> obs =
    do_GetService("@mozilla.org/observer-service;1");
  NS_ENSURE_TRUE_VOID(obs);

  if (aAddress.IsEmpty()) {
    if (NS_FAILED(obs->NotifyObservers(nullptr,
                                       BLUETOOTH_A2DP_STATUS_CHANGED,
                                       nullptr))) {
      NS_WARNING("Failed to notify bluetooth-a2dp-status-changed observsers!");
      return;
    }
  } else {
    if (NS_FAILED(obs->NotifyObservers(nullptr,
                                       BLUETOOTH_A2DP_STATUS_CHANGED,
                                       aAddress.BeginReading()))) {
      NS_WARNING("Failed to notify bluetooth-a2dp-status-changed observsers!");
      return;
    }
  }
}

static void
RouteA2dpAudioPath()
{
  SetParameter(NS_LITERAL_STRING("bluetooth_enabled=true"));
  SetParameter(NS_LITERAL_STRING("A2dpSuspended=false"));
  android::AudioSystem::setForceUse((audio_policy_force_use_t)1,
      (audio_policy_forced_cfg_t)0);
}

/* HandleSinkPropertyChange stores current A2DP state
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
BluetoothA2dpManager::HandleSinkPropertyChange(const nsAString& aDeviceObjectPath,
                         const nsAString& aNewState)
{

  if (aNewState.EqualsLiteral("connected")) {
    BT_LOG("A2DP connected!! Route path to a2dp");
    BT_LOG("Currnet device: %s",NS_ConvertUTF16toUTF8(mConnectedDeviceAddress).get());
    RouteA2dpAudioPath();
  } else if (aNewState.EqualsLiteral("playing")) {
    BT_LOG("Start streaming Route path to a2dp");
    RouteA2dpAudioPath();
  }
  mCurrentSinkState = ConvertSinkStringToState(aNewState);
  //TODO: Need to check Sink state and do more stuffs
}

bool
BluetoothA2dpManager::Connect(const nsAString& aDeviceAddress)
{
  MOZ_ASSERT(NS_IsMainThread());

  if ((mConnectedDeviceAddress != aDeviceAddress) &&
      (mConnectedDeviceAddress != NS_LITERAL_STRING(BLUETOOTH_INVALID_ADDRESS))) {
    BT_LOG("[A2DP] Connection already exists");
    return false;
  }

  BluetoothService* bs = BluetoothService::Get();
  NS_ENSURE_TRUE(bs, false);

  if (!bs->ConnectSink(aDeviceAddress)) {
    BT_LOG("[A2DP] Connect failed!");
    return false;
  }

  NotifyAudioManager(aDeviceAddress);
  BT_LOG("[A2DP] Connect successfully!");

  mConnectedDeviceAddress = aDeviceAddress;
  BT_LOG("Connected Device address:%s", NS_ConvertUTF16toUTF8(mConnectedDeviceAddress).get() );
  return true;
}

void
BluetoothA2dpManager::Disconnect(const nsAString& aDeviceAddress)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (mConnectedDeviceAddress == NS_LITERAL_STRING(BLUETOOTH_INVALID_ADDRESS)) {
    return;
  }

  BluetoothService* bs = BluetoothService::Get();
  if (!bs->DisconnectSink(aDeviceAddress)) {
    BT_LOG("[A2DP] Disconnect failed!");
    return;
  }

  NotifyAudioManager(NS_LITERAL_STRING("")); 
  BT_LOG("[A2DP] Disconnect successfully!");

  mConnectedDeviceAddress.AssignLiteral(BLUETOOTH_INVALID_ADDRESS);
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
  BT_LOG("mConnectedDeviceAddress: %s", NS_ConvertUTF16toUTF8(mConnectedDeviceAddress).get());
  aDeviceAddress = mConnectedDeviceAddress;
}

bool
BluetoothA2dpManager::Listen()
{
  BT_LOG("[A2DP] Listen");
  return true;
}

