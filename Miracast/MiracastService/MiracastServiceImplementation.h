/*
* If not stated otherwise in this file or this component's LICENSE file the
* following copyright and licenses apply:
*
* Copyright 2025 RDK Management
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#pragma once

#include "Module.h"

#include <interfaces/Ids.h>
#include <interfaces/IMiracastService.h>
#include <interfaces/IMiracastPlayer.h>
#include <interfaces/IPowerManager.h>
#include<interfaces/IConfiguration.h>
#include <interfaces/ISystemServices.h>

#include <MiracastController.h>
#include "libIARM.h"

#include <com/com.h>
#include <core/core.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "libIBus.h"

#include "PowerManagerInterface.h"

using namespace WPEFramework;
using PowerState = WPEFramework::Exchange::IPowerManager::PowerState;
using MiracastPlayerState = WPEFramework::Exchange::IMiracastService::PlayerState;
using MiracastPlayerReasonCode = WPEFramework::Exchange::IMiracastService::PlayerReasonCode;
using MiracastServiceReasonCode = WPEFramework::Exchange::IMiracastService::ReasonCode;

typedef enum DeviceWiFiStates
{
    DEVICE_WIFI_STATE_UNINSTALLED = 0,
    DEVICE_WIFI_STATE_DISABLED = 1,
    DEVICE_WIFI_STATE_DISCONNECTED = 2,
    DEVICE_WIFI_STATE_PAIRING = 3,
    DEVICE_WIFI_STATE_CONNECTING = 4,
    DEVICE_WIFI_STATE_CONNECTED = 5,
    DEVICE_WIFI_STATE_FAILED = 6
}DEVICE_WIFI_STATES;

namespace WPEFramework
{
    namespace Plugin
    {		
        class MiracastServiceImplementation : public Exchange::IMiracastService, public Exchange::IConfiguration, public MiracastServiceNotifier
        {
            public:
                // We do not allow this plugin to be copied !!
                MiracastServiceImplementation();
                ~MiracastServiceImplementation() override;

                static MiracastServiceImplementation *instance(MiracastServiceImplementation *MiracastServiceImpl = nullptr);
                static MiracastController *m_miracast_ctrler_obj;

                // We do not allow this plugin to be copied !!
                MiracastServiceImplementation(const MiracastServiceImplementation &) = delete;
                MiracastServiceImplementation &operator=(const MiracastServiceImplementation &) = delete;

                virtual void onMiracastServiceClientConnectionRequest(string client_mac, string client_name) override;
                virtual void onMiracastServiceClientConnectionError(string client_mac, string client_name , MiracastServiceReasonCode reason_code ) override;
                virtual void onMiracastServiceLaunchRequest(string src_dev_ip, string src_dev_mac, string src_dev_name, string sink_dev_ip, bool is_connect_req_reported ) override;
                virtual void onStateChange(eMIRA_SERVICE_STATES state ) override;
                
                BEGIN_INTERFACE_MAP(MiracastServiceImplementation)
                INTERFACE_ENTRY(Exchange::IMiracastService)
                INTERFACE_ENTRY(Exchange::IConfiguration)
                END_INTERFACE_MAP

            public:
                enum Event
                {
                    MIRACASTSERVICE_EVENT_CLIENT_CONNECTION_REQUEST,
                    MIRACASTSERVICE_EVENT_CLIENT_CONNECTION_ERROR,
                    MIRACASTSERVICE_EVENT_PLAYER_LAUNCH_REQUEST
                };

                class EXTERNAL Job : public Core::IDispatch
                {
                    protected:
                        Job(MiracastServiceImplementation *MiracastServiceImplementation, Event event, JsonObject &params)
                            : _miracastServiceImplementation(MiracastServiceImplementation), _event(event), _params(params)
                        {
                            if (_miracastServiceImplementation != nullptr)
                            {
                                _miracastServiceImplementation->AddRef();
                            }
                        }

                    public:
                        Job() = delete;
                        Job(const Job &) = delete;
                        Job &operator=(const Job &) = delete;
                        ~Job()
                        {
                            if (_miracastServiceImplementation != nullptr)
                            {
                                _miracastServiceImplementation->Release();
                            }
                        }

                    public:
                        /* coverity[pass_by_value : FALSE] */
                        static Core::ProxyType<Core::IDispatch> Create(MiracastServiceImplementation *miracastServiceImplementation, Event event, JsonObject params)
                        {
        #ifndef USE_THUNDER_R4
                            return (Core::proxy_cast<Core::IDispatch>(Core::ProxyType<Job>::Create(miracastServiceImplementation, event, params)));
        #else
                            return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<Job>::Create(miracastServiceImplementation, event, params)));
        #endif
                        }

                        virtual void Dispatch()
                        {
                            _miracastServiceImplementation->Dispatch(_event, _params);
                        }

                    private:
                        MiracastServiceImplementation *_miracastServiceImplementation;
                        const Event _event;
                        JsonObject _params;
                }; // class Job

                class EXTERNAL PowerModeJob : public Core::IDispatch
                {
                    protected:
                        PowerModeJob(MiracastServiceImplementation *impl, PowerState newState, int transactionId)
                            : _impl(impl), _newState(newState), _transactionId(transactionId)
                        {
                            if (_impl != nullptr)
                            {
                                _impl->AddRef();
                            }
                        }

                    public:
                        PowerModeJob() = delete;
                        PowerModeJob(const PowerModeJob &) = delete;
                        PowerModeJob &operator=(const PowerModeJob &) = delete;
                        ~PowerModeJob()
                        {
                            if (_impl != nullptr)
                            {
                                _impl->Release();
                            }
                        }

                        static Core::ProxyType<Core::IDispatch> Create(MiracastServiceImplementation *impl, PowerState newState, int transactionId)
                        {
        #ifndef USE_THUNDER_R4
                            return (Core::proxy_cast<Core::IDispatch>(Core::ProxyType<PowerModeJob>::Create(impl, newState, transactionId)));
        #else
                            return (Core::ProxyType<Core::IDispatch>(Core::ProxyType<PowerModeJob>::Create(impl, newState, transactionId)));
        #endif
                        }

                        void Dispatch() override
                        {
                            _impl->setPowerStateInternal(_newState, true, _transactionId);
                        }

                    private:
                        MiracastServiceImplementation *_impl;
                        const PowerState _newState;
                        const int _transactionId;
                }; // class PowerModeJob

            public:
                uint32_t Configure(PluginHost::IShell* service) override;

                Core::hresult Register(Exchange::IMiracastService::INotification *notification) override;
                Core::hresult Unregister(Exchange::IMiracastService::INotification *notification) override;

                Core::hresult SetEnabled(const bool enabled , Result &returnPayload ) override;
                Core::hresult GetEnabled(bool &enabled , bool &success ) override;
                Core::hresult AcceptClientConnection(const string &requestStatus , Result &returnPayload ) override;
                Core::hresult StopClientConnection(const string &clientMac , const string &clientName, Result &returnPayload ) override;
                Core::hresult UpdatePlayerState(const string &clientMac , const MiracastPlayerState playerState , const int reasonCode , Result &returnPayload ) override;
                Core::hresult SetP2PBackendDiscovery(const bool enabled , Result &returnPayload ) override;

            private:
                class PowerManagerNotification : public Exchange::IPowerManager::IModePreChangeNotification
                {
                    private:
                        PowerManagerNotification(const PowerManagerNotification&) = delete;
                        PowerManagerNotification& operator=(const PowerManagerNotification&) = delete;
                    
                    public:
                        explicit PowerManagerNotification(MiracastServiceImplementation& parent)
                            : _parent(parent)
                        {
                        }
                        ~PowerManagerNotification() override = default;
                    
                    public:
                        void OnPowerModePreChange(const PowerState currentState, const PowerState newState, const int transactionId, const int stateChangeAfter) override
                        {
                            _parent.onPowerModePreChange(currentState, newState, transactionId, stateChangeAfter);
                        }

                        template <typename T>
                        T* baseInterface()
                        {
                            static_assert(std::is_base_of<T, PowerManagerNotification>(), "base type mismatch");
                            return static_cast<T*>(this);
                        }

                        BEGIN_INTERFACE_MAP(PowerManagerNotification)
                        INTERFACE_ENTRY(Exchange::IPowerManager::IModePreChangeNotification)
                        END_INTERFACE_MAP

                    private:
                        MiracastServiceImplementation& _parent;
                }; // class PowerManagerNotification

                class SystemServicesNotification : public Exchange::ISystemServices::INotification
                {
                    private:
                        SystemServicesNotification(const SystemServicesNotification&) = delete;
                        SystemServicesNotification& operator=(const SystemServicesNotification&) = delete;

                    public:
                        explicit SystemServicesNotification(MiracastServiceImplementation& parent)
                        : _parent(parent)
                        {
                        }
                        ~SystemServicesNotification() override = default;

                    public:
                        void OnFriendlyNameChanged(const string& friendlyName) override
                        {
                            MIRACASTLOG_INFO("OnFriendlyNameChanged [%s]", friendlyName.c_str());
                            _parent.onFriendlyNameUpdateHandler(friendlyName);
                        }

                        BEGIN_INTERFACE_MAP(SystemServicesNotification)
                        INTERFACE_ENTRY(Exchange::ISystemServices::INotification)
                        END_INTERFACE_MAP

                    private:
                        MiracastServiceImplementation& _parent;
                };

                class PlayerStopNotification : public Exchange::IMiracastPlayer::INotification
                {
                    private:
                        PlayerStopNotification(const PlayerStopNotification&) = delete;
                        PlayerStopNotification& operator=(const PlayerStopNotification&) = delete;

                    public:
                        explicit PlayerStopNotification(MiracastServiceImplementation& parent)
                        : _parent(parent)
                        {
                        }
                        ~PlayerStopNotification() override = default;

                    public:
                        void OnStateChange(const string &clientName, const string &clientMac, const Exchange::IMiracastPlayer::State playerState, const string &reasonCode, const Exchange::IMiracastPlayer::ReasonCode reasonDescription) override
                        {
                            if (Exchange::IMiracastPlayer::STATE_STOPPED == playerState)
                            {
                                _parent.onPlayerStopped();
                            }
                        }

                        BEGIN_INTERFACE_MAP(PlayerStopNotification)
                        INTERFACE_ENTRY(Exchange::IMiracastPlayer::INotification)
                        END_INTERFACE_MAP

                    private:
                        MiracastServiceImplementation& _parent;
                };

                mutable Core::CriticalSection _adminLock;
                std::mutex m_DiscoveryStateMutex;
                std::recursive_mutex m_EventMutex;
                std::string m_src_dev_ip{""};
                std::string m_src_dev_mac{""};
                std::string m_src_dev_name{""};
                std::string m_sink_dev_ip{""};
                std::mutex m_WiFiPluginMutex;
                WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement> *m_WiFiPluginObj = nullptr;
                std::list<Exchange::IMiracastService::INotification *> _miracastServiceNotification; // List of registered notifications
                PluginHost::IShell *m_CurrentService;
                guint m_FriendlyNameMonitorTimerID{0};
                guint m_WiFiConnectedStateMonitorTimerID{0};
                guint m_MiracastConnectionMonitorTimerID{0};
                eMIRA_SERVICE_STATES m_eService_state;
                bool m_isServiceInitialized{false};
                bool m_isServiceEnabled{false};

                std::atomic<bool> m_IsTransitionFromDeepSleep{false};
                std::atomic<bool> m_IsDeepSleepInProgress{false};
                uint32_t m_PowerPreChangeClientId{0};
                std::mutex m_PowerManagerMutex;
                std::mutex m_PowerTransitionMutex;
                std::mutex m_PowerDelayMonitorCtrlMutex;
                std::mutex m_PowerDelayMonitorMutex;
                std::condition_variable m_PowerDelayMonitorCv;
                bool m_PowerDelayMonitorDone{true};
                int m_PowerDelayMonitorTransactionId{0};
                std::thread m_PowerDelayMonitorThread;
                std::mutex m_PlayerStopMutex;
                std::condition_variable m_PlayerStopCv;
                bool m_PlayerStopped{false};
                Core::Sink<PlayerStopNotification> _playerStopNotification;

                void dispatchEvent(Event, const JsonObject &params);
                void Dispatch(Event event, const JsonObject &params);

                eMIRA_SERVICE_STATES getCurrentServiceState(void);
                void changeServiceState(eMIRA_SERVICE_STATES eService_state);
                bool envGetValue(const char *key, std::string &value);
                void getThunderPlugins(void);
                void subscribeWiFiEvents(void);
                void releaseWiFiPlugin(void);
                bool updateSystemFriendlyName();
                void setEnableInternal(bool isEnabled);

                void InitializePowerManager(PluginHost::IShell *service);
                void registerEventHandlers();
                void InitializePowerState();
                std::string getPowerStateString(PowerState pwrState);
                void setPowerStateInternal(PowerState pwrState, bool isPreChange = false, int transactionId = 0);
                void requestPowerModeChangeDelay(int transactionId, int delaySec);
                void startPowerDelayMonitor(int transactionId);
                void stopPowerDelayMonitor(int transactionId, bool force = false);
                void powerDelayMonitor(int transactionId);
                void stopActivePlayerSession(void);
                void onPlayerStopped(void);

                void setWiFiStateInternal(DEVICE_WIFI_STATES wifiState);

                void onFriendlyNameUpdateHandler(const string& friendlyName);
                void onWIFIStateChangedHandler(const JsonObject &parameters);

                void registerSystemEventHandlers();
                void unregisterSystemEventHandlers();
                void InitializeSystemServices(PluginHost::IShell* service);

                static gboolean monitor_friendly_name_timercallback(gpointer userdata);
                static gboolean monitor_wifi_connection_state_timercallback(gpointer userdata);
                static gboolean monitor_miracast_connection_timercallback(gpointer userdata);
                void remove_wifi_connection_state_timer(void);
                void remove_miracast_connection_timer(void);
                void remove_friendly_name_timer(void);

            public:
                static MiracastServiceImplementation *_instance;
                static PowerManagerInterfaceRef _powerManagerPlugin;
                Core::Sink<PowerManagerNotification> _pwrMgrNotification;
                bool _registeredEventHandlers;

                Exchange::ISystemServices* _systemServicesPlugin;
                Core::Sink<SystemServicesNotification> _systemServicesNotification;
                bool _registeredSystemEventHandlers;

                void onPowerModePreChange(const PowerState currentState, const PowerState newState, const int transactionId, const int stateChangeAfter);

                friend class Job;
                friend class PowerModeJob;
        }; // class MiracastServiceImplementation
    } // namespace Plugin
} // namespace WPEFramework
