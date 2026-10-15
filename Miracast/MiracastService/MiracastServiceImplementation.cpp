/**
 * If not stated otherwise in this file or this component's LICENSE
 * file the following copyright and licenses apply:
 *
 * Copyright 2023 RDK Management
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
 **/

#include <string> 

#include "MiracastServiceImplementation.h"

#include "UtilsJsonRpc.h"

#include "UtilsSynchroIarm.hpp"

using PowerState = WPEFramework::Exchange::IPowerManager::PowerState;

#define MIRACAST_DEVICE_PROPERTIES_FILE "/etc/device.properties"
#define SERVER_DETAILS "127.0.0.1:9998"
#define SYSTEM_CALLSIGN "org.rdk.System"
#define SYSTEM_CALLSIGN_VER SYSTEM_CALLSIGN ".1"
#define WIFI_CALLSIGN "org.rdk.Wifi"
#define WIFI_CALLSIGN_VER WIFI_CALLSIGN ".1"
#define SECURITY_TOKEN_LEN_MAX 1024
#define THUNDER_RPC_TIMEOUT 2000
#define MIRACAST_PLAYER_CALLSIGN "org.rdk.MiracastPlayer"
/* Mirrors STOP_REASON_APP_REQ_FOR_EXIT in MiracastPlayer/RTSP/MiracastRTSPMsg.h */
#define MIRACAST_PLAYER_STOP_REASON_APP_REQ_FOR_EXIT 300
#define POWER_PRECHANGE_CLIENT_NAME "MiracastService"
/* Initial reservation, sized to cover a normal teardown (player stop + P2P stop + Wi-Fi unsubscribe). */
#define DEEP_SLEEP_TEARDOWN_BUDGET_SEC 8
/* How long before the current reservation expires the monitor checks whether more time is needed. */
#define DEEP_SLEEP_DELAY_RENEW_MARGIN_SEC 1
/* Extra time added per extension when teardown is still running. */
#define DEEP_SLEEP_DELAY_EXTENSION_SEC 2
/* Upper bound so a hung teardown cannot hold off deep sleep indefinitely. */
#define DEEP_SLEEP_MAX_DELAY_SEC 30
#define DEEP_SLEEP_PLAYER_STOP_TIMEOUT_MS 3000
#define DEEP_SLEEP_CONTROLLER_STOP_TIMEOUT_MS 4000

static PowerState m_powerState = WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY;
static bool m_IsWiFiConnectingState = false;

namespace WPEFramework
{
    namespace Plugin
    {
        SERVICE_REGISTRATION(MiracastServiceImplementation, MIRACAST_SERVICE_API_VERSION_NUMBER_MAJOR, MIRACAST_SERVICE_API_VERSION_NUMBER_MINOR);
        
        MiracastServiceImplementation *MiracastServiceImplementation::_instance = nullptr;
        PowerManagerInterfaceRef MiracastServiceImplementation::_powerManagerPlugin;
        MiracastController *MiracastServiceImplementation::m_miracast_ctrler_obj = nullptr;
    
        MiracastServiceImplementation::MiracastServiceImplementation()
        : _adminLock(),
        _playerStopNotification(*this),
        _pwrMgrNotification(*this),
        _systemServicesPlugin(nullptr),
        _systemServicesNotification(*this)
        {
            LOGINFO("Create MiracastServiceImplementation Instance");
            MiracastServiceImplementation::_instance = this;
            MIRACAST::logger_init("MiracastService");
            m_isServiceInitialized = false;
            _registeredEventHandlers = false;
            _registeredSystemEventHandlers = false;
            m_eService_state = MIRACAST_SERVICE_STATE_IDLE;
            m_CurrentService = nullptr;
            m_isServiceEnabled = false;
        }

        MiracastServiceImplementation::~MiracastServiceImplementation()
        {
            stopPowerDelayMonitor(0, true);
        }

        /**
         * Register a notification callback
         */
        Core::hresult MiracastServiceImplementation::Register(Exchange::IMiracastService::INotification *notification)
        {
            MIRACASTLOG_TRACE("Entering ...");
            ASSERT(nullptr != notification);

            MIRACASTLOG_INFO("Register notification: %p", notification);
            _adminLock.Lock();

            // Make sure we can't register the same notification callback multiple times
            if (std::find(_miracastServiceNotification.begin(), _miracastServiceNotification.end(), notification) == _miracastServiceNotification.end())
            {
                _miracastServiceNotification.push_back(notification);
                notification->AddRef();
            }
            else
            {
                MIRACASTLOG_ERROR("same notification is registered already");
            }

            _adminLock.Unlock();
            MIRACASTLOG_TRACE("Exiting ...");
            return Core::ERROR_NONE;
        }

        /**
         * Unregister a notification callback
         */
        Core::hresult MiracastServiceImplementation::Unregister(Exchange::IMiracastService::INotification *notification)
        {
            MIRACASTLOG_TRACE("Entering ...");
            Core::hresult status = Core::ERROR_GENERAL;

            ASSERT(nullptr != notification);

            _adminLock.Lock();

            // we just unregister one notification once
            auto itr = std::find(_miracastServiceNotification.begin(), _miracastServiceNotification.end(), notification);
            if (itr != _miracastServiceNotification.end())
            {
                (*itr)->Release();
                MIRACASTLOG_INFO("Unregister notification");
                _miracastServiceNotification.erase(itr);
                status = Core::ERROR_NONE;
            }
            else
            {
                MIRACASTLOG_ERROR("notification not found");
            }

            _adminLock.Unlock();
            MIRACASTLOG_TRACE("Exiting ...");
            return status;
        }

        /*  Helper methods Start */
        /* ------------------------------------------------------------------------------------------------------- */
        eMIRA_SERVICE_STATES MiracastServiceImplementation::getCurrentServiceState(void)
        {
            MIRACASTLOG_INFO("current state [%#08X]",m_eService_state);
            return m_eService_state;
        }

        void MiracastServiceImplementation::changeServiceState(eMIRA_SERVICE_STATES eService_state)
        {
            eMIRA_SERVICE_STATES old_state = m_eService_state,
                                new_state = eService_state;
            m_eService_state = eService_state;
            MIRACASTLOG_INFO("changing state [%#08X] -> [%#08X]",old_state,new_state);
        }

        bool MiracastServiceImplementation::envGetValue(const char *key, std::string &value)
        {
            std::ifstream fs(MIRACAST_DEVICE_PROPERTIES_FILE, std::ifstream::in);
            std::string::size_type delimpos;
            std::string line;
            if (!fs.fail())
            {
                while (std::getline(fs, line))
                {
                    if (!line.empty() && ((delimpos = line.find('=')) > 0))
                    {
                        std::string itemKey = line.substr(0, delimpos);
                        if (itemKey == key)
                        {
                            value = line.substr(delimpos + 1, std::string::npos);
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        void MiracastServiceImplementation::getThunderPlugins(void)
        {
            MIRACASTLOG_TRACE("Entering ...");
            if (nullptr == m_CurrentService)
            {
                MIRACASTLOG_ERROR("m_CurrentService is nullptr, cannot get Thunder plugins");
                return;
            }
            
            if (nullptr == m_WiFiPluginObj)
            {
                string token;
                // TODO: use interfaces and remove token
                auto security = m_CurrentService->QueryInterfaceByCallsign<PluginHost::IAuthenticate>("SecurityAgent");
                if (nullptr != security)
                {
                    string payload = "http://localhost";
                    if (security->CreateToken(static_cast<uint16_t>(payload.length()),
                                            reinterpret_cast<const uint8_t *>(payload.c_str()),
                                            token) == Core::ERROR_NONE)
                    {
                        MIRACASTLOG_INFO("got security token");
                    }
                    else
                    {
                        MIRACASTLOG_ERROR("failed to get security token");
                    }
                    security->Release();
                }
                else
                {
                    MIRACASTLOG_WARNING("No security agent");
                }

                string query = "token=" + token;
                Core::SystemInfo::SetEnvironment(_T("THUNDER_ACCESS"), (_T(SERVER_DETAILS)));
                m_WiFiPluginObj = new WPEFramework::JSONRPC::LinkType<Core::JSON::IElement>(_T(WIFI_CALLSIGN_VER), (_T("MiracastService")), false, query);
                if (nullptr == m_WiFiPluginObj)
                {
                    MIRACASTLOG_ERROR("JSONRPC: %s: initialization failed", WIFI_CALLSIGN_VER);
                }
                else
                {
                    MIRACASTLOG_INFO("JSONRPC: %s: initialization ok", WIFI_CALLSIGN_VER);
                }
            }
            MIRACASTLOG_TRACE("Exiting ...");
        }

        void MiracastServiceImplementation::subscribeWiFiEvents(void)
        {
            lock_guard<mutex> lock(m_WiFiPluginMutex);
            if (nullptr != m_WiFiPluginObj)
            {
                return;
            }
            getThunderPlugins();
            if (nullptr != m_WiFiPluginObj)
            {
                m_WiFiPluginObj->Subscribe<JsonObject>(1000, "onWIFIStateChanged", &MiracastServiceImplementation::onWIFIStateChangedHandler, this);
            }
        }

        void MiracastServiceImplementation::releaseWiFiPlugin(void)
        {
            lock_guard<mutex> lock(m_WiFiPluginMutex);
            if (m_WiFiPluginObj)
            {
                m_WiFiPluginObj->Unsubscribe(1000, _T("onWIFIStateChanged"));
                delete m_WiFiPluginObj;
                m_WiFiPluginObj = nullptr;
            }
        }

        bool MiracastServiceImplementation::updateSystemFriendlyName()
        {
            bool success = false;
            string friendlyName;
            MIRACASTLOG_TRACE("Entering ...");


            if (nullptr == _systemServicesPlugin)
            {
                MIRACASTLOG_ERROR("_systemServicesPlugin not yet instantiated");
                return false;
            }

            auto ret = _systemServicesPlugin->GetFriendlyName(friendlyName, success);
            if (Core::ERROR_NONE == ret)
            {
                if (success)
                {
                    bool isServiceEnabled;
                    {
                        lock_guard<recursive_mutex> lock(m_EventMutex);
                        isServiceEnabled = m_isServiceEnabled;
                    }
                    m_miracast_ctrler_obj->set_FriendlyName(friendlyName,isServiceEnabled);
                    MIRACASTLOG_INFO("Miracast FriendlyName=%s", friendlyName.c_str());
                    success = true;
                }
                else
                {
                    ret = Core::ERROR_GENERAL;
                    MIRACASTLOG_ERROR("updateSystemFriendlyName call failed");
                }
            }
            else
            {
                MIRACASTLOG_ERROR("updateSystemFriendlyName call failed E[%u]", ret);
            }
            return success;
        }

        void MiracastServiceImplementation::setEnableInternal(bool isEnabled)
        {
            MIRACASTLOG_TRACE("Entering ...");
            if ( nullptr != m_miracast_ctrler_obj )
            {
                m_miracast_ctrler_obj->set_enable(isEnabled);
            }
            MIRACASTLOG_TRACE("Exiting ...");
        }
        /*  Helper and Internal methods End */
        /* ------------------------------------------------------------------------------------------------------- */

        void MiracastServiceImplementation::dispatchEvent(Event event, const JsonObject &params)
        {
            Core::IWorkerPool::Instance().Submit(Job::Create(this, event, params));
        }

        void MiracastServiceImplementation::Dispatch(Event event,const JsonObject& params)
        {
            _adminLock.Lock();

            std::list<Exchange::IMiracastService::INotification *>::const_iterator index(_miracastServiceNotification.begin());

            switch (event)
            {
                case MIRACASTSERVICE_EVENT_CLIENT_CONNECTION_REQUEST:
                {
                    string clientMac = "";
                    string clientName = "";

                    if (!(params.HasLabel("source_dev_mac") && !(clientMac = params["source_dev_mac"].String()).empty()))
                    {
                        MIRACASTLOG_ERROR("source_dev_mac not present or empty");
                    }
                    else if (!(params.HasLabel("source_dev_name") && !(clientName = params["source_dev_name"].String()).empty()))
                    {
                        MIRACASTLOG_ERROR("source_dev_name not present or empty");
                    }
                    else
                    {
                        MIRACASTLOG_INFO("Notifying CLIENT_CONNECTION_REQUEST Event ClientMac[%s] ClientName[%s]", clientMac.c_str(), clientName.c_str());
                        while (index != _miracastServiceNotification.end())
                        {
                            (*index)->OnClientConnectionRequest(clientMac , clientName );
                            ++index;
                        }
                    }
                }
                break;
                case MIRACASTSERVICE_EVENT_CLIENT_CONNECTION_ERROR:
                {
                    string clientMac = "";
                    string clientName = "";
                    string reasonCodeStr = "";
                    MiracastServiceReasonCode reason = WPEFramework::Exchange::IMiracastService::REASON_CODE_SUCCESS;

                    if (!(params.HasLabel("source_dev_mac") && !(clientMac = params["source_dev_mac"].String()).empty()))
                    {
                        MIRACASTLOG_ERROR("source_dev_mac not present or empty");
                    }
                    else if (!(params.HasLabel("source_dev_name") && !(clientName = params["source_dev_name"].String()).empty()))
                    {
                        MIRACASTLOG_ERROR("source_dev_name not present or empty");
                    }
                    else if (!(params.HasLabel("reason_code")))
                    {
                        MIRACASTLOG_ERROR("reason_code not present or empty");
                    }
                    else
                    {
                        reason = static_cast<MiracastServiceReasonCode>(params["reason_code"].Number());
                        reasonCodeStr = std::to_string(reason);
                        MIRACASTLOG_INFO("Notifying CLIENT_CONNECTION_ERROR Event Mac[%s] Name[%s] Reason[%u]",clientMac.c_str(), clientName.c_str(), reason);
                        while (index != _miracastServiceNotification.end())
                        {
                            (*index)->OnClientConnectionError(clientMac , clientName , reasonCodeStr , reason);
                            ++index;
                        }
                    }
                }
                break;
                case MIRACASTSERVICE_EVENT_PLAYER_LAUNCH_REQUEST:
                {
                    DeviceParameters deviceParameters;
                    if (!(params.HasLabel("source_dev_mac") && !(deviceParameters.sourceDeviceMac = params["source_dev_mac"].String()).empty()))
                    {
                        MIRACASTLOG_ERROR("source_dev_mac not present or empty");
                    }
                    else if (!(params.HasLabel("source_dev_name") && !(deviceParameters.sourceDeviceName = params["source_dev_name"].String()).empty()))
                    {
                        MIRACASTLOG_ERROR("source_dev_name not present or empty");
                    }
                    else if (!(params.HasLabel("source_dev_ip") && !(deviceParameters.sourceDeviceIP = params["source_dev_ip"].String()).empty()))
                    {
                        MIRACASTLOG_ERROR("source_dev_ip not present or empty");
                    }
                    else if (!(params.HasLabel("sink_dev_ip") && !(deviceParameters.sinkDeviceIP = params["sink_dev_ip"].String()).empty()))
                    {
                        MIRACASTLOG_ERROR("sink_dev_ip not present or empty");
                    }
                    else
                    {
                        MIRACASTLOG_INFO("Notifying PLAYER_LAUNCH_REQUEST Event SourceDeviceIP[%s] SourceDeviceMac[%s] SourceDeviceName[%s] SinkDeviceIP[%s]",
                                        deviceParameters.sourceDeviceIP.c_str(), deviceParameters.sourceDeviceMac.c_str(),
                                        deviceParameters.sourceDeviceName.c_str(), deviceParameters.sinkDeviceIP.c_str());
                        while (index != _miracastServiceNotification.end())
                        {
                            (*index)->OnLaunchRequest(deviceParameters);
                            ++index;
                        }
                    }
                }
                break;
                default:
                {
                    MIRACASTLOG_WARNING("Event[%u] not handled", event);
                }
                break;
            }
            _adminLock.Unlock();
        }

        /*  COMRPC Methods Start */
        /* ------------------------------------------------------------------------------------------------------- */
        uint32_t MiracastServiceImplementation::Configure(PluginHost::IShell *service)
        {
            MIRACASTLOG_TRACE("Entering ...");
            uint32_t result = Core::ERROR_GENERAL;
			

	    	if ((m_CurrentService) && (nullptr == service))
            {
				MIRACASTLOG_INFO("MiracastServiceImplementation::Configure deinitialize");
                MIRACASTLOG_TRACE("Call MiracastServiceImplementation deinitialize");
            	{
                    lock_guard<mutex> lck(m_PowerManagerMutex);
                    if (_powerManagerPlugin)
                    {
                        // Must disengage from pre-change before unregistering the pre-change notification.
                        if (0 != m_PowerPreChangeClientId)
                        {
                            _powerManagerPlugin->RemovePowerModePreChangeClient(m_PowerPreChangeClientId);
                            m_PowerPreChangeClientId = 0;
                        }
                        _powerManagerPlugin->Unregister(_pwrMgrNotification.baseInterface<Exchange::IPowerManager::IModePreChangeNotification>());
                        _powerManagerPlugin.Reset();
                    }
                    _registeredEventHandlers = false;
                }
                stopPowerDelayMonitor(0, true);

            	{
                    // Serialise with any in-flight power transition job, which may re-arm timers or use the controller.
                    lock_guard<mutex> lck(m_DiscoveryStateMutex);
                    remove_friendly_name_timer();
                    remove_wifi_connection_state_timer();
                    remove_miracast_connection_timer();
                    if (m_isServiceInitialized)
                    {
                        MiracastController::destroyInstance();
                        m_CurrentService = nullptr;
                        m_miracast_ctrler_obj = nullptr;
                        m_isServiceInitialized = false;
                        lock_guard<recursive_mutex> lock(m_EventMutex);
                        m_isServiceEnabled = false;
                        MIRACASTLOG_INFO("Done..!!!");
                    }
                    if(m_CurrentService)
                    {
                        m_CurrentService->Release();
                        m_CurrentService = nullptr;
                    }
                }

                // Released after the controller block so a racing resume job cannot re-create them.
            	releaseWiFiPlugin();

                if (_systemServicesPlugin)
            	{
					if (_registeredSystemEventHandlers) {
                        _systemServicesPlugin->Unregister(&_systemServicesNotification);
                        _registeredSystemEventHandlers = false;
					}
                    _systemServicesPlugin->Release();
                    _systemServicesPlugin = nullptr;
            	}
            	MIRACASTLOG_INFO("Disconnect from the COM-RPC socket");

            	MIRACAST::logger_deinit();
            	MiracastServiceImplementation::_instance = nullptr;
            	result = Core::ERROR_NONE;
            }
            else if ((service) && ( nullptr == m_CurrentService ))
            {
                m_CurrentService = service;
				MIRACASTLOG_TRACE(" MiracastServiceImplementation initialize...");
				MIRACASTLOG_INFO("MiracastServiceImplementation::Configure initialize");

            	if (nullptr != m_CurrentService)
            	{
                	m_CurrentService->AddRef();
                	string	p2p_ctrl_iface = "";

                if (!(envGetValue("WIFI_P2P_CTRL_INTERFACE", p2p_ctrl_iface)))
                {
                    MIRACASTLOG_ERROR("WIFI_P2P_CTRL_INTERFACE not configured in device properties file");
                    MIRACASTLOG_TRACE("Exiting ...");
                    return Core::ERROR_GENERAL;
                }

                if (!m_isServiceInitialized)
                {
                    MiracastError ret_code = MIRACAST_OK;

                    InitializePowerManager(service);
                    InitializePowerState();

                    m_miracast_ctrler_obj = MiracastController::getInstance(ret_code, this,std::move(p2p_ctrl_iface));
                    if (nullptr != m_miracast_ctrler_obj)
                    {
                        InitializeSystemServices(service);
                        subscribeWiFiEvents();

                        if ( false == updateSystemFriendlyName())
                        {
                            m_FriendlyNameMonitorTimerID = g_timeout_add(2000, MiracastServiceImplementation::monitor_friendly_name_timercallback, this);
                            MIRACASTLOG_WARNING("Unable to get friendlyName, requires polling [%u]...",m_FriendlyNameMonitorTimerID);
                        }
                        else
                        {
                            MIRACASTLOG_INFO("friendlyName updated properly...");
                        }
                        m_isServiceInitialized = true;
                        result = Core::ERROR_NONE;
                    }
                    else
                    {
                        switch (ret_code)
                        {
                            case MIRACAST_INVALID_P2P_CTRL_IFACE:
                            {
                                MIRACASTLOG_ERROR("Invalid P2P Ctrl iface configured");
                            }
                            break;
                            case MIRACAST_CONTROLLER_INIT_FAILED:
                            {
                                MIRACASTLOG_ERROR("Controller Init Failed");
                            }
                            break;
                            case MIRACAST_P2P_INIT_FAILED:
                            {
                                MIRACASTLOG_ERROR("P2P Init Failed");
                            }
                            break;
                            default:
                            {
                                MIRACASTLOG_ERROR("Unknown Error:Failed to obtain MiracastController Object");
                            }
                            break;
                        }
                    }
                }
            }
	        }
			else
			{
                ASSERT(nullptr != service);
            }   
            MIRACASTLOG_TRACE("Exiting ...");
            return result;
        }

        Core::hresult MiracastServiceImplementation::SetEnabled(const bool enabled , Result &result )
        {
            MIRACASTLOG_TRACE("Entering ...");
            bool isSuccessOrFailure = false;
            MIRACASTLOG_INFO("SetEnabled called with [%s]", enabled ? "true" : "false");
            lock_guard<mutex> lck(m_DiscoveryStateMutex);
            eMIRA_SERVICE_STATES current_state = getCurrentServiceState();
            if (enabled)
            {
                lock_guard<recursive_mutex> lock(m_EventMutex);
                if (!m_isServiceEnabled)
                {
                    m_isServiceEnabled = true;
                    if (m_IsDeepSleepInProgress)
                    {
                        MIRACASTLOG_INFO("#### MCAST-TRIAGE-OK Deep sleep in progress, discovery deferred until resume");
                    }
                    else if (m_IsTransitionFromDeepSleep.exchange(false))
                    {
                        MIRACASTLOG_INFO("#### MCAST-TRIAGE-OK Enable Miracast discovery Async");
                        m_miracast_ctrler_obj->restart_discoveryAsync();
                    }
                    else
                    {
                        setEnableInternal(true);
                    }
                    result.message = "Successfully enabled the WFD Discovery";
                    isSuccessOrFailure = true;
                }
                else
                {
                    result.message = "WFD Discovery already enabled.";
                }
            }
            else
            {
                if ( MIRACAST_SERVICE_STATE_PLAYER_LAUNCHED == current_state )
                {
                    result.message = "Failed as MiracastPlayer already Launched";
                }
                else
                {
                    lock_guard<recursive_mutex> lock(m_EventMutex);
                    if (m_isServiceEnabled)
                    {
                        m_isServiceEnabled = false;
                        if (!m_IsTransitionFromDeepSleep)
                        {
                            if ( MIRACAST_SERVICE_STATE_RESTARTING_SESSION == current_state )
                            {
                                m_miracast_ctrler_obj->stop_discoveryAsync();
                            }
                            else
                            {
                                setEnableInternal(false);
                            }
                        }
                        else
                        {
                            MIRACASTLOG_INFO("#### MCAST-TRIAGE-OK Skipping Disable discovery as done by PwrMgr");
                        }
                        remove_wifi_connection_state_timer();
                        remove_miracast_connection_timer();
                        isSuccessOrFailure = true;
                        result.message = "Successfully disabled the WFD Discovery";
                    }
                    else
                    {
                        result.message = "WFD Discovery already disabled.";
                    }
                }
            }
            result.success = isSuccessOrFailure;
            MIRACASTLOG_TRACE("Exiting ...");
            return Core::ERROR_NONE;
        }

        Core::hresult MiracastServiceImplementation::GetEnabled(bool &enabled , bool &success )
        {
            MIRACASTLOG_TRACE("Entering ...");
            {
                lock_guard<recursive_mutex> lock(m_EventMutex);
                enabled = m_isServiceEnabled;
            }
            success = true;
            MIRACASTLOG_TRACE("Exiting ...");
            return Core::ERROR_NONE;
        }

        Core::hresult MiracastServiceImplementation::AcceptClientConnection(const string &requestStatus , Result &result )
        {
            MIRACASTLOG_TRACE("Entering ...");
            bool isSuccessOrFailure = false;
            if (("Accept" == requestStatus) || ("Reject" == requestStatus))
            {
                lock_guard<recursive_mutex> lock(m_EventMutex);
                eMIRA_SERVICE_STATES current_state = getCurrentServiceState();
                isSuccessOrFailure = true;

                remove_miracast_connection_timer();

                if ( MIRACAST_SERVICE_STATE_DIRECT_LAUCH_WITH_CONNECTING == current_state)
                {
                    if ("Accept" == requestStatus)
                    {
                        MIRACASTLOG_INFO("#### Notifying Launch Request ####");
                        m_miracast_ctrler_obj->switch_launch_request_context(m_src_dev_ip, m_src_dev_mac, m_src_dev_name, m_sink_dev_ip );
                        changeServiceState(MIRACAST_SERVICE_STATE_CONNECTION_ACCEPTED);
                    }
                    else
                    {
                        changeServiceState(MIRACAST_SERVICE_STATE_CONNECTION_REJECTED);
                        m_miracast_ctrler_obj->restart_session_discovery(m_src_dev_mac);
                        changeServiceState(MIRACAST_SERVICE_STATE_RESTARTING_SESSION);
                        MIRACASTLOG_INFO("#### Refreshing the Session ####");
                    }
                    m_src_dev_ip.clear();
                    m_src_dev_mac.clear();
                    m_src_dev_name.clear();
                    m_sink_dev_ip.clear();
                }
                else
                {
                    if ( MIRACAST_SERVICE_STATE_CONNECTING == current_state )
                    {
                        m_miracast_ctrler_obj->accept_client_connection(requestStatus);
                        if ("Accept" == requestStatus)
                        {
                            changeServiceState(MIRACAST_SERVICE_STATE_CONNECTION_ACCEPTED);
                        }
                        else
                        {
                            changeServiceState(MIRACAST_SERVICE_STATE_CONNECTION_REJECTED);
                        }
                    }
                    else
                    {
                        MIRACASTLOG_INFO("Ignoring '%s' as Session already Refreshed and Current State[%#08X]",requestStatus.c_str(),current_state);
                    }
                }
            }
            else
            {
                result.message = "Supported 'requestStatus' parameter values are Accept or Reject";
                MIRACASTLOG_ERROR("Unsupported param passed [%s]", requestStatus.c_str());
            }
            result.success = isSuccessOrFailure;
            MIRACASTLOG_TRACE("Exiting ...");
            return Core::ERROR_NONE;
        }

        Core::hresult MiracastServiceImplementation::StopClientConnection(const string &clientMac , const string &clientName, Result &result )
        {
            MIRACASTLOG_TRACE("Entering ...");
            bool isSuccessOrFailure = false;
            MIRACASTLOG_INFO("clientMac[%s] clientName[%s]", clientMac.c_str(), clientName.c_str());
            lock_guard<recursive_mutex> lock(m_EventMutex);
            eMIRA_SERVICE_STATES current_state = getCurrentServiceState();

            if (( MIRACAST_SERVICE_STATE_PLAYER_LAUNCHED == current_state )||
                ( MIRACAST_SERVICE_STATE_CONNECTION_ACCEPTED != current_state ))
            {
                MIRACASTLOG_WARNING("Invalid state to process stopClientConnection [%#08X]", current_state);
                result.message = "Invalid state to process stopClientConnection";
            }
            else
            {
                if ((( 0 == clientName.compare(m_miracast_ctrler_obj->get_WFDSourceName())) &&
                    ( 0 == clientMac.compare(m_miracast_ctrler_obj->get_WFDSourceMACAddress())))||
                    (( 0 == clientName.compare(m_miracast_ctrler_obj->get_NewSourceName())) &&
                    ( 0 == clientMac.compare(m_miracast_ctrler_obj->get_NewSourceMACAddress()))))
                {
                    std::string cached_mac_address = "";
                    if ( 0 == clientMac.compare(m_miracast_ctrler_obj->get_NewSourceMACAddress()))
                    {
                        cached_mac_address = clientMac;
                    }

                    changeServiceState(MIRACAST_SERVICE_STATE_APP_REQ_TO_ABORT_CONNECTION);
                    m_miracast_ctrler_obj->restart_session_discovery(cached_mac_address);
                    changeServiceState(MIRACAST_SERVICE_STATE_RESTARTING_SESSION);
                    isSuccessOrFailure = true;
                }
                else
                {
                    result.message = "Invalid MAC and Name";
                    MIRACASTLOG_ERROR("Invalid MAC and Name[%s][%s]..!!!",clientMac.c_str(),clientName.c_str());
                }
            }
            MIRACASTLOG_TRACE("Exiting ...");
            result.success = isSuccessOrFailure;
            return Core::ERROR_NONE;
        }

        Core::hresult MiracastServiceImplementation::UpdatePlayerState(const string &clientMac , const MiracastPlayerState playerState , const int reasonCode , Result &result )
        {
            MIRACASTLOG_TRACE("Entering ...");
            bool restart_discovery_needed = false;
            switch (playerState)
            {
                case WPEFramework::Exchange::IMiracastService::PLAYER_STATE_IDLE:
                case WPEFramework::Exchange::IMiracastService::PLAYER_STATE_INITIATED:
                case WPEFramework::Exchange::IMiracastService::PLAYER_STATE_INPROGRESS:
                case WPEFramework::Exchange::IMiracastService::PLAYER_STATE_PLAYING:
                case WPEFramework::Exchange::IMiracastService::PLAYER_STATE_STOPPED:
                {
                    MIRACASTLOG_INFO("#### clientMac[%s] playerState[%d] reasonCode[%d] ####", clientMac.c_str(), (int)playerState, (int)reasonCode);

                    if (WPEFramework::Exchange::IMiracastService::PLAYER_STATE_STOPPED == playerState)
                    {
                        onPlayerStopped();

                        MiracastPlayerReasonCode playerReasonCode = static_cast<MiracastPlayerReasonCode>(reasonCode);
                        if (WPEFramework::Exchange::IMiracastService::PLAYER_REASON_CODE_NEW_SRC_DEV_CONNECT_REQ == playerReasonCode )
                        {
                            MIRACASTLOG_INFO("!!! STOPPED RECEIVED FOR NEW CONECTION !!!");
                            m_miracast_ctrler_obj->flush_current_session();
                        }
                        else
                        {
                            restart_discovery_needed = true;
                            if ( WPEFramework::Exchange::IMiracastService::PLAYER_REASON_CODE_APP_REQ_TO_STOP == playerReasonCode )
                            {
                                MIRACASTLOG_INFO("!!! STOPPED RECEIVED FOR ON EXIT !!!");
                            }
                            else if ( WPEFramework::Exchange::IMiracastService::PLAYER_REASON_CODE_SRC_DEV_REQ_TO_STOP == playerReasonCode )
                            {
                                MIRACASTLOG_INFO("!!! SRC DEV TEARDOWN THE CONNECTION !!!");
                            }
                            else
                            {
                                MIRACASTLOG_ERROR("!!! STOPPED RECEIVED FOR REASON[%d] !!!",playerReasonCode);
                            }
                        }
                    }
                }
                break;
                default:
                {
                    result.message = "Invalid Player State";
                    MIRACASTLOG_ERROR("Invalid Player State[%#04X]", (int)playerState);
                    return Core::ERROR_BAD_REQUEST;
                }
            }

            bool isServiceEnabled;
            {
                lock_guard<recursive_mutex> lock(m_EventMutex);
                isServiceEnabled = m_isServiceEnabled;
            }
            if ( isServiceEnabled && restart_discovery_needed )
            {
                if (m_IsDeepSleepInProgress)
                {
                    MIRACASTLOG_INFO("Deep sleep in progress, discovery will be restarted on resume");
                }
                else
                {
                    // It will restart the discovering
                    m_miracast_ctrler_obj->restart_session_discovery(clientMac);
                    changeServiceState(MIRACAST_SERVICE_STATE_RESTARTING_SESSION);
                }
            }
            MIRACASTLOG_INFO("#### MiracastPlayerState[%d] reasonCode[%#04X] ####", (int)playerState, (int)reasonCode);
            MIRACASTLOG_TRACE("Exiting ...");
            result.success = true;
            return Core::ERROR_NONE;
        }

        Core::hresult MiracastServiceImplementation::SetP2PBackendDiscovery(const bool enabled , Result &result )
        {
            MIRACASTLOG_TRACE("Entering ...");
            if (m_miracast_ctrler_obj)
            {
                m_miracast_ctrler_obj->setP2PBackendDiscovery(enabled);
            }
            MIRACASTLOG_TRACE("Exiting ...");
            result.success = true;
            return Core::ERROR_NONE;
        }
        /*  COMRPC Methods End */
        /* ------------------------------------------------------------------------------------------------------- */

        /*  Events Start */
        /* ------------------------------------------------------------------------------------------------------- */
        void MiracastServiceImplementation::onMiracastServiceClientConnectionRequest(string client_mac, string client_name)
        {
            MIRACASTLOG_TRACE("Entering ...");
            bool is_another_connect_request = false;

            lock_guard<recursive_mutex> lock(m_EventMutex);
            eMIRA_SERVICE_STATES current_state = getCurrentServiceState();

            if ( MIRACAST_SERVICE_STATE_PLAYER_LAUNCHED == current_state )
            {
                is_another_connect_request = true;
                MIRACASTLOG_WARNING("Another Connect Request received while casting");
            }

            if (0 == access("/opt/miracast_autoconnect", F_OK))
            {
                char commandBuffer[768] = {0};

                if ( is_another_connect_request )
                {
                    MIRACASTLOG_INFO("!!! NEED TO STOP ONGOING SESSION !!!");
                    strncpy(commandBuffer,"curl -H \"Authorization: Bearer `WPEFrameworkSecurityUtility | cut -d '\"' -f 4`\" --header \"Content-Type: application/json\" --request POST --data '{\"jsonrpc\":\"2.0\", \"id\":3,\"method\":\"org.rdk.MiracastPlayer.1.stopRequest\", \"params\":{\"reason\": \"NEW_CONNECTION\"}}' http://127.0.0.1:9998/jsonrpc &",sizeof(commandBuffer));
                    commandBuffer[sizeof(commandBuffer) - 1] = '\0';
                    MIRACASTLOG_INFO("Stopping old Session by [%s]",commandBuffer);
                    MiracastCommon::execute_SystemCommand(commandBuffer);
                }
                if (MIRACAST_SERVICE_STATE_DIRECT_LAUCH_REQUESTED == current_state)
                {
                    changeServiceState(MIRACAST_SERVICE_STATE_DIRECT_LAUCH_WITH_CONNECTING);
                }
                else
                {
                    changeServiceState(MIRACAST_SERVICE_STATE_CONNECTING);
                }
                memset(commandBuffer,0x00,sizeof(commandBuffer));
                strncpy(commandBuffer,"curl -H \"Authorization: Bearer `WPEFrameworkSecurityUtility | cut -d '\"' -f 4`\" --header \"Content-Type: application/json\" --request POST --data '{\"jsonrpc\":\"2.0\", \"id\":3,\"method\":\"org.rdk.MiracastService.1.acceptClientConnection\", \"params\":{\"requestStatus\": \"Accept\"}}' http://127.0.0.1:9998/jsonrpc &",sizeof(commandBuffer));
                commandBuffer[sizeof(commandBuffer) - 1] = '\0';
                MIRACASTLOG_INFO("AutoConnecting [%s - %s] by [%s]",client_name.c_str(),client_mac.c_str(),commandBuffer);
                MiracastCommon::execute_SystemCommand(commandBuffer);
            }
            else
            {
                JsonObject eventDetails;
                eventDetails["source_dev_mac"] = client_mac;
                eventDetails["source_dev_name"] = client_name;
                dispatchEvent(MIRACASTSERVICE_EVENT_CLIENT_CONNECTION_REQUEST, eventDetails);

                m_src_dev_mac = std::move(client_mac);

                if (MIRACAST_SERVICE_STATE_DIRECT_LAUCH_REQUESTED == current_state)
                {
                    changeServiceState(MIRACAST_SERVICE_STATE_DIRECT_LAUCH_WITH_CONNECTING);
                }
                else
                {
                    changeServiceState(MIRACAST_SERVICE_STATE_CONNECTING);
                }
                m_MiracastConnectionMonitorTimerID = g_timeout_add(40000, MiracastServiceImplementation::monitor_miracast_connection_timercallback, this);
                MIRACASTLOG_INFO("Timer created to Monitor Miracast Connection Status [%u]",m_MiracastConnectionMonitorTimerID);
            }
            MIRACASTLOG_TRACE("Exiting ...");
        }

        void MiracastServiceImplementation::onMiracastServiceClientConnectionError(string client_mac, string client_name , MiracastServiceReasonCode reason_code )
        {
            MIRACASTLOG_TRACE("Entering ...");
            lock_guard<recursive_mutex> lock(m_EventMutex);
            eMIRA_SERVICE_STATES current_state = getCurrentServiceState();

            if ( MIRACAST_SERVICE_STATE_CONNECTION_ACCEPTED != current_state )
            {
                MIRACASTLOG_INFO("Session already refreshed, So no need to report Error. Current state [%#08X]",current_state);
            }
            else
            {
                JsonObject eventDetails;
                eventDetails["source_dev_mac"] = client_mac;
                eventDetails["source_dev_name"] = client_name;
                eventDetails["reason_code"] = static_cast<int>(reason_code);
                dispatchEvent(MIRACASTSERVICE_EVENT_CLIENT_CONNECTION_ERROR, eventDetails);
            }
            MIRACASTLOG_TRACE("Exiting ...");
        }

        void MiracastServiceImplementation::onMiracastServiceLaunchRequest(string src_dev_ip, string src_dev_mac, string src_dev_name, string sink_dev_ip, bool is_connect_req_reported )
        {
            lock_guard<recursive_mutex> lock(m_EventMutex);
            eMIRA_SERVICE_STATES current_state = getCurrentServiceState();
            MIRACASTLOG_INFO("Entering[%u]..!!!",is_connect_req_reported);

            if ( !is_connect_req_reported )
            {
                changeServiceState(MIRACAST_SERVICE_STATE_DIRECT_LAUCH_REQUESTED);
                m_src_dev_ip = std::move(src_dev_ip);
                m_src_dev_mac = std::move(src_dev_mac);
                m_src_dev_name = std::move(src_dev_name);
                m_sink_dev_ip = std::move(sink_dev_ip);
                MIRACASTLOG_INFO("Direct Launch request has received. So need to notify connect Request");
                onMiracastServiceClientConnectionRequest( m_src_dev_mac, m_src_dev_name );
            }
            else if ( MIRACAST_SERVICE_STATE_CONNECTION_ACCEPTED != current_state )
            {
                MIRACASTLOG_INFO("Session already refreshed, So no need to notify Launch Request. Current state [%#08X]",current_state);
                //m_miracast_ctrler_obj->restart_session_discovery();
            }
            else
            {
                auto tupleParam = std::make_tuple(src_dev_ip,src_dev_mac,src_dev_name,sink_dev_ip);

                if (0 == access("/opt/miracast_autoconnect", F_OK))
                {
                    char commandBuffer[768] = {0};
                    snprintf( commandBuffer,
                            sizeof(commandBuffer),
                            "curl -H \"Authorization: Bearer `WPEFrameworkSecurityUtility | cut -d '\"' -f 4`\" --header \"Content-Type: application/json\" --request POST --data '{\"jsonrpc\":\"2.0\", \"id\":3,\"method\":\"org.rdk.MiracastPlayer.1.playRequest\", \"params\":{\"device_parameters\": {\"source_dev_ip\": \"%s\",\"source_dev_mac\": \"%s\",\"source_dev_name\": \"%s\",\"sink_dev_ip\": \"%s\"},\"video_rectangle\": {\"X\": 0,\"Y\": 0,\"W\": 1280,\"H\": 720}}}' http://127.0.0.1:9998/jsonrpc &",
                            src_dev_ip.c_str(),
                            src_dev_mac.c_str(),
                            src_dev_name.c_str(),
                            sink_dev_ip.c_str());
                    MIRACASTLOG_INFO("System Command [%s]",commandBuffer);
                    MiracastCommon::execute_SystemCommand(commandBuffer);
                }
                else
                {
                    JsonObject eventDetails;
                    eventDetails["source_dev_mac"] = src_dev_mac;
                    eventDetails["source_dev_name"] = src_dev_name;
                    eventDetails["source_dev_ip"] = src_dev_ip;
                    eventDetails["sink_dev_ip"] = sink_dev_ip;
                    dispatchEvent(MIRACASTSERVICE_EVENT_PLAYER_LAUNCH_REQUEST, eventDetails);
                }
                changeServiceState(MIRACAST_SERVICE_STATE_PLAYER_LAUNCHED);
            }
            MIRACASTLOG_INFO("Exiting ...");
        }

        void MiracastServiceImplementation::onStateChange(eMIRA_SERVICE_STATES state)
        {
            MIRACASTLOG_INFO("Entering state [%#08X]",state);
            lock_guard<recursive_mutex> lock(m_EventMutex);
            switch (state)
            {
                case MIRACAST_SERVICE_STATE_IDLE:
                case MIRACAST_SERVICE_STATE_DISCOVERABLE:
                {
                    if ((!m_isServiceEnabled) && (MIRACAST_SERVICE_STATE_DISCOVERABLE == state))
                    {
                        /*User already disabled the discovery, so should not enable again.*/
                        m_miracast_ctrler_obj->stop_discoveryAsync();
                    }
                    else
                    {
                        changeServiceState(state);
                    }
                }
                break;
                default:
                {

                }
                break;
            }
            MIRACASTLOG_INFO("Exiting ...");
        }
        /*  Events End */
        /* ------------------------------------------------------------------------------------------------------- */

        /*  PowerManager related methods Start */
        /* ------------------------------------------------------------------------------------------------------- */
        void MiracastServiceImplementation::InitializePowerManager(PluginHost::IShell *service)
        {
            MIRACASTLOG_INFO("Initializing PowerManager plugin ...");
            _powerManagerPlugin = PowerManagerInterfaceBuilder(_T("org.rdk.PowerManager"))
                                        .withIShell(service)
                                        .withRetryIntervalMS(200)
                                        .withRetryCount(25)
                                        .createInterface();
            MIRACASTLOG_INFO("Registering PowerManager plugin events");
            registerEventHandlers();
        }

        void MiracastServiceImplementation::registerEventHandlers()
        {
            ASSERT (_powerManagerPlugin);
            if(!_registeredEventHandlers && _powerManagerPlugin)
            {
                _registeredEventHandlers = true;
                _powerManagerPlugin->Register(_pwrMgrNotification.baseInterface<Exchange::IPowerManager::IModePreChangeNotification>());

                uint32_t clientId = 0;
                Core::hresult res = _powerManagerPlugin->AddPowerModePreChangeClient(POWER_PRECHANGE_CLIENT_NAME, clientId);
                if (Core::ERROR_NONE == res)
                {
                    lock_guard<mutex> lck(m_PowerManagerMutex);
                    m_PowerPreChangeClientId = clientId;
                    MIRACASTLOG_INFO("Registered as PowerModePreChange client [%u]", clientId);
                }
                else
                {
                    MIRACASTLOG_ERROR("AddPowerModePreChangeClient failed, ErrorCode [%#04X]", res);
                }
                MIRACASTLOG_INFO("onPowerModePreChange event registered ...");
            }
        }

        void MiracastServiceImplementation::onPowerModePreChange(const PowerState currentState, const PowerState newState, const int transactionId, const int stateChangeAfter)
        {
            MIRACASTLOG_INFO("#### MCAST-TRIAGE-OK-PWR PreChange [%#04X] -> [%#04X] trxn[%d] stateChangeAfter[%d] ####",
                                currentState, newState, transactionId, stateChangeAfter);
            if ((WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY_DEEP_SLEEP == newState) ||
                (WPEFramework::Exchange::IPowerManager::POWER_STATE_OFF == newState))
            {
                // Reserve time before teardown or the monitor start; the monitor only extends it if teardown overruns.
                requestPowerModeChangeDelay(transactionId, DEEP_SLEEP_TEARDOWN_BUDGET_SEC);
                startPowerDelayMonitor(transactionId);
            }
            // Session teardown can take seconds, so never block the PowerManager callback; the job acks on completion.
            Core::IWorkerPool::Instance().Submit(PowerModeJob::Create(this, newState, transactionId));
        }

        void MiracastServiceImplementation::requestPowerModeChangeDelay(int transactionId, int delaySec)
        {
            lock_guard<mutex> lck(m_PowerManagerMutex);
            if (_powerManagerPlugin && (0 != m_PowerPreChangeClientId))
            {
                Core::hresult res = _powerManagerPlugin->DelayPowerModeChangeBy(m_PowerPreChangeClientId, transactionId, delaySec);
                if (Core::ERROR_NONE != res)
                {
                    MIRACASTLOG_ERROR("DelayPowerModeChangeBy failed trxn[%d], ErrorCode [%#04X]", transactionId, res);
                }
                else
                {
                    MIRACASTLOG_INFO("DelayPowerModeChangeBy [%d]s requested trxn[%d]", delaySec, transactionId);
                }
            }
        }

        void MiracastServiceImplementation::startPowerDelayMonitor(int transactionId)
        {
            lock_guard<mutex> ctrl(m_PowerDelayMonitorCtrlMutex);
            if (m_PowerDelayMonitorThread.joinable())
            {
                {
                    lock_guard<mutex> lk(m_PowerDelayMonitorMutex);
                    m_PowerDelayMonitorDone = true;
                }
                m_PowerDelayMonitorCv.notify_all();
                m_PowerDelayMonitorThread.join();
            }
            {
                lock_guard<mutex> lk(m_PowerDelayMonitorMutex);
                m_PowerDelayMonitorDone = false;
            }
            m_PowerDelayMonitorTransactionId = transactionId;
            m_PowerDelayMonitorThread = std::thread(&MiracastServiceImplementation::powerDelayMonitor, this, transactionId);
        }

        void MiracastServiceImplementation::stopPowerDelayMonitor(int transactionId, bool force)
        {
            lock_guard<mutex> ctrl(m_PowerDelayMonitorCtrlMutex);
            if (!m_PowerDelayMonitorThread.joinable() || (!force && (transactionId != m_PowerDelayMonitorTransactionId)))
            {
                return;
            }
            {
                lock_guard<mutex> lk(m_PowerDelayMonitorMutex);
                m_PowerDelayMonitorDone = true;
            }
            m_PowerDelayMonitorCv.notify_all();
            m_PowerDelayMonitorThread.join();
        }

        void MiracastServiceImplementation::powerDelayMonitor(int transactionId)
        {
            const auto start = std::chrono::steady_clock::now();
            int reservedSec = DEEP_SLEEP_TEARDOWN_BUDGET_SEC;
            unique_lock<mutex> lk(m_PowerDelayMonitorMutex);
            // Wake only just before the current reservation expires, so a fast teardown never requests an extension.
            while (!m_PowerDelayMonitorCv.wait_until(lk, start + std::chrono::seconds(reservedSec - DEEP_SLEEP_DELAY_RENEW_MARGIN_SEC), [this] { return m_PowerDelayMonitorDone; }))
            {
                if (reservedSec >= DEEP_SLEEP_MAX_DELAY_SEC)
                {
                    MIRACASTLOG_ERROR("#### MCAST-TRIAGE-NOK-PWR Teardown exceeded [%d]s, no further delay requested trxn[%d] ####", DEEP_SLEEP_MAX_DELAY_SEC, transactionId);
                    break;
                }
                const int extensionSec = std::min(DEEP_SLEEP_DELAY_EXTENSION_SEC, DEEP_SLEEP_MAX_DELAY_SEC - reservedSec);
                MIRACASTLOG_WARNING("#### MCAST-TRIAGE-NOK-PWR Teardown still in progress after [%d]s, extending by [%d]s trxn[%d] ####",
                                    reservedSec - DEEP_SLEEP_DELAY_RENEW_MARGIN_SEC, extensionSec, transactionId);
                lk.unlock();
                // The delay is measured from now, so include the margin still left on the current reservation.
                requestPowerModeChangeDelay(transactionId, DEEP_SLEEP_DELAY_RENEW_MARGIN_SEC + extensionSec);
                lk.lock();
                reservedSec += extensionSec;
            }
        }

        void MiracastServiceImplementation::stopActivePlayerSession(void)
        {
            std::string clientMac;
            std::string clientName;
            Exchange::IMiracastPlayer *player = nullptr;
            {
                lock_guard<mutex> lck(m_DiscoveryStateMutex);
                lock_guard<recursive_mutex> lock(m_EventMutex);
                if ((MIRACAST_SERVICE_STATE_PLAYER_LAUNCHED != m_eService_state) || (nullptr == m_miracast_ctrler_obj) || (nullptr == m_CurrentService))
                {
                    MIRACASTLOG_INFO("No active Miracast session to stop");
                    return;
                }
                clientMac = m_miracast_ctrler_obj->get_WFDSourceMACAddress();
                clientName = m_miracast_ctrler_obj->get_WFDSourceName();
                player = m_CurrentService->QueryInterfaceByCallsign<Exchange::IMiracastPlayer>(MIRACAST_PLAYER_CALLSIGN);
            }

            if (nullptr == player)
            {
                MIRACASTLOG_ERROR("Failed to get %s interface, unable to stop active session", MIRACAST_PLAYER_CALLSIGN);
                return;
            }

            {
                lock_guard<mutex> lk(m_PlayerStopMutex);
                m_PlayerStopped = false;
            }
            const bool registered = (Core::ERROR_NONE == player->Register(&_playerStopNotification));

            MIRACASTLOG_INFO("#### MCAST-TRIAGE-OK-PWR Stopping active session [%s - %s] ####", clientName.c_str(), clientMac.c_str());
            Exchange::IMiracastPlayer::Result result;
            Core::hresult res = player->StopRequest(clientMac, clientName, MIRACAST_PLAYER_STOP_REASON_APP_REQ_FOR_EXIT, result);
            if ((Core::ERROR_NONE == res) && result.success)
            {
                unique_lock<mutex> lk(m_PlayerStopMutex);
                if (!m_PlayerStopCv.wait_for(lk, std::chrono::milliseconds(DEEP_SLEEP_PLAYER_STOP_TIMEOUT_MS), [this] { return m_PlayerStopped; }))
                {
                    MIRACASTLOG_WARNING("#### MCAST-TRIAGE-NOK-PWR Player stop not confirmed before timeout ####");
                }
            }
            else
            {
                MIRACASTLOG_ERROR("StopRequest failed, ErrorCode [%#04X] message [%s]", res, result.message.c_str());
            }

            if (registered)
            {
                player->Unregister(&_playerStopNotification);
            }
            player->Release();
        }

        void MiracastServiceImplementation::onPlayerStopped(void)
        {
            {
                lock_guard<mutex> lk(m_PlayerStopMutex);
                m_PlayerStopped = true;
            }
            m_PlayerStopCv.notify_all();
        }

        void MiracastServiceImplementation::InitializePowerState()
        {
            MIRACASTLOG_TRACE("Entering ...");
            Core::hresult res = Core::ERROR_GENERAL;
            PowerState pwrStateCur = WPEFramework::Exchange::IPowerManager::POWER_STATE_UNKNOWN;
            PowerState pwrStatePrev = WPEFramework::Exchange::IPowerManager::POWER_STATE_UNKNOWN;

            MIRACASTLOG_INFO("Initializing Power State");
            ASSERT (_powerManagerPlugin);
            if (_powerManagerPlugin)
            {
                res = _powerManagerPlugin->GetPowerState(pwrStateCur, pwrStatePrev);
                if (Core::ERROR_NONE == res)
                {
                    setPowerStateInternal(pwrStateCur);
                    MIRACASTLOG_INFO("Current Power State is [%#04X]",pwrStateCur);
                }
                else
                {
                    MIRACASTLOG_ERROR("Failed to get PowerState, ErrorCode [%#04X]",res);
                }
            }
            MIRACASTLOG_TRACE("Exiting ...");
        }

        std::string MiracastServiceImplementation::getPowerStateString(PowerState pwrState)
        {
            std::string pwrStateStr = "";
            switch (pwrState) 
            {
                case WPEFramework::Exchange::IPowerManager::POWER_STATE_ON: pwrStateStr = "ON"; break;
                case WPEFramework::Exchange::IPowerManager::POWER_STATE_OFF: pwrStateStr = "OFF"; break;
                case WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY: pwrStateStr = "STANDBY"; break;
                case WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY_LIGHT_SLEEP: pwrStateStr = "LIGHT_SLEEP"; break;
                case WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY_DEEP_SLEEP: pwrStateStr = "DEEP_SLEEP"; break;
                default: pwrStateStr = "UNKNOWN"; break;
            }
            MIRACASTLOG_INFO("Power State[%#04X - %s]",pwrState ,pwrStateStr.c_str());
            return pwrStateStr;
        }

        void MiracastServiceImplementation::setPowerStateInternal(PowerState pwrState, bool isPreChange, int transactionId)
        {
            MIRACASTLOG_TRACE("Entering ...");
            lock_guard<mutex> transitionLock(m_PowerTransitionMutex);
            PowerState old_pwr_state = m_powerState,
                                        new_pwr_state = pwrState;
            m_powerState = pwrState;
            MIRACASTLOG_INFO("changing power state [%s] -> [%s]",
                                getPowerStateString(old_pwr_state).c_str(),
                                getPowerStateString(new_pwr_state).c_str());
            if ((WPEFramework::Exchange::IPowerManager::POWER_STATE_ON == pwrState) ||
                (WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY == pwrState) ||
                (WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY_LIGHT_SLEEP == pwrState))
            {
                lock_guard<mutex> lck(m_DiscoveryStateMutex);
                if (m_IsDeepSleepInProgress.exchange(false) && m_isServiceInitialized && (nullptr != m_miracast_ctrler_obj))
                {
                    // A fresh link has no subscriptions yet, so creating it under the lock cannot deadlock with its event thread.
                    subscribeWiFiEvents();
                    if (!updateSystemFriendlyName())
                    {
                        remove_friendly_name_timer();
                        m_FriendlyNameMonitorTimerID = g_timeout_add(2000, MiracastServiceImplementation::monitor_friendly_name_timercallback, this);
                        MIRACASTLOG_WARNING("Unable to get friendlyName after resume, requires polling [%u]...", m_FriendlyNameMonitorTimerID);
                    }
                }

                lock_guard<recursive_mutex> lock(m_EventMutex);
                if ((m_IsTransitionFromDeepSleep) && (m_isServiceEnabled) && (nullptr != m_miracast_ctrler_obj))
                {
                    MIRACASTLOG_INFO("#### MCAST-TRIAGE-OK-PWR Enable Miracast discovery from PwrMgr [%d]",m_isServiceEnabled);
                    m_miracast_ctrler_obj->restart_discoveryAsync();
                    m_IsTransitionFromDeepSleep = false;
                }
                else if (!m_isServiceEnabled)
                {
                    MIRACASTLOG_INFO("#### MCAST-TRIAGE-OK-PWR Miracast discovery already Disabled [%d]. No need to enable it",m_isServiceEnabled);
                }
            }
            else if ((WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY_DEEP_SLEEP == pwrState) ||
                     (WPEFramework::Exchange::IPowerManager::POWER_STATE_OFF == pwrState))
            {
                if (!m_IsDeepSleepInProgress.exchange(true))
                {
                    // Must be released outside m_DiscoveryStateMutex: its event thread may be blocked on that mutex.
                    releaseWiFiPlugin();
                    // Stop casting first so the source receives an RTSP TEARDOWN while the P2P link is still up.
                    stopActivePlayerSession();
                }

                lock_guard<mutex> lck(m_DiscoveryStateMutex);
                bool isServiceEnabled;
                {
                    lock_guard<recursive_mutex> lock(m_EventMutex);
                    isServiceEnabled = m_isServiceEnabled;
                }
                if ( isServiceEnabled && (nullptr != m_miracast_ctrler_obj) )
                {
                    MIRACASTLOG_INFO("#### MCAST-TRIAGE-OK-PWR Miracast Discovery Disabled ####");
                    // Replaces setEnableInternal(false) to confirm P2P teardown; m_EventMutex must not be held as the controller thread takes it.
                    if (!m_miracast_ctrler_obj->stop_discoveryAndWait(DEEP_SLEEP_CONTROLLER_STOP_TIMEOUT_MS))
                    {
                        MIRACASTLOG_WARNING("#### MCAST-TRIAGE-NOK-PWR P2P teardown not confirmed before timeout ####");
                    }
                    lock_guard<recursive_mutex> lock(m_EventMutex);
                    changeServiceState(MIRACAST_SERVICE_STATE_IDLE);
                }
                else
                {
                    MIRACASTLOG_INFO("#### MCAST-TRIAGE-OK-PWR Miracast discovery already Disabled [%d]. No need to disable it",isServiceEnabled);
                }
                remove_friendly_name_timer();
                remove_wifi_connection_state_timer();
                remove_miracast_connection_timer();
                m_IsTransitionFromDeepSleep = true;
            }

            if (isPreChange)
            {
                stopPowerDelayMonitor(transactionId);
                lock_guard<mutex> lck(m_PowerManagerMutex);
                if (_powerManagerPlugin && (0 != m_PowerPreChangeClientId))
                {
                    Core::hresult res = _powerManagerPlugin->PowerModePreChangeComplete(m_PowerPreChangeClientId, transactionId);
                    if (Core::ERROR_NONE != res)
                    {
                        MIRACASTLOG_ERROR("PowerModePreChangeComplete failed trxn[%d], ErrorCode [%#04X]", transactionId, res);
                    }
                    else
                    {
                        MIRACASTLOG_INFO("PowerModePreChangeComplete sent trxn[%d]", transactionId);
                    }
                }
            }
            MIRACASTLOG_TRACE("Exiting ...");
        }
        /*  PowerManager related methods End */
        /* ------------------------------------------------------------------------------------------------------- */

        void MiracastServiceImplementation::InitializeSystemServices(PluginHost::IShell* service)
        {
            if (nullptr == _systemServicesPlugin)
            {
                _systemServicesPlugin = service->QueryInterfaceByCallsign<WPEFramework::Exchange::ISystemServices>(SYSTEM_CALLSIGN);
                if (_systemServicesPlugin != nullptr)
                {
                    registerSystemEventHandlers();
                }
                else
                {
                    MIRACASTLOG_ERROR("Failed to get SystemServices instance");
                }
            }
        }

        void MiracastServiceImplementation::registerSystemEventHandlers()
        {
            if (_systemServicesPlugin)
            {
                if (Core::ERROR_NONE == _systemServicesPlugin->Register(&_systemServicesNotification))
                {
                    MIRACASTLOG_INFO("ISystemServices::Register event registered");
                    _registeredSystemEventHandlers = true;
                }
                else
                {
                    MIRACASTLOG_ERROR("Failed to register ISystemServices::Register event");
                    _registeredSystemEventHandlers = false;
                }
            }
        }

        /*  WiFi related methods Start */
        /* ------------------------------------------------------------------------------------------------------- */
        void MiracastServiceImplementation::setWiFiStateInternal(DEVICE_WIFI_STATES wifiState)
        {
            MIRACASTLOG_INFO("Miracast WiFi State=%#08X", wifiState);
            lock_guard<mutex> lck(m_DiscoveryStateMutex);
            if (m_IsDeepSleepInProgress)
            {
                MIRACASTLOG_INFO("Ignoring WiFi state change while deep sleep in progress");
                return;
            }
            bool isServiceEnabled;
            {
                lock_guard<recursive_mutex> lock(m_EventMutex);
                isServiceEnabled = m_isServiceEnabled;
            }
            if (isServiceEnabled)
            {
                switch(wifiState)
                {
                    case DEVICE_WIFI_STATE_CONNECTING:
                    {
                        MIRACASTLOG_INFO("#### MCAST-TRIAGE-OK-WIFI DEVICE_WIFI_STATE [CONNECTING] ####");
                        {lock_guard<recursive_mutex> lock(m_EventMutex);
                            setEnableInternal(false);
                        }
                        remove_wifi_connection_state_timer();
                        m_IsWiFiConnectingState = true;
                        m_WiFiConnectedStateMonitorTimerID = g_timeout_add(30000, MiracastServiceImplementation::monitor_wifi_connection_state_timercallback, this);
                        MIRACASTLOG_INFO("Timer created to Monitor WiFi Connection Status [%u]",m_WiFiConnectedStateMonitorTimerID);
                    }
                    break;
                    case DEVICE_WIFI_STATE_CONNECTED:
                    case DEVICE_WIFI_STATE_FAILED:
                    {
                        MIRACASTLOG_INFO("#### MCAST-TRIAGE-OK-WIFI DEVICE_WIFI_STATE [%s] ####",( DEVICE_WIFI_STATE_CONNECTED == wifiState ) ? "CONNECTED" : "FAILED");
                        if (m_IsWiFiConnectingState)
                        {
                            {lock_guard<recursive_mutex> lock(m_EventMutex);
                                setEnableInternal(true);
                            }
                            m_IsWiFiConnectingState = false;
                        }
                        remove_wifi_connection_state_timer();
                    }
                    break;
                    default:
                    {
                        /* NOP */
                    }
                    break;
                }
            }
        }
        /*  WiFi related methods End */
        /* ------------------------------------------------------------------------------------------------------- */

        /*  JsonRPC Event Subscribed handler methods Start */
        /* ------------------------------------------------------------------------------------------------------- */
        void MiracastServiceImplementation::onFriendlyNameUpdateHandler(const string& friendlyName)
        {
            MIRACASTLOG_TRACE("Entering ...");
            MIRACASTLOG_INFO("[Friendly Name Event], [%s]", friendlyName.c_str());

            if (!friendlyName.empty())
            {
                bool isServiceEnabled = false;
                {
                    lock_guard<recursive_mutex> lock(m_EventMutex);
                    isServiceEnabled = m_isServiceEnabled;
                }
                m_miracast_ctrler_obj->set_FriendlyName(friendlyName, isServiceEnabled);
                MIRACASTLOG_INFO("Miracast FriendlyName=%s", friendlyName.c_str());
            }
            MIRACASTLOG_TRACE("Exiting ...");
        }

        void MiracastServiceImplementation::onWIFIStateChangedHandler(const JsonObject &parameters)
        {
            MIRACASTLOG_TRACE("Entering ...");
            string message;
            uint32_t wifiState;
            parameters.ToString(message);
            MIRACASTLOG_INFO("[WiFi State Changed Event], [%s]", message.c_str());

            if (parameters.HasLabel("state"))
            {
                wifiState = parameters["state"].Number();
                setWiFiStateInternal(static_cast<DEVICE_WIFI_STATES>(wifiState));
            }
            MIRACASTLOG_TRACE("Exiting ...");
        }
        /*  JsonRPC Event Subscribed handler methods End */
        /* ------------------------------------------------------------------------------------------------------- */

        /*  Internal Timer Callback and methods Start */
        /* ------------------------------------------------------------------------------------------------------- */
        gboolean MiracastServiceImplementation::monitor_friendly_name_timercallback(gpointer userdata)
        {
            gboolean timer_retry_state = G_SOURCE_CONTINUE;
            MIRACASTLOG_TRACE("Entering ...");
            MiracastServiceImplementation *self = (MiracastServiceImplementation *)userdata;
            MIRACASTLOG_INFO("TimerCallback Triggered for updating friendlyName...");
            if ( true == self->updateSystemFriendlyName() )
            {
                MIRACASTLOG_INFO("friendlyName updated properly, No polling required...");
                self->m_FriendlyNameMonitorTimerID = 0;
                timer_retry_state = G_SOURCE_REMOVE;
            }
            else
            {
                MIRACASTLOG_WARNING("Unable to get friendlyName, still requires polling...");
            }
            MIRACASTLOG_TRACE("Exiting ...");
            return timer_retry_state;
        }

        gboolean MiracastServiceImplementation::monitor_wifi_connection_state_timercallback(gpointer userdata)
        {
            MIRACASTLOG_TRACE("Entering ...");
            MiracastServiceImplementation *self = (MiracastServiceImplementation *)userdata;
            MIRACASTLOG_INFO("TimerCallback Triggered for Monitor WiFi Connection Status...");
            {lock_guard<mutex> lck(self->m_DiscoveryStateMutex);
                bool isServiceEnabled;
                {
                    lock_guard<recursive_mutex> lock(self->m_EventMutex);
                    isServiceEnabled = self->m_isServiceEnabled;
                }
                MIRACASTLOG_INFO("#### MCAST-TRIAGE-OK-WIFI Discovery[%u] WiFiConnectingState[%u] ####",
                                    isServiceEnabled,m_IsWiFiConnectingState);
                if (isServiceEnabled && m_IsWiFiConnectingState)
                {
                    {lock_guard<recursive_mutex> lock(self->m_EventMutex);
                        self->setEnableInternal(true);
                    }
                }
                m_IsWiFiConnectingState = false;
            }
            MIRACASTLOG_TRACE("Exiting ...");
            return G_SOURCE_REMOVE;
        }

        gboolean MiracastServiceImplementation::monitor_miracast_connection_timercallback(gpointer userdata)
        {
            MiracastServiceImplementation *self = (MiracastServiceImplementation *)userdata;
            MIRACASTLOG_TRACE("Entering ...");
            lock_guard<recursive_mutex> lock(self->m_EventMutex);
            MIRACASTLOG_INFO("TimerCallback Triggered for Monitor Miracast Connection Expired and Restarting Session...");
            if (self->m_isServiceEnabled)
            {
                self->m_miracast_ctrler_obj->restart_session_discovery(self->m_src_dev_mac);
                self->m_src_dev_mac.clear();
                self->changeServiceState(MIRACAST_SERVICE_STATE_RESTARTING_SESSION);
            }
            MIRACASTLOG_TRACE("Exiting ...");
            return G_SOURCE_REMOVE;
        }

        void MiracastServiceImplementation::remove_wifi_connection_state_timer(void)
        {
            MIRACASTLOG_TRACE("Entering ...");
            if (m_WiFiConnectedStateMonitorTimerID)
            {
                MIRACASTLOG_INFO("Removing WiFi Connection Status Monitor Timer");
                g_source_remove(m_WiFiConnectedStateMonitorTimerID);
                m_WiFiConnectedStateMonitorTimerID = 0;
            }
            m_IsWiFiConnectingState = false;
            MIRACASTLOG_TRACE("Exiting ...");
        }

        void MiracastServiceImplementation::remove_miracast_connection_timer(void)
        {
            MIRACASTLOG_TRACE("Entering ...");
            if (m_MiracastConnectionMonitorTimerID)
            {
                MIRACASTLOG_INFO("Removing Miracast Connection Status Monitor Timer");
                g_source_remove(m_MiracastConnectionMonitorTimerID);
                m_MiracastConnectionMonitorTimerID = 0;
            }
            MIRACASTLOG_TRACE("Exiting ...");
        }

        void MiracastServiceImplementation::remove_friendly_name_timer(void)
        {
            if (m_FriendlyNameMonitorTimerID)
            {
                MIRACASTLOG_INFO("Removing FriendlyName Monitor Timer");
                g_source_remove(m_FriendlyNameMonitorTimerID);
                m_FriendlyNameMonitorTimerID = 0;
            }
        }
        /*  Internal Timer Callback and methods End */
        /* ------------------------------------------------------------------------------------------------------- */
    } // namespace Plugin
} // namespace WPEFramework
