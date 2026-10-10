/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2024 RDK Management
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

#include <gtest/gtest.h>

#include "MiracastService.h"

#include "FactoriesImplementation.h"
#include "ServiceMock.h"
#include "ThunderPortability.h"
#include "PowerManagerMock.h"

#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include "COMLinkMock.h"
#include "WrapsMock.h"
#include "WorkerPoolImplementation.h"
#include "MiracastServiceImplementation.h"
#include <sys/time.h>

bool appendMiracastCommandOutput(char* destination, size_t capacity, const char* line);

TEST(MiracastServiceSecurityTest, BoundsCommonCommandOutput)
{
    char output[8] = "abc";
    EXPECT_TRUE(appendMiracastCommandOutput(output, sizeof(output), "de"));
    EXPECT_STREQ("abcde", output);
    EXPECT_FALSE(appendMiracastCommandOutput(output, sizeof(output), "0123456789"));
    EXPECT_EQ('\0', output[sizeof(output) - 1]);
    EXPECT_FALSE(appendMiracastCommandOutput(nullptr, sizeof(output), "line"));
}
#include <future>

using namespace WPEFramework;
using ::testing::NiceMock;
namespace
{
    #define TEST_LOG(FMT, ...) log(__func__, __FILE__, __LINE__, syscall(__NR_gettid),FMT,##__VA_ARGS__)

	static void removeFile(const char* fileName)
	{
		if (std::remove(fileName) != 0)
		{
			printf("File %s failed to remove\n", fileName);
			perror("Error deleting file");
		}
		else
		{
			printf("File %s successfully deleted\n", fileName);
		}
	}

	static void removeEntryFromFile(const char* fileName, const char* entryToRemove)
	{
		std::ifstream inputFile(fileName);
		if (!inputFile.is_open())
		{
			printf("Error: Unable to open file: %s\n",fileName);
			return;
		}

		std::vector<std::string> lines;
		std::string line;
		while (std::getline(inputFile, line)) {
			if (line != entryToRemove) {
				lines.push_back(line);
			}
		}
		inputFile.close();

		std::ofstream outputFile(fileName);
		if (!outputFile.is_open())
		{
			printf("Error: Unable to open file: %s for writing\n",fileName);
			return;
		}

		for (const auto& line : lines) {
			outputFile << line << "\n";
		}
		outputFile.close();

		printf("Entry removed from file: %s\n",fileName);
	}
	
	static void createFile(const char* fileName, const char* fileContent)
	{
		removeFile(fileName);

		std::ofstream fileContentStream(fileName);
		fileContentStream << fileContent;
		fileContentStream << "\n";
		fileContentStream.close();
	}
    void current_time(char *time_str)
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);

        long microseconds = tv.tv_usec;

        // Convert time to human-readable format
        struct tm *tm_info;
        tm_info = localtime(&tv.tv_sec);

        /* FIX: Replace sprintf with snprintf to prevent buffer overflow - assuming time_str buffer is 24 bytes */
        snprintf(time_str, 24, ": %02d:%02d:%02d:%06ld", tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec, microseconds);
    }
    void log( const char *func, const char *file, int line, int threadID,const char *format, ...)
    {
        const short kFormatMessageSize = 4096;
        char formatted[kFormatMessageSize];
        char time[24] = {0};
        va_list argptr;

        current_time(time);

        /* FIX: Add null pointer check for format parameter */
        if (!format) return;

        va_start(argptr, format);
        vsnprintf(formatted, kFormatMessageSize, format, argptr);
        va_end(argptr);

        fprintf(stderr, "[GUNIT][%d] INFO [%s:%d %s] %s: %s \n",
                    (int)syscall(SYS_gettid),
                    basename(file),
                    line,
                    time,
                    func,
                    formatted);
        fflush(stderr);
    }
}

static struct wpa_ctrl global_wpa_ctrl_handle;

class MiracastServiceTest : public ::testing::Test {
protected:
    Core::ProxyType<Plugin::MiracastService> plugin;
    Core::JSONRPC::Handler& handler;
    DECL_CORE_JSONRPC_CONX connection;
    Core::JSONRPC::Message message;
    string response;

    WrapsImplMock *p_wrapsImplMock = nullptr;
    Core::ProxyType<Plugin::MiracastServiceImplementation> miracastServiceImpl;

    NiceMock<COMLinkMock> comLinkMock;
    NiceMock<ServiceMock> service;
    PLUGINHOST_DISPATCHER* dispatcher;
    Core::ProxyType<WorkerPoolImplementation> workerPool;
    
    NiceMock<FactoriesImplementation> factoriesImplementation;

    MiracastServiceTest()
        : plugin(Core::ProxyType<Plugin::MiracastService>::Create())
        , handler(*(plugin))
        , INIT_CONX(1, 0)
        , workerPool(Core::ProxyType<WorkerPoolImplementation>::Create(2, Core::Thread::DefaultStackSize(), 16))
    {
        p_wrapsImplMock = new NiceMock<WrapsImplMock>;
        printf("Pass created wrapsImplMock: %p ", p_wrapsImplMock);
        Wraps::setImpl(p_wrapsImplMock);
        
        ON_CALL(service, COMLink())
        .WillByDefault(::testing::Invoke(
              [this]() {
                    TEST_LOG("Pass created comLinkMock: %p ", &comLinkMock);
                    return &comLinkMock;
                }));


        #ifdef USE_THUNDER_R4
            ON_CALL(comLinkMock, Instantiate(::testing::_, ::testing::_, ::testing::_))
                .WillByDefault(::testing::Invoke(
                        [&](const RPC::Object& object, const uint32_t waitTime, uint32_t& connectionId) {
                            miracastServiceImpl = Core::ProxyType<Plugin::MiracastServiceImplementation>::Create();
                            TEST_LOG("Pass created miracastServiceImpl: %p ", &miracastServiceImpl);
                            return &miracastServiceImpl;
                    }));
         #else
            ON_CALL(comLinkMock, Instantiate(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
                 .WillByDefault(::testing::Return(miracastServiceImpl));
         #endif /*USE_THUNDER_R4 */
        
        ON_CALL(*p_wrapsImplMock, wpa_ctrl_open(::testing::_))
			.WillByDefault(::testing::Invoke([&](const char *ctrl_path) { return &global_wpa_ctrl_handle; }));
		ON_CALL(*p_wrapsImplMock, wpa_ctrl_close(::testing::_))
			.WillByDefault(::testing::Invoke([&](struct wpa_ctrl *) { return; }));
		ON_CALL(*p_wrapsImplMock, wpa_ctrl_pending(::testing::_))
			.WillByDefault(::testing::Invoke([&](struct wpa_ctrl *ctrl) { return true; }));
		ON_CALL(*p_wrapsImplMock, wpa_ctrl_attach(::testing::_))
			.WillByDefault(::testing::Invoke([&](struct wpa_ctrl *ctrl) { return false; }));
		ON_CALL(*p_wrapsImplMock, system(::testing::_))
			.WillByDefault(::testing::Invoke([&](const char* command) {return 0;}));
        
        PluginHost::IFactories::Assign(&factoriesImplementation);

        Core::IWorkerPool::Assign(&(*workerPool));
        workerPool->Run();

        dispatcher = static_cast<PLUGINHOST_DISPATCHER*>(
        plugin->QueryInterface(PLUGINHOST_DISPATCHER_ID));
        dispatcher->Activate(&service);
    }
    virtual ~MiracastServiceTest() override
    {
        TEST_LOG("MiracastServiceTest Destructor");

        dispatcher->Deactivate();
        dispatcher->Release();

        Core::IWorkerPool::Assign(nullptr);
        workerPool.Release();
    
        Wraps::setImpl(nullptr);
        if (p_wrapsImplMock != nullptr)
        {
            delete p_wrapsImplMock;
            p_wrapsImplMock = nullptr;
        }
        PluginHost::IFactories::Assign(nullptr);
    }
};

class MiracastServiceEventTest : public MiracastServiceTest {
protected:
    NiceMock<ServiceMock> service;
    NiceMock<FactoriesImplementation> factoriesImplementation;
    PLUGINHOST_DISPATCHER* dispatcher;
    Core::JSONRPC::Message message;

    MiracastServiceEventTest()
        : MiracastServiceTest()
    {
        PluginHost::IFactories::Assign(&factoriesImplementation);

        dispatcher = static_cast<PLUGINHOST_DISPATCHER*>(
            plugin->QueryInterface(PLUGINHOST_DISPATCHER_ID));
        dispatcher->Activate(&service);
    }

    virtual ~MiracastServiceEventTest() override
    {
		dispatcher->Deactivate();
        dispatcher->Release();

        PluginHost::IFactories::Assign(nullptr);
    }
};

TEST_F(MiracastServiceTest, GetInformation)
{
    EXPECT_EQ("This MiracastService Plugin Facilitates Peer-to-Peer Control and WFD Source Device Discovery", plugin->Information());
}

TEST_F(MiracastServiceTest, P2PCtrlInterfaceNameNotFound)
{
	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");

	// WIFI_P2P_CTRL_INTERFACE not configured in device properties file
	EXPECT_NE(string(""), plugin->Initialize(&service));
	plugin->Deinitialize(nullptr);
}

TEST_F(MiracastServiceTest, P2PCtrlInterfacePathNotFound)
{
	removeFile("/var/run/wpa_supplicant/p2p0");
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");

	// Invalid P2P Ctrl iface configured
	EXPECT_NE(string(""), plugin->Initialize(&service));
	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
}

TEST_F(MiracastServiceTest, RegisteredMethods)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");

	EXPECT_EQ(string(""), plugin->Initialize(&service));

	EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("setEnable")));
	EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("getEnable")));
	EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("acceptClientConnection")));
	EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("stopClientConnection")));
	EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("updatePlayerState")));
	EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("setP2PBackendDiscovery")));

	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
}

TEST_F(MiracastServiceTest, P2P_DiscoveryStatus)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");

	EXPECT_EQ(string(""), plugin->Initialize(&service));

	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));
	EXPECT_EQ(response, string("{\"message\":\"Successfully enabled the WFD Discovery\",\"success\":true}"));

	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));
    EXPECT_EQ(response, string("{\"message\":\"WFD Discovery already enabled.\",\"success\":false}"));

	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getEnable"), _T("{}"), response));
	EXPECT_EQ(response, string("{\"enabled\":true,\"success\":true}"));

	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": false}"), response));
	EXPECT_EQ(response, string("{\"message\":\"Successfully disabled the WFD Discovery\",\"success\":true}"));

	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": false}"), response));
    EXPECT_EQ(response, string("{\"message\":\"WFD Discovery already disabled.\",\"success\":false}"));

	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("getEnable"), _T("{}"), response));
	EXPECT_EQ(response, string("{\"enabled\":false,\"success\":true}"));

	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
}

TEST_F(MiracastServiceTest, BackendDiscoveryStatus)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");

	EXPECT_EQ(string(""), plugin->Initialize(&service));

	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setP2PBackendDiscovery"), _T("{\"enabled\": true}"), response));

	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
}

TEST_F(MiracastServiceEventTest, stopClientConnection)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");

	EXPECT_EQ(string(""), plugin->Initialize(&service));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_request(::testing::_, ::testing::_, ::testing::_,::testing::_, ::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len, char *reply, size_t *reply_len, void(*msg_cb)(char *msg, size_t len))
					{
						if ( 0 == strncmp(cmd,"P2P_CONNECT",strlen("P2P_CONNECT")))
						{
							strncpy(reply,"OK",*reply_len);
						}
						return false;
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_recv(::testing::_, ::testing::_, ::testing::_))
		.WillOnce(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
					strncpy(reply, "P2P-DEVICE-FOUND 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0 wfd_dev_info=0x01101c440032 vendor_elems=1 new=1", *reply_len);
					return false;
					}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-PROV-DISC-PBC-REQ 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-REQUEST 96:52:44:b6:7d:14 dev_passwd_id=4 go_intent=13", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-SUCCESS role=client freq=2437 ht40=0 x=96:52:44:b6:7d:14 peer_iface=96:52:44:b6:fd:14 wps_method=PBC", *reply_len);
				return false;
				}))

	.WillRepeatedly(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				return true;
				}));

	Core::Event connectRequest(false, true);

	EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
		.Times(1)
		.WillOnce(::testing::Invoke(
					[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
					string text;
					EXPECT_TRUE(json->ToString(text));
					EXPECT_EQ(text,string(_T("{"
									"\"jsonrpc\":\"2.0\","
									"\"method\":\"client.events.onClientConnectionRequest\","
									"\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
									"\"name\":\"Sample-Test-Android-2\""
									"}}"
								)));
					connectRequest.SetEvent();
					return Core::ERROR_NONE;
					}));

     EVENT_SUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);

	EXPECT_EQ(Core::ERROR_NONE, connectRequest.Lock(10000));

	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("acceptClientConnection"), _T("{\"requestStatus\": Timeout}"), response));
    EXPECT_EQ(response, string("{\"message\":\"Supported 'requestStatus' parameter values are Accept or Reject\",\"success\":false}"));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("acceptClientConnection"), _T("{\"request\": Accept}"), response));
    EXPECT_EQ(response, string("{\"message\":\"Supported 'requestStatus' parameter values are Accept or Reject\",\"success\":false}"));

	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("acceptClientConnection"), _T("{\"requestStatus\": Accept}"), response));

	sleep(2);

	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("stopClientConnection"), _T("{\"name\": \"Sample-Test\",\"mac\": \"96:52:44:b6:7d:14\"}"), response));
    EXPECT_EQ(response, string("{\"message\":\"Invalid MAC and Name\",\"success\":false}"));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("stopClientConnection"), _T("{\"name\": \"Sample-Test-Android-2\",\"mac\": \"96:52:44:b6:7d\"}"), response));
    EXPECT_EQ(response, string("{\"message\":\"Invalid MAC and Name\",\"success\":false}"));

	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("stopClientConnection"), _T("{\"name\": \"Sample-Test-Android-2\",\"mac\": \"96:52:44:b6:7d:14\"}"), response));

	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);

	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
}

TEST_F(MiracastServiceEventTest, P2P_GOMode_onClientConnectionAndLaunchRequest)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");

	EXPECT_EQ(string(""), plugin->Initialize(&service));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));

	EXPECT_CALL(*p_wrapsImplMock, popen(::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](const char* command, const char* type)
					{
					char buffer[1024] = {0};
					if ( 0 == strncmp(command,"awk '$4 == ",strlen("awk '$4 == ")))
					{
						strncpy(buffer, "192.168.59.165", sizeof(buffer) - 1);
						buffer[sizeof(buffer) - 1] = '\0';
					}
					else if ( 0 == strncmp(command,"awk '$1 == ",strlen("awk '$1 == ")))
					{
						// Need to return as empty
					}
					else if ( 0 == strncmp(command,"arping",strlen("arping")))
					{
						const char* response = "Unicast reply from 192.168.59.165 [96:52:44:b6:7d:14]  2.189ms\nReceived 1 response";
						strncpy(buffer, response, sizeof(buffer) - 1);
						buffer[sizeof(buffer) - 1] = '\0';
					}
					size_t len = strnlen(buffer, sizeof(buffer));
					return (fmemopen(buffer, len, "r"));
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_request(::testing::_, ::testing::_, ::testing::_,::testing::_, ::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len, char *reply, size_t *reply_len, void(*msg_cb)(char *msg, size_t len))
					{
						if ( 0 == strncmp(cmd,"P2P_CONNECT",strlen("P2P_CONNECT")))
						{
							strncpy(reply,"OK",*reply_len);
						}
						return false;
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_recv(::testing::_, ::testing::_, ::testing::_))
		.WillOnce(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
					strncpy(reply, "P2P-DEVICE-FOUND 2c:33:58:9c:73:2d p2p_dev_addr=2c:33:58:9c:73:2d pri_dev_type=1-0050F200-0 name='Sample-Test-Android-1' config_methods=0x11e8 dev_capab=0x25 group_capab=0x82 wfd_dev_info=0x01101c440006 new=0", *reply_len);
					return false;
					}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-DEVICE-LOST 2c:33:58:9c:73:2d p2p_dev_addr=2c:33:58:9c:73:2d pri_dev_type=1-0050F200-0 name='Sample-Test-Android-1' config_methods=0x11e8 dev_capab=0x25 group_capab=0x82 wfd_dev_info=0x01101c440006 new=0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-DEVICE-FOUND 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0 wfd_dev_info=0x01101c440032 vendor_elems=1 new=1", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-PROV-DISC-PBC-REQ 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-PROV-DISC-PBC-REQ 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-REQUEST 96:52:44:b6:7d:14 dev_passwd_id=4 go_intent=13", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-SUCCESS role=client freq=2437 ht40=0 x=96:52:44:b6:7d:14 peer_iface=96:52:44:b6:fd:14 wps_method=PBC", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GROUP-FORMATION-SUCCESS", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-FIND-STOPPED", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				// Here using lo to avoid the operation not permitted error for unknown interfaces
				strncpy(reply, "P2P-GROUP-STARTED lo GO ssid=\"DIRECT-UU-Element-Xumo-TV\" freq=2437 psk=12c3ce3d8976152df796e5f42fc646723471bf1aab8d72a546fa3dce60dc14a3 go_dev_addr=96:52:44:b6:7d:14 ip_addr=192.168.49.200 ip_mask=255.255.255.0 go_ip_addr=192.168.49.1", *reply_len);
				return false;
				}))

	.WillRepeatedly(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				return true;
				}));

	Core::Event connectRequest(false, true);
	Core::Event P2PGrpStart(false, true);

	EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
		.Times(2)
		.WillOnce(::testing::Invoke(
					[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
					string text;
					EXPECT_TRUE(json->ToString(text));
					EXPECT_EQ(text,string(_T("{"
									"\"jsonrpc\":\"2.0\","
									"\"method\":\"client.events.onClientConnectionRequest\","
									"\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
									"\"name\":\"Sample-Test-Android-2\""
									"}}"
								)));
					connectRequest.SetEvent();
					return Core::ERROR_NONE;
					}))

	.WillOnce(::testing::Invoke(
				[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
				string text;
				EXPECT_TRUE(json->ToString(text));
				EXPECT_EQ(text,string(_T("{"
								"\"jsonrpc\":\"2.0\","
								"\"method\":\"client.events.onLaunchRequest\","
								"\"params\":{\"device_parameters\":{\"source_dev_ip\":\"192.168.59.165\","
								"\"source_dev_mac\":\"96:52:44:b6:7d:14\","
								"\"source_dev_name\":\"Sample-Test-Android-2\","
								"\"sink_dev_ip\":\"192.168.59.1\""
								"}}}"
							)));
				P2PGrpStart.SetEvent();
				return Core::ERROR_NONE;
				}));


	EVENT_SUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_SUBSCRIBE(0, _T("onLaunchRequest"), _T("client.events"), message);

	EXPECT_EQ(Core::ERROR_NONE, connectRequest.Lock(10000));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("acceptClientConnection"), _T("{\"requestStatus\": Accept}"), response));

	EXPECT_EQ(Core::ERROR_NONE, P2PGrpStart.Lock(10000));

	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("stopClientConnection"), _T("{\"name\": \"Sample-Test-Android-2\",\"mac\": \"96:52:44:b6:7d:14\"}"), response));
	EXPECT_EQ(response, string("{\"message\":\"Invalid state to process stopClientConnection\",\"success\":false}"));

	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("updatePlayerState"), _T("{\"mac\": \"96:52:44:b6:7d:14\",\"state\":\"INITIATED\"}"), response));
	sleep(1);
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("updatePlayerState"), _T("{\"mac\": \"96:52:44:b6:7d:14\",\"state\":\"INPROGRESS\"}"), response));
	sleep(1);
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("updatePlayerState"), _T("{\"mac\": \"96:52:44:b6:7d:14\",\"state\":\"PLAYING\"}"), response));
	sleep(1);

	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": false}"), response));
    EXPECT_EQ(response, string("{\"message\":\"Failed as MiracastPlayer already Launched\",\"success\":false}"));

	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("stopClientConnection"), _T("{\"name\": \"Sample-Test-Android-2\",\"mac\": \"96:52:44:b6:7d:14\"}"), response));
    EXPECT_EQ(response, string("{\"message\":\"Invalid state to process stopClientConnection\",\"success\":false}"));

	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("updatePlayerState"), _T("{\"mac\": \"96:52:44:b6:7d:14\",\"state\":\"STOPPED\"}"), response));

	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_UNSUBSCRIBE(0, _T("onLaunchRequest"), _T("client.events"), message);

	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
}

TEST_F(MiracastServiceEventTest, onClientConnectionRequestRejected)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");

	EXPECT_EQ(string(""), plugin->Initialize(&service));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_request(::testing::_, ::testing::_, ::testing::_,::testing::_, ::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len, char *reply, size_t *reply_len, void(*msg_cb)(char *msg, size_t len))
					{
						if ( 0 == strncmp(cmd,"P2P_CONNECT",strlen("P2P_CONNECT")))
						{
							strncpy(reply,"OK",*reply_len);
						}
						return false;
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_recv(::testing::_, ::testing::_, ::testing::_))
		.WillOnce(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
					strncpy(reply, "P2P-DEVICE-FOUND 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0 wfd_dev_info=0x01101c440032 vendor_elems=1 new=1", *reply_len);
					return false;
					}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-PROV-DISC-PBC-REQ 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-PROV-DISC-PBC-REQ 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-REQUEST 96:52:44:b6:7d:14 dev_passwd_id=4 go_intent=13", *reply_len);
				return false;
				}))

	.WillRepeatedly(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				return true;
				}));

	Core::Event connectRequest(false, true);

	EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
		.Times(1)
		.WillOnce(::testing::Invoke(
					[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
					string text;
					EXPECT_TRUE(json->ToString(text));
					EXPECT_EQ(text,string(_T("{"
									"\"jsonrpc\":\"2.0\","
									"\"method\":\"client.events.onClientConnectionRequest\","
									"\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
									"\"name\":\"Sample-Test-Android-2\""
									"}}"
								)));
					connectRequest.SetEvent();
					return Core::ERROR_NONE;
					}));

	EVENT_SUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);

	EXPECT_EQ(Core::ERROR_NONE, connectRequest.Lock(10000));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("acceptClientConnection"), _T("{\"requestStatus\": Reject}"), response));

	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);

	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
}

TEST_F(MiracastServiceEventTest, P2P_CONNECT_FAIL_onClientConnectionError)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");

	EXPECT_EQ(string(""), plugin->Initialize(&service));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_request(::testing::_, ::testing::_, ::testing::_,::testing::_, ::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len, char *reply, size_t *reply_len, void(*msg_cb)(char *msg, size_t len))
					{
						if ( 0 == strncmp(cmd,"P2P_CONNECT",strlen("P2P_CONNECT")))
						{
							strncpy(reply,"FAIL",*reply_len);
						}
						return false;
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_recv(::testing::_, ::testing::_, ::testing::_))
	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-DEVICE-FOUND 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0 wfd_dev_info=0x01101c440032 vendor_elems=1 new=1", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-PROV-DISC-PBC-REQ 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-REQUEST 96:52:44:b6:7d:14 dev_passwd_id=4 go_intent=13", *reply_len);
				return false;
				}))

	.WillRepeatedly(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				return true;
				}));

	Core::Event connectRequest(false, true);
	Core::Event P2PConnectFail(false, true);

	EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
		.Times(2)
		.WillOnce(::testing::Invoke(
					[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
					string text;
					EXPECT_TRUE(json->ToString(text));
					EXPECT_EQ(text,string(_T("{"
									"\"jsonrpc\":\"2.0\","
									"\"method\":\"client.events.onClientConnectionRequest\","
									"\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
									"\"name\":\"Sample-Test-Android-2\""
									"}"
									"}")));
					connectRequest.SetEvent();
					return Core::ERROR_NONE;
					}))
	.WillOnce(::testing::Invoke(
				[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
				string text;
				EXPECT_TRUE(json->ToString(text));
				EXPECT_EQ(text,string(_T("{"
								"\"jsonrpc\":\"2.0\","
								"\"method\":\"client.events.onClientConnectionError\","
								"\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
								"\"name\":\"Sample-Test-Android-2\","
								"\"error_code\":\"101\","
								"\"reason\":\"P2P_CONNECT_FAILURE\""
								"}"
								"}")));
				P2PConnectFail.SetEvent();
				return Core::ERROR_NONE;
				}));

	EVENT_SUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_SUBSCRIBE(0, _T("onClientConnectionError"), _T("client.events"), message);

	EXPECT_EQ(Core::ERROR_NONE, connectRequest.Lock(10000));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("acceptClientConnection"), _T("{\"requestStatus\": Accept}"), response));

	EXPECT_EQ(Core::ERROR_NONE, P2PConnectFail.Lock(10000));

	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionError"), _T("client.events"), message);

	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
}

TEST_F(MiracastServiceEventTest, P2P_GO_NEGOTIATION_FAIL_onClientConnectionError)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");

	EXPECT_EQ(string(""), plugin->Initialize(&service));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_request(::testing::_, ::testing::_, ::testing::_,::testing::_, ::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len, char *reply, size_t *reply_len, void(*msg_cb)(char *msg, size_t len))
					{
						if ( 0 == strncmp(cmd,"P2P_CONNECT",strlen("P2P_CONNECT")))
						{
							strncpy(reply,"OK",*reply_len);
						}
						return false;
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_recv(::testing::_, ::testing::_, ::testing::_))
		.WillOnce(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
					strncpy(reply, "P2P-DEVICE-FOUND 2c:33:58:9c:73:2d p2p_dev_addr=2c:33:58:9c:73:2d pri_dev_type=1-0050F200-0 name='Sample-Test-Android-1' config_methods=0x11e8 dev_capab=0x25 group_capab=0x82 wfd_dev_info=0x01101c440006 new=0", *reply_len);
					return false;
					}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-DEVICE-FOUND 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0 wfd_dev_info=0x01101c440032 vendor_elems=1 new=1", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-PROV-DISC-PBC-REQ 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-PROV-DISC-PBC-REQ 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-REQUEST 96:52:44:b6:7d:14 dev_passwd_id=4 go_intent=13", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-FAILURE 96:52:44:b6:7d:14", *reply_len);
				return false;
				}))
	.WillRepeatedly(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				return true;
				}));

	Core::Event connectRequest(false, true);
	Core::Event P2PGoFail(false, true);

	EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
		.Times(2)
		.WillOnce(::testing::Invoke(
					[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
					string text;
					EXPECT_TRUE(json->ToString(text));
					EXPECT_EQ(text,string(_T("{"
									"\"jsonrpc\":\"2.0\","
									"\"method\":\"client.events.onClientConnectionRequest\","
									"\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
									"\"name\":\"Sample-Test-Android-2\""
									"}"
									"}")));
					connectRequest.SetEvent();
					return Core::ERROR_NONE;
					}))
	.WillOnce(::testing::Invoke(
				[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
				string text;
				EXPECT_TRUE(json->ToString(text));
				EXPECT_EQ(text,string(_T("{"
								"\"jsonrpc\":\"2.0\","
								"\"method\":\"client.events.onClientConnectionError\","
								"\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
								"\"name\":\"Sample-Test-Android-2\","
								"\"error_code\":\"102\","
								"\"reason\":\"P2P_GROUP_NEGOTIATION_FAILURE\""
								"}"
								"}")));
				P2PGoFail.SetEvent();
				return Core::ERROR_NONE;
				}));

	EVENT_SUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_SUBSCRIBE(0, _T("onClientConnectionError"), _T("client.events"), message);

	EXPECT_EQ(Core::ERROR_NONE, connectRequest.Lock(10000));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("acceptClientConnection"), _T("{\"requestStatus\": Accept}"), response));

	EXPECT_EQ(Core::ERROR_NONE, P2PGoFail.Lock(10000));

	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionError"), _T("client.events"), message);

	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
}

TEST_F(MiracastServiceEventTest, P2P_GO_FORMATION_FAIL_onClientConnectionError)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");

	EXPECT_EQ(string(""), plugin->Initialize(&service));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_request(::testing::_, ::testing::_, ::testing::_,::testing::_, ::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len, char *reply, size_t *reply_len, void(*msg_cb)(char *msg, size_t len))
					{
						if ( 0 == strncmp(cmd,"P2P_CONNECT",strlen("P2P_CONNECT")))
						{
							strncpy(reply,"OK",*reply_len);
						}
						return false;
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_recv(::testing::_, ::testing::_, ::testing::_))
		.WillOnce(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
					strncpy(reply, "P2P-DEVICE-FOUND 2c:33:58:9c:73:2d p2p_dev_addr=2c:33:58:9c:73:2d pri_dev_type=1-0050F200-0 name='Sample-Test-Android-1' config_methods=0x11e8 dev_capab=0x25 group_capab=0x82 wfd_dev_info=0x01101c440006 new=0", *reply_len);
					return false;
					}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-DEVICE-FOUND 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0 wfd_dev_info=0x01101c440032 vendor_elems=1 new=1", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-PROV-DISC-PBC-REQ 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-PROV-DISC-PBC-REQ 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-REQUEST 96:52:44:b6:7d:14 dev_passwd_id=4 go_intent=13", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-SUCCESS role=client freq=2437 ht40=0 x=96:52:44:b6:7d:14 peer_iface=96:52:44:b6:fd:14 wps_method=PBC", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GROUP-FORMATION-FAILURE", *reply_len);
				return false;
				}))

	.WillRepeatedly(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				return true;
				}));

	Core::Event connectRequest(false, true);
	Core::Event P2PGoFail(false, true);

	EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
		.Times(2)
		.WillOnce(::testing::Invoke(
					[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
					string text;
					EXPECT_TRUE(json->ToString(text));
					EXPECT_EQ(text,string(_T("{"
									"\"jsonrpc\":\"2.0\","
									"\"method\":\"client.events.onClientConnectionRequest\","
									"\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
									"\"name\":\"Sample-Test-Android-2\""
									"}"
									"}")));
					connectRequest.SetEvent();
					return Core::ERROR_NONE;
					}))
	.WillOnce(::testing::Invoke(
				[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
				string text;
				EXPECT_TRUE(json->ToString(text));
				EXPECT_EQ(text,string(_T("{"
								"\"jsonrpc\":\"2.0\","
								"\"method\":\"client.events.onClientConnectionError\","
								"\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
								"\"name\":\"Sample-Test-Android-2\","
								"\"error_code\":\"103\","
								"\"reason\":\"P2P_GROUP_FORMATION_FAILURE\""
								"}"
								"}")));
				P2PGoFail.SetEvent();
				return Core::ERROR_NONE;
				}));

	EVENT_SUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_SUBSCRIBE(0, _T("onClientConnectionError"), _T("client.events"), message);

	EXPECT_EQ(Core::ERROR_NONE, connectRequest.Lock(10000));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("acceptClientConnection"), _T("{\"requestStatus\": Accept}"), response));

	EXPECT_EQ(Core::ERROR_NONE, P2PGoFail.Lock(10000));

	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionError"), _T("client.events"), message);

	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
}

TEST_F(MiracastServiceEventTest, P2P_ClientMode_onClientConnectionAndLaunchRequest)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");

	EXPECT_EQ(string(""), plugin->Initialize(&service));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));

	EXPECT_CALL(*p_wrapsImplMock, popen(::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](const char* command, const char* type)
					{
					char buffer[1024] = {0};
					if ( 0 == strncmp(command,"/sbin/udhcpc -v -i",strlen("/sbin/udhcpc -v -i")))
					{
						const char* response = "udhcpc: sending select for 192.168.49.165\tudhcpc: lease of 192.168.49.165 obtained, lease time 3599\tdeleting routers\troute add default gw 192.168.49.1 dev lo\tadding dns 192.168.49.1";
						strncpy(buffer, response, sizeof(buffer) - 1);
						buffer[sizeof(buffer) - 1] = '\0';
					}
					size_t len = strnlen(buffer, sizeof(buffer));
					return (fmemopen(buffer, len, "r"));
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_request(::testing::_, ::testing::_, ::testing::_,::testing::_, ::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len, char *reply, size_t *reply_len, void(*msg_cb)(char *msg, size_t len))
					{
						if ( 0 == strncmp(cmd,"P2P_CONNECT",strlen("P2P_CONNECT")))
						{
							strncpy(reply,"OK",*reply_len);
						}
						return false;
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_recv(::testing::_, ::testing::_, ::testing::_))
		.WillOnce(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
					strncpy(reply, "P2P-DEVICE-FOUND 2c:33:58:9c:73:2d p2p_dev_addr=2c:33:58:9c:73:2d pri_dev_type=1-0050F200-0 name='Sample-Test-Android-1' config_methods=0x11e8 dev_capab=0x25 group_capab=0x82 wfd_dev_info=0x01101c440006 new=0", *reply_len);
					return false;
					}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-DEVICE-FOUND 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0 wfd_dev_info=0x01101c440032 vendor_elems=1 new=1", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-DEVICE-LOST 2c:33:58:9c:73:2d p2p_dev_addr=2c:33:58:9c:73:2d pri_dev_type=1-0050F200-0 name='Sample-Test-Android-1' config_methods=0x11e8 dev_capab=0x25 group_capab=0x82 wfd_dev_info=0x01101c440006 new=0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-PROV-DISC-PBC-REQ 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-PROV-DISC-PBC-REQ 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-REQUEST 96:52:44:b6:7d:14 dev_passwd_id=4 go_intent=13", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-SUCCESS role=client freq=2437 ht40=0 x=96:52:44:b6:7d:14 peer_iface=96:52:44:b6:fd:14 wps_method=PBC", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GROUP-FORMATION-SUCCESS", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-FIND-STOPPED", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				// Here using lo to avoid the operation not permitted error for unknown interfaces
				strncpy(reply, "P2P-GROUP-STARTED lo client ssid=\"DIRECT-UU-Galaxy A23 5G\" freq=2437 psk=12c3ce3d8976152df796e5f42fc646723471bf1aab8d72a546fa3dce60dc14a3 go_dev_addr=96:52:44:b6:7d:14 [PERSISTENT]", *reply_len);
				return false;
				}))

	.WillRepeatedly(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				return true;
				}));

	Core::Event connectRequest(false, true);
	Core::Event P2PGrpStart(false, true);

	EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
		.Times(2)
		.WillOnce(::testing::Invoke(
					[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
					string text;
					EXPECT_TRUE(json->ToString(text));
					EXPECT_EQ(text,string(_T("{"
									"\"jsonrpc\":\"2.0\","
									"\"method\":\"client.events.onClientConnectionRequest\","
									"\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
									"\"name\":\"Sample-Test-Android-2\""
									"}}"
								)));
					connectRequest.SetEvent();
					return Core::ERROR_NONE;
					}))

	.WillOnce(::testing::Invoke(
				[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
				string text;
				EXPECT_TRUE(json->ToString(text));
				EXPECT_EQ(text,string(_T("{"
                                                                "\"jsonrpc\":\"2.0\","
                                                                "\"method\":\"client.events.onLaunchRequest\","
                                                                "\"params\":{\"device_parameters\":{\"source_dev_ip\":\"192.168.49.1\","
                                                                "\"source_dev_mac\":\"96:52:44:b6:7d:14\","
                                                                "\"source_dev_name\":\"Sample-Test-Android-2\","
                                                                "\"sink_dev_ip\":\"192.168.49.165\""
                                                                "}}}"
                                                        )));
                                P2PGrpStart.SetEvent();
				return Core::ERROR_NONE;
				}));

	EVENT_SUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_SUBSCRIBE(0, _T("onLaunchRequest"), _T("client.events"), message);

	EXPECT_EQ(Core::ERROR_NONE, connectRequest.Lock(10000));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("acceptClientConnection"), _T("{\"requestStatus\": Accept}"), response));

	EXPECT_EQ(Core::ERROR_NONE, P2PGrpStart.Lock(10000));

	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_UNSUBSCRIBE(0, _T("onLaunchRequest"), _T("client.events"), message);

	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
}

TEST_F(MiracastServiceEventTest, P2P_ClientMode_DirectonClientConnectionAndLaunchRequest)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");

	EXPECT_EQ(string(""), plugin->Initialize(&service));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));

	EXPECT_CALL(*p_wrapsImplMock, popen(::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](const char* command, const char* type)
					{
					char buffer[1024] = {0};
					if ( 0 == strncmp(command,"/sbin/udhcpc -v -i",strlen("/sbin/udhcpc -v -i")))
					{
						const char* response = "udhcpc: sending select for 192.168.49.165\tudhcpc: lease of 192.168.49.165 obtained, lease time 3599\tdeleting routers\troute add default gw 192.168.49.1 dev lo\tadding dns 192.168.49.1";
						strncpy(buffer, response, sizeof(buffer) - 1);
						buffer[sizeof(buffer) - 1] = '\0';
					}
					size_t len = strnlen(buffer, sizeof(buffer));
					return (fmemopen(buffer, len, "r"));
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_request(::testing::_, ::testing::_, ::testing::_,::testing::_, ::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len, char *reply, size_t *reply_len, void(*msg_cb)(char *msg, size_t len))
					{
						if ( 0 == strncmp(cmd,"P2P_CONNECT",strlen("P2P_CONNECT")))
						{
							strncpy(reply,"OK",*reply_len);
						}
						return false;
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_recv(::testing::_, ::testing::_, ::testing::_))
	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-DEVICE-FOUND 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0 wfd_dev_info=0x01101c440032 vendor_elems=1 new=1", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				// Here using lo to avoid the operation not permitted error for unknown interfaces
				strncpy(reply, "P2P-GROUP-STARTED lo client ssid=\"DIRECT-UU-Galaxy A23 5G\" freq=2437 psk=12c3ce3d8976152df796e5f42fc646723471bf1aab8d72a546fa3dce60dc14a3 go_dev_addr=96:52:44:b6:7d:14 [PERSISTENT]", *reply_len);
				return false;
				}))

	.WillRepeatedly(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				return true;
				}));

	Core::Event connectRequest(false, true);
	Core::Event P2PGrpStart(false, true);

	EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
		.Times(2)
		.WillOnce(::testing::Invoke(
					[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
					string text;
					EXPECT_TRUE(json->ToString(text));
					EXPECT_EQ(text,string(_T("{"
									"\"jsonrpc\":\"2.0\","
									"\"method\":\"client.events.onClientConnectionRequest\","
									"\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
									"\"name\":\"Sample-Test-Android-2\""
									"}}"
								)));
					connectRequest.SetEvent();
					return Core::ERROR_NONE;
					}))

	.WillOnce(::testing::Invoke(
				[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
				string text;
				EXPECT_TRUE(json->ToString(text));
				EXPECT_EQ(text,string(_T("{"
                                                                "\"jsonrpc\":\"2.0\","
                                                                "\"method\":\"client.events.onLaunchRequest\","
                                                                "\"params\":{\"device_parameters\":{\"source_dev_ip\":\"192.168.49.1\","
                                                                "\"source_dev_mac\":\"96:52:44:b6:7d:14\","
                                                                "\"source_dev_name\":\"Sample-Test-Android-2\","
                                                                "\"sink_dev_ip\":\"192.168.49.165\""
                                                                "}}}"
                                                        )));
                                P2PGrpStart.SetEvent();
				return Core::ERROR_NONE;
				}));

	EVENT_SUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_SUBSCRIBE(0, _T("onLaunchRequest"), _T("client.events"), message);

	EXPECT_EQ(Core::ERROR_NONE, connectRequest.Lock(10000));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("acceptClientConnection"), _T("{\"requestStatus\": Accept}"), response));

	EXPECT_EQ(Core::ERROR_NONE, P2PGrpStart.Lock(10000));

	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_UNSUBSCRIBE(0, _T("onLaunchRequest"), _T("client.events"), message);

	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
}

TEST_F(MiracastServiceEventTest, P2P_ClientMode_DirectGroupStartWithName)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");

	EXPECT_EQ(string(""), plugin->Initialize(&service));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));

	EXPECT_CALL(*p_wrapsImplMock, popen(::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](const char* command, const char* type)
					{
					char buffer[1024] = {0};
					if ( 0 == strncmp(command,"/sbin/udhcpc -v -i",strlen("/sbin/udhcpc -v -i")))
					{
						const char* response = "udhcpc: sending select for 192.168.49.165\tudhcpc: lease of 192.168.49.165 obtained, lease time 3599\tdeleting routers\troute add default gw 192.168.49.1 dev lo\tadding dns 192.168.49.1";
						strncpy(buffer, response, sizeof(buffer) - 1);
						buffer[sizeof(buffer) - 1] = '\0';
					}
					size_t len = strnlen(buffer, sizeof(buffer));
					return (fmemopen(buffer, len, "r"));
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_request(::testing::_, ::testing::_, ::testing::_,::testing::_, ::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len, char *reply, size_t *reply_len, void(*msg_cb)(char *msg, size_t len))
					{
						if ( 0 == strncmp(cmd,"P2P_CONNECT",strlen("P2P_CONNECT")))
						{
							strncpy(reply,"OK",*reply_len);
						}
						return false;
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_recv(::testing::_, ::testing::_, ::testing::_))
	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				// Here using lo to avoid the operation not permitted error for unknown interfaces
				strncpy(reply, "P2P-GROUP-STARTED lo client ssid=\"DIRECT-UU-Galaxy A23 5G\" freq=2437 psk=12c3ce3d8976152df796e5f42fc646723471bf1aab8d72a546fa3dce60dc14a3 go_dev_addr=96:52:44:b6:7d:14 [PERSISTENT]", *reply_len);
				return false;
				}))

	.WillRepeatedly(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				return true;
				}));

	Core::Event connectRequest(false, true);
	Core::Event P2PGrpStart(false, true);

	EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
		.Times(2)
		.WillOnce(::testing::Invoke(
					[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
					string text;
					EXPECT_TRUE(json->ToString(text));
					EXPECT_EQ(text,string(_T("{"
									"\"jsonrpc\":\"2.0\","
									"\"method\":\"client.events.onClientConnectionRequest\","
									"\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
									"\"name\":\"Galaxy A23 5G\""
									"}}"
								)));
					connectRequest.SetEvent();
					return Core::ERROR_NONE;
					}))

	.WillOnce(::testing::Invoke(
				[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
				string text;
				EXPECT_TRUE(json->ToString(text));
				EXPECT_EQ(text,string(_T("{"
                                                                "\"jsonrpc\":\"2.0\","
                                                                "\"method\":\"client.events.onLaunchRequest\","
                                                                "\"params\":{\"device_parameters\":{\"source_dev_ip\":\"192.168.49.1\","
                                                                "\"source_dev_mac\":\"96:52:44:b6:7d:14\","
                                                                "\"source_dev_name\":\"Galaxy A23 5G\","
                                                                "\"sink_dev_ip\":\"192.168.49.165\""
                                                                "}}}"
                                                        )));
                                P2PGrpStart.SetEvent();
				return Core::ERROR_NONE;
				}));

	EVENT_SUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_SUBSCRIBE(0, _T("onLaunchRequest"), _T("client.events"), message);

	EXPECT_EQ(Core::ERROR_NONE, connectRequest.Lock(10000));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("acceptClientConnection"), _T("{\"requestStatus\": Accept}"), response));

	EXPECT_EQ(Core::ERROR_NONE, P2PGrpStart.Lock(10000));

	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_UNSUBSCRIBE(0, _T("onLaunchRequest"), _T("client.events"), message);

	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
}

TEST_F(MiracastServiceEventTest, P2P_ClientMode_DirectGroupStartWithoutName)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");

	EXPECT_EQ(string(""), plugin->Initialize(&service));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));

	EXPECT_CALL(*p_wrapsImplMock, popen(::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](const char* command, const char* type)
					{
					char buffer[1024] = {0};
					if ( 0 == strncmp(command,"/sbin/udhcpc -v -i",strlen("/sbin/udhcpc -v -i")))
					{
						const char* response = "udhcpc: sending select for 192.168.49.165\tudhcpc: lease of 192.168.49.165 obtained, lease time 3599\tdeleting routers\troute add default gw 192.168.49.1 dev lo\tadding dns 192.168.49.1";
						strncpy(buffer, response, sizeof(buffer) - 1);
						buffer[sizeof(buffer) - 1] = '\0';
					}
					size_t len = strnlen(buffer, sizeof(buffer));
					return (fmemopen(buffer, len, "r"));
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_request(::testing::_, ::testing::_, ::testing::_,::testing::_, ::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len, char *reply, size_t *reply_len, void(*msg_cb)(char *msg, size_t len))
					{
						if ( 0 == strncmp(cmd,"P2P_CONNECT",strlen("P2P_CONNECT")))
						{
							strncpy(reply,"OK",*reply_len);
						}
						return false;
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_recv(::testing::_, ::testing::_, ::testing::_))
	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				// Here using lo to avoid the operation not permitted error for unknown interfaces
				strncpy(reply, "P2P-GROUP-STARTED lo client ssid=\"DIRECT-UU\" freq=2437 psk=12c3ce3d8976152df796e5f42fc646723471bf1aab8d72a546fa3dce60dc14a3 go_dev_addr=96:52:44:b6:7d:14 [PERSISTENT]", *reply_len);
				return false;
				}))

	.WillRepeatedly(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				return true;
				}));

	Core::Event connectRequest(false, true);
	Core::Event P2PGrpStart(false, true);

	EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
		.Times(2)
		.WillOnce(::testing::Invoke(
					[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
					string text;
					EXPECT_TRUE(json->ToString(text));
					EXPECT_EQ(text,string(_T("{"
									"\"jsonrpc\":\"2.0\","
									"\"method\":\"client.events.onClientConnectionRequest\","
									"\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
									"\"name\":\"Miracast-Source\""
									"}}"
								)));
					connectRequest.SetEvent();
					return Core::ERROR_NONE;
					}))

	.WillOnce(::testing::Invoke(
				[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
				string text;
				EXPECT_TRUE(json->ToString(text));
				EXPECT_EQ(text,string(_T("{"
                                                                "\"jsonrpc\":\"2.0\","
                                                                "\"method\":\"client.events.onLaunchRequest\","
                                                                "\"params\":{\"device_parameters\":{\"source_dev_ip\":\"192.168.49.1\","
                                                                "\"source_dev_mac\":\"96:52:44:b6:7d:14\","
                                                                "\"source_dev_name\":\"Miracast-Source\","
                                                                "\"sink_dev_ip\":\"192.168.49.165\""
                                                                "}}}"
                                                        )));
                                P2PGrpStart.SetEvent();
				return Core::ERROR_NONE;
				}));

	EVENT_SUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_SUBSCRIBE(0, _T("onLaunchRequest"), _T("client.events"), message);

	EXPECT_EQ(Core::ERROR_NONE, connectRequest.Lock(10000));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("acceptClientConnection"), _T("{\"requestStatus\": Accept}"), response));

	EXPECT_EQ(Core::ERROR_NONE, P2PGrpStart.Lock(10000));

	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_UNSUBSCRIBE(0, _T("onLaunchRequest"), _T("client.events"), message);

	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
}

TEST_F(MiracastServiceEventTest, P2P_ClientMode_DirectP2PGoNegotiationGroupStartWithoutName)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");

	EXPECT_EQ(string(""), plugin->Initialize(&service));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));

	EXPECT_CALL(*p_wrapsImplMock, popen(::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](const char* command, const char* type)
					{
					char buffer[1024] = {0};
					if ( 0 == strncmp(command,"/sbin/udhcpc -v -i",strlen("/sbin/udhcpc -v -i")))
					{
						const char* response = "udhcpc: sending select for 192.168.49.165\tudhcpc: lease of 192.168.49.165 obtained, lease time 3599\tdeleting routers\troute add default gw 192.168.49.1 dev lo\tadding dns 192.168.49.1";
						strncpy(buffer, response, sizeof(buffer) - 1);
						buffer[sizeof(buffer) - 1] = '\0';
					}
					size_t len = strnlen(buffer, sizeof(buffer));
					return (fmemopen(buffer, len, "r"));
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_request(::testing::_, ::testing::_, ::testing::_,::testing::_, ::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len, char *reply, size_t *reply_len, void(*msg_cb)(char *msg, size_t len))
					{
						if ( 0 == strncmp(cmd,"P2P_CONNECT",strlen("P2P_CONNECT")))
						{
							strncpy(reply,"OK",*reply_len);
						}
						return false;
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_recv(::testing::_, ::testing::_, ::testing::_))
	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-REQUEST 96:52:44:b6:7d:14 dev_passwd_id=4 go_intent=13", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-REQUEST 96:52:44:b6:7d:14 dev_passwd_id=4 go_intent=13", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-SUCCESS role=client freq=2437 ht40=0 x=96:52:44:b6:7d:14 peer_iface=96:52:44:b6:fd:14 wps_method=PBC", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GROUP-FORMATION-SUCCESS", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				// Here using lo to avoid the operation not permitted error for unknown interfaces
				strncpy(reply, "P2P-GROUP-STARTED lo client ssid=\"DIRECT-UU-Unknown\" freq=2437 psk=12c3ce3d8976152df796e5f42fc646723471bf1aab8d72a546fa3dce60dc14a3 go_dev_addr=96:52:44:b6:7d:14 [PERSISTENT]", *reply_len);
				return false;
				}))

	.WillRepeatedly(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				return true;
				}));

	Core::Event connectRequest(false, true);
	Core::Event P2PGrpStart(false, true);

	EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
		.Times(2)
		.WillOnce(::testing::Invoke(
					[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
					string text;
					EXPECT_TRUE(json->ToString(text));
					EXPECT_EQ(text,string(_T("{"
									"\"jsonrpc\":\"2.0\","
									"\"method\":\"client.events.onClientConnectionRequest\","
									"\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
									"\"name\":\"Miracast-Source\""
									"}}"
								)));
					connectRequest.SetEvent();
					return Core::ERROR_NONE;
					}))

	.WillOnce(::testing::Invoke(
				[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
				string text;
				EXPECT_TRUE(json->ToString(text));
				EXPECT_EQ(text,string(_T("{"
                                                                "\"jsonrpc\":\"2.0\","
                                                                "\"method\":\"client.events.onLaunchRequest\","
                                                                "\"params\":{\"device_parameters\":{\"source_dev_ip\":\"192.168.49.1\","
                                                                "\"source_dev_mac\":\"96:52:44:b6:7d:14\","
                                                                "\"source_dev_name\":\"Miracast-Source\","
                                                                "\"sink_dev_ip\":\"192.168.49.165\""
                                                                "}}}"
                                                        )));
                                P2PGrpStart.SetEvent();
				return Core::ERROR_NONE;
				}));

	EVENT_SUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_SUBSCRIBE(0, _T("onLaunchRequest"), _T("client.events"), message);

	EXPECT_EQ(Core::ERROR_NONE, connectRequest.Lock(10000));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("acceptClientConnection"), _T("{\"requestStatus\": Accept}"), response));

	EXPECT_EQ(Core::ERROR_NONE, P2PGrpStart.Lock(10000));

	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_UNSUBSCRIBE(0, _T("onLaunchRequest"), _T("client.events"), message);

	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
}

TEST_F(MiracastServiceEventTest, P2P_ClientMode_GENERIC_FAILURE)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");

	EXPECT_EQ(string(""), plugin->Initialize(&service));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));

	EXPECT_CALL(*p_wrapsImplMock, popen(::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](const char* command, const char* type)
					{
					char buffer[1024] = {0};
					if ( 0 == strncmp(command,"/sbin/udhcpc -v -i",strlen("/sbin/udhcpc -v -i")))
					{
						const char* response = "P2P GENERIC FAILURE";
						strncpy(buffer, response, sizeof(buffer) - 1);
						buffer[sizeof(buffer) - 1] = '\0';
					}
					size_t len = strnlen(buffer, sizeof(buffer));
					return (fmemopen(buffer, len, "r"));
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_request(::testing::_, ::testing::_, ::testing::_,::testing::_, ::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len, char *reply, size_t *reply_len, void(*msg_cb)(char *msg, size_t len))
					{
						if ( 0 == strncmp(cmd,"P2P_CONNECT",strlen("P2P_CONNECT")))
						{
							strncpy(reply,"OK",*reply_len);
						}
						return false;
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_recv(::testing::_, ::testing::_, ::testing::_))
		.WillOnce(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
					strncpy(reply, "P2P-DEVICE-FOUND 2c:33:58:9c:73:2d p2p_dev_addr=2c:33:58:9c:73:2d pri_dev_type=1-0050F200-0 name='Sample-Test-Android-1' config_methods=0x11e8 dev_capab=0x25 group_capab=0x82 wfd_dev_info=0x01101c440006 new=0", *reply_len);
					return false;
					}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-DEVICE-FOUND 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0 wfd_dev_info=0x01101c440032 vendor_elems=1 new=1", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-DEVICE-LOST 2c:33:58:9c:73:2d p2p_dev_addr=2c:33:58:9c:73:2d pri_dev_type=1-0050F200-0 name='Sample-Test-Android-1' config_methods=0x11e8 dev_capab=0x25 group_capab=0x82 wfd_dev_info=0x01101c440006 new=0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-PROV-DISC-PBC-REQ 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-PROV-DISC-PBC-REQ 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-REQUEST 96:52:44:b6:7d:14 dev_passwd_id=4 go_intent=13", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-SUCCESS role=client freq=2437 ht40=0 x=96:52:44:b6:7d:14 peer_iface=96:52:44:b6:fd:14 wps_method=PBC", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GROUP-FORMATION-SUCCESS", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-FIND-STOPPED", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				// Here using lo to avoid the operation not permitted error for unknown interfaces
				strncpy(reply, "P2P-GROUP-STARTED lo client ssid=\"DIRECT-UU-Galaxy A23 5G\" freq=2437 psk=12c3ce3d8976152df796e5f42fc646723471bf1aab8d72a546fa3dce60dc14a3 go_dev_addr=96:52:44:b6:7d:14 [PERSISTENT]", *reply_len);
				return false;
				}))

	.WillRepeatedly(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				return true;
				}));

	Core::Event connectRequest(false, true);
	Core::Event P2PGenericFail(false, true);

	EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
		.Times(2)
		.WillOnce(::testing::Invoke(
					[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
					string text;
					EXPECT_TRUE(json->ToString(text));
					EXPECT_EQ(text,string(_T("{"
									"\"jsonrpc\":\"2.0\","
									"\"method\":\"client.events.onClientConnectionRequest\","
									"\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
									"\"name\":\"Sample-Test-Android-2\""
									"}}"
								)));
					connectRequest.SetEvent();
					return Core::ERROR_NONE;
					}))

	.WillOnce(::testing::Invoke(
				[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
				string text;
				EXPECT_TRUE(json->ToString(text));
				EXPECT_EQ(text,string(_T("{"
                                                                "\"jsonrpc\":\"2.0\","
                                                                "\"method\":\"client.events.onClientConnectionError\","
                                                                "\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
                                                                "\"name\":\"Sample-Test-Android-2\","
                                                                "\"error_code\":\"104\","
                                                                "\"reason\":\"GENERIC_FAILURE\""
                                                                "}"
                                                                "}"
							)));
				P2PGenericFail.SetEvent();
				return Core::ERROR_NONE;
				}));


	EVENT_SUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_SUBSCRIBE(0, _T("onClientConnectionError"), _T("client.events"), message);

	EXPECT_EQ(Core::ERROR_NONE, connectRequest.Lock(10000));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("acceptClientConnection"), _T("{\"requestStatus\": Accept}"), response));

	EXPECT_EQ(Core::ERROR_NONE, P2PGenericFail.Lock(10000));

	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionError"), _T("client.events"), message);

	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
}

TEST_F(MiracastServiceEventTest, P2P_GOMode_GENERIC_FAILURE)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");

	EXPECT_EQ(string(""), plugin->Initialize(&service));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));

	EXPECT_CALL(*p_wrapsImplMock, popen(::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](const char* command, const char* type)
					{
					char buffer[1024] = {0};
					if ( 0 == strncmp(command,"awk '$4 == ",strlen("awk '$4 == ")))
					{
						strncpy(buffer, "192.168.59.165", sizeof(buffer) - 1);
						buffer[sizeof(buffer) - 1] = '\0';
					}
					else if ( 0 == strncmp(command,"awk '$1 == ",strlen("awk '$1 == ")))
					{
						// Need to return as empty
					}
					else if ( 0 == strncmp(command,"arping",strlen("arping")))
					{
						const char* response = "Received 0 response";
						strncpy(buffer, response, sizeof(buffer) - 1);
						buffer[sizeof(buffer) - 1] = '\0';
					}
					size_t len = strnlen(buffer, sizeof(buffer));
					return (fmemopen(buffer, len, "r"));
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_request(::testing::_, ::testing::_, ::testing::_,::testing::_, ::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len, char *reply, size_t *reply_len, void(*msg_cb)(char *msg, size_t len))
					{
						if ( 0 == strncmp(cmd,"P2P_CONNECT",strlen("P2P_CONNECT")))
						{
							strncpy(reply,"OK",*reply_len);
						}
						return false;
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_recv(::testing::_, ::testing::_, ::testing::_))
	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-DEVICE-FOUND 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0 wfd_dev_info=0x01101c440032 vendor_elems=1 new=1", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-PROV-DISC-PBC-REQ 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-REQUEST 96:52:44:b6:7d:14 dev_passwd_id=4 go_intent=13", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-SUCCESS role=client freq=2437 ht40=0 x=96:52:44:b6:7d:14 peer_iface=96:52:44:b6:fd:14 wps_method=PBC", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GROUP-FORMATION-SUCCESS", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				// Here using lo to avoid the operation not permitted error for unknown interfaces
				strncpy(reply, "P2P-GROUP-STARTED lo GO ssid=\"DIRECT-UU-Element-Xumo-TV\" freq=2437 psk=12c3ce3d8976152df796e5f42fc646723471bf1aab8d72a546fa3dce60dc14a3 go_dev_addr=96:52:44:b6:7d:14 ip_addr=192.168.49.200 ip_mask=255.255.255.0 go_ip_addr=192.168.49.1", *reply_len);
				return false;
				}))

	.WillRepeatedly(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				return true;
				}));

	Core::Event connectRequest(false, true);
	Core::Event P2PGenericFail(false, true);

	EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
		.Times(2)
		.WillOnce(::testing::Invoke(
					[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
					string text;
					EXPECT_TRUE(json->ToString(text));
					EXPECT_EQ(text,string(_T("{"
									"\"jsonrpc\":\"2.0\","
									"\"method\":\"client.events.onClientConnectionRequest\","
									"\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
									"\"name\":\"Sample-Test-Android-2\""
									"}}"
								)));
					connectRequest.SetEvent();
					return Core::ERROR_NONE;
					}))

		.WillOnce(::testing::Invoke(
					[&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
					string text;
					EXPECT_TRUE(json->ToString(text));
					EXPECT_EQ(text,string(_T("{"
												"\"jsonrpc\":\"2.0\","
												"\"method\":\"client.events.onClientConnectionError\","
												"\"params\":{\"mac\":\"96:52:44:b6:7d:14\","
												"\"name\":\"Sample-Test-Android-2\","
												"\"error_code\":\"104\","
												"\"reason\":\"GENERIC_FAILURE\""
												"}"
												"}"
											)));
					P2PGenericFail.SetEvent();
					return Core::ERROR_NONE;
					}));

	EVENT_SUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_SUBSCRIBE(0, _T("onClientConnectionError"), _T("client.events"), message);

	EXPECT_EQ(Core::ERROR_NONE, connectRequest.Lock(10000));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("acceptClientConnection"), _T("{\"requestStatus\": Accept}"), response));

	EXPECT_EQ(Core::ERROR_NONE, P2PGenericFail.Lock(10000));

	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionRequest"), _T("client.events"), message);
	EVENT_UNSUBSCRIBE(0, _T("onClientConnectionError"), _T("client.events"), message);

	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
}

TEST_F(MiracastServiceEventTest, P2P_GOMode_AutoConnect)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");
	createFile("/opt/miracast_autoconnect","GTest");

	EXPECT_EQ(string(""), plugin->Initialize(&service));
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));

	EXPECT_CALL(*p_wrapsImplMock, popen(::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](const char* command, const char* type)
					{
					char buffer[1024] = {0};
					if ( 0 == strncmp(command,"awk '$4 == ",strlen("awk '$4 == ")))
					{
						strncpy(buffer, "192.168.59.165", sizeof(buffer) - 1);
						buffer[sizeof(buffer) - 1] = '\0';
					}
					else if ( 0 == strncmp(command,"awk '$1 == ",strlen("awk '$1 == ")))
					{
						// Need to return as empty
					}
					else if ( 0 == strncmp(command,"arping",strlen("arping")))
					{
						const char* response = "Unicast reply from 192.168.59.165 [96:52:44:b6:7d:14]  2.189ms\nReceived 1 response";
						strncpy(buffer, response, sizeof(buffer) - 1);
						buffer[sizeof(buffer) - 1] = '\0';
					}
					size_t len = strnlen(buffer, sizeof(buffer));
					return (fmemopen(buffer, len, "r"));
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_request(::testing::_, ::testing::_, ::testing::_,::testing::_, ::testing::_, ::testing::_))
		.Times(::testing::AnyNumber())
		.WillRepeatedly(::testing::Invoke(
					[&](struct wpa_ctrl *ctrl, const char *cmd, size_t cmd_len, char *reply, size_t *reply_len, void(*msg_cb)(char *msg, size_t len))
					{
						if ( 0 == strncmp(cmd,"P2P_CONNECT",strlen("P2P_CONNECT")))
						{
							strncpy(reply,"OK",*reply_len);
						}
						return false;
					}));

	EXPECT_CALL(*p_wrapsImplMock, wpa_ctrl_recv(::testing::_, ::testing::_, ::testing::_))
	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-DEVICE-FOUND 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0 wfd_dev_info=0x01101c440032 vendor_elems=1 new=1", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-PROV-DISC-PBC-REQ 96:52:44:b6:7d:14 p2p_dev_addr=96:52:44:b6:7d:14 pri_dev_type=10-0050F204-5 name='Sample-Test-Android-2' config_methods=0x188 dev_capab=0x25 group_capab=0x0", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-REQUEST 96:52:44:b6:7d:14 dev_passwd_id=4 go_intent=13", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GO-NEG-SUCCESS role=client freq=2437 ht40=0 x=96:52:44:b6:7d:14 peer_iface=96:52:44:b6:fd:14 wps_method=PBC", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-GROUP-FORMATION-SUCCESS", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				strncpy(reply, "P2P-FIND-STOPPED", *reply_len);
				return false;
				}))

	.WillOnce(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				// Here using lo to avoid the operation not permitted error for unknown interfaces
				strncpy(reply, "P2P-GROUP-STARTED lo GO ssid=\"DIRECT-UU-Element-Xumo-TV\" freq=2437 psk=12c3ce3d8976152df796e5f42fc646723471bf1aab8d72a546fa3dce60dc14a3 go_dev_addr=96:52:44:b6:7d:14 ip_addr=192.168.49.200 ip_mask=255.255.255.0 go_ip_addr=192.168.49.1", *reply_len);
				return false;
				}))

	.WillRepeatedly(::testing::Invoke(
				[&](struct wpa_ctrl *ctrl, char *reply, size_t *reply_len) {
				return true;
				}));

	sleep(10);

	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
	removeFile("/opt/miracast_autoconnect");
}

TEST_F(MiracastServiceEventTest, powerStateChange)
{
	createFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	createFile("/var/run/wpa_supplicant/p2p0","p2p0");

	EXPECT_EQ(string(""), plugin->Initialize(&service));

	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));

	Plugin::MiracastServiceImplementation::_instance->onPowerModeChanged(WPEFramework::Exchange::IPowerManager::POWER_STATE_ON, WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY_DEEP_SLEEP);
    Plugin::MiracastServiceImplementation::_instance->onPowerModeChanged(WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY_DEEP_SLEEP, WPEFramework::Exchange::IPowerManager::POWER_STATE_ON);
    sleep(5);
    Plugin::MiracastServiceImplementation::_instance->onPowerModeChanged(WPEFramework::Exchange::IPowerManager::POWER_STATE_ON, WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY_DEEP_SLEEP);
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": false}"), response));
    Plugin::MiracastServiceImplementation::_instance->onPowerModeChanged(WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY_DEEP_SLEEP, WPEFramework::Exchange::IPowerManager::POWER_STATE_ON);
	EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnable"), _T("{\"enabled\": true}"), response));
	plugin->Deinitialize(nullptr);

	removeEntryFromFile("/etc/device.properties","WIFI_P2P_CTRL_INTERFACE=p2p0");
	removeFile("/var/run/wpa_supplicant/p2p0");
}
