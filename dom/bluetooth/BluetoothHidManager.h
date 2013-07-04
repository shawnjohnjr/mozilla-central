/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_BluetoothHidmanager_h__
#define mozilla_dom_bluetooth_BluetoothHidmanager_h__

#include "BluetoothCommon.h"
#include "BluetoothProfileManagerBase.h"

BEGIN_BLUETOOTH_NAMESPACE

enum InputState {
    INPUT_DISCONNECTED = 1,
    INPUT_CONNECTING,
    INPUT_CONNECTED,
    INPUT_DISCONNECTING
};

class BluetoothHidManagerObserver;
class BluetoothValue;

class BluetoothHidManager : public BluetoothProfileManagerBase
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  static BluetoothHidManager* Get();
  ~BluetoothHidManager();

  bool Connect(const nsAString& aDeviceAddress);
  void Disconnect();

  virtual void OnGetServiceChannel(const nsAString& aDeviceAddress,
                                   const nsAString& aServiceUuid,
                                   int aChannel) MOZ_OVERRIDE;
  virtual void OnUpdateSdpRecords(const nsAString& aDeviceAddress) MOZ_OVERRIDE;
  virtual void GetAddress(nsAString& aDeviceAddress) MOZ_OVERRIDE;

  void HandleSinkPropertyChanged(const BluetoothSignal& aSignal);

private:
  BluetoothHidManager();
  bool Init();
  void Cleanup();

  void HandleShutdown();

  void NotifyStatusChanged();

  bool mConnected;
  nsString mDeviceAddress;
  InputState mInputState;
};

END_BLUETOOTH_NAMESPACE

#endif
