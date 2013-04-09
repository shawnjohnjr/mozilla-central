/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetootha2dpmanager_h__
#define mozilla_dom_bluetooth_bluetootha2dpmanager_h__

#include "BluetoothCommon.h"
#include "BluetoothProfileManager.h"
#include "BluetoothTelephonyListener.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothReplyRunnable;

enum BluetoothA2dpState {
  SINK_DISCONNECTED = 0,
  SINK_CONNECTING = 1,
  SINK_CONNECTED = 2,
  SINK_PLAYING = 3
};

class BluetoothA2dpManager : public BluetoothProfileManager
{
public:
  ~BluetoothA2dpManager();
  static BluetoothA2dpManager* Get();
  bool Connect(const nsAString& aDeviceAddress);
  void Disconnect(const nsAString& aDeviceAddress);
  void GetConnectedSinkAddress(nsAString& aDeviceAddress);
  bool GetConnectionStatus();
  void NotifyMusicPlayStatus();
  void HandleSinkStatusChanged(const nsAString& aDeviceObjectPath,
                               const nsAString& newState);
  void HandleCallStateChanged(uint16_t aCallState);
  void DispatchConnectionStatus(const nsAString& aDeviceAddress,
                                bool aIsConnected);
private:
  BluetoothA2dpManager();

  void NotifyAudioManager(const nsAString& aAddress);

  BluetoothA2dpState mCurrentSinkState;
  nsString mDeviceAddress;
  bool mConnected;
  //For the reason HFP/A2DP usage switch, we need to force suspend A2DP
  nsAutoPtr<BluetoothTelephonyListener> mListener;
};

END_BLUETOOTH_NAMESPACE

#endif
