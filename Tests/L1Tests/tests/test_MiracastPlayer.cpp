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

#include "MiracastPlayer.h"

#include "FactoriesImplementation.h"
#include "ServiceMock.h"
#include "ThunderPortability.h"

#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include "COMLinkMock.h"
#include "WrapsMock.h"
#include "WorkerPoolImplementation.h"
#include "MiracastPlayerImplementation.h"

namespace WPEFramework { namespace Plugin {
bool isValidEnvName(const std::string& name);
bool isValidEnvValue(const std::string& value);
std::string sanitizeShellArgument(const std::string& input);
} }

TEST(MiracastPlayerSecurityTest, RejectsUnsafeEnvironmentAndShellInputs)
{
    EXPECT_TRUE(WPEFramework::Plugin::isValidEnvName("SAFE_NAME"));
    EXPECT_FALSE(WPEFramework::Plugin::isValidEnvName("BAD-NAME"));
    EXPECT_FALSE(WPEFramework::Plugin::isValidEnvName(""));
    EXPECT_TRUE(WPEFramework::Plugin::isValidEnvValue("safe-value_1"));
    EXPECT_FALSE(WPEFramework::Plugin::isValidEnvValue("$(command)"));
    EXPECT_FALSE(WPEFramework::Plugin::isValidEnvValue("value\nnext"));
    EXPECT_EQ("", WPEFramework::Plugin::sanitizeShellArgument("device;name_1"));
    EXPECT_EQ("", WPEFramework::Plugin::sanitizeShellArgument("`$()&|<>"));
}
#include <sys/time.h>
#include <future>
#include <thread>

using namespace WPEFramework;

using ::testing::NiceMock;
namespace {

#define RTSP_SEND (1)
#define RTSP_RECV (2)

#define BUFFER_SIZE (2048)
#define PORT (7236)
#define TEST_LOG(FMT, ...) log(__func__, __FILE__, __LINE__, syscall(__NR_gettid),FMT,##__VA_ARGS__)

    typedef enum rtsp_srcmsg_reqresp
    {
        RTSP_SEND_M1_REQUEST = 0x00,
        RTSP_RECV_M1_RESPONSE,
        RTSP_RECV_M2_REQUEST,
        RTSP_SEND_M2_RESPONSE,
        RTSP_SEND_M3_REQUEST,
        RTSP_RECV_M3_RESPONSE,
        RTSP_SEND_M4_REQUEST,
        RTSP_RECV_M4_RESPONSE,
        RTSP_SEND_M5_REQUEST,
        RTSP_RECV_M5_RESPONSE,
        RTSP_RECV_M6_REQUEST,
        RTSP_SEND_M6_RESPONSE,
        RTSP_RECV_M7_REQUEST,
        RTSP_SEND_M7_RESPONSE,
        RTSP_SEND_M16_REQUEST,
        RTSP_RECV_M16_RESPONSE,
        RTSP_SEND_TEARDOWN_REQUEST,
        RTSP_RECV_TEADOWN_RESPONSE,
        RTSP_RECV_TEARDOWN_REQUEST,
        RTSP_SEND_TEARDOWN_RESPONSE
    }
    RTSP_SRCMSG_REQ_RESP;

    typedef struct rtsp_src_msg_handler_format
    {
        int rtsp_sendorreceive;
        RTSP_SRCMSG_REQ_RESP rtsp_msg_type;
        const char* template_name;
    }
    RTSP_MSG_HANDLER_FORMAT;

    RTSP_MSG_HANDLER_FORMAT default_rtsp_srcMsgbuffer[] =
    {
        { RTSP_SEND , RTSP_SEND_M1_REQUEST , "OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nServer: AllShareCast/Galaxy/Android13\r\nRequire: org.wfa.wfd1.0\r\n"},
        { RTSP_RECV , RTSP_RECV_M1_RESPONSE , "RTSP/1.0 200 OK\r\nPublic: \"org.wfa.wfd1.0, GET_PARAMETER, SET_PARAMETER\"\r\nCSeq: 1\r\n\r\n"},
        { RTSP_RECV , RTSP_RECV_M2_REQUEST , "OPTIONS * RTSP/1.0\r\nRequire: org.wfa.wfd1.0\r\nCSeq: %s"},
        { RTSP_SEND , RTSP_SEND_M2_RESPONSE , "RTSP/1.0 200 OK\r\nCSeq: %s\r\nPublic: org.wfa.wfd1.0, SETUP, TEARDOWN, PLAY, PAUSE, GET_PARAMETER, SET_PARAMETER\r\n" },
        { RTSP_SEND , RTSP_SEND_M3_REQUEST , "GET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0\r\nCSeq: 2\r\nContent-Type: text/parameters\r\nContent-Length: 211\r\n\r\nwfd_video_formats\r\nwfd_audio_codecs\r\nwfd_uibc_capability\r\nwfd_client_rtp_ports\r\nwfd_content_protection\r\nwfd_sec_screensharing\r\nwfd_sec_portrait_display\r\nwfd_sec_rotation\r\nwfd_sec_hw_rotation\r\nwfd_sec_framerate\r\n" },
        { RTSP_RECV , RTSP_RECV_M3_RESPONSE , "RTSP/1.0 200 OK\r\nContent-Length: 210\r\nContent-Type: text/parameters\r\nCSeq: 2\r\n\r\nwfd_content_protection: none\r\nwfd_video_formats: 00 00 03 10 0001ffff 1fffffff 00001fff 00 0000 0000 10 none none\r\nwfd_audio_codecs: AAC 00000007 00\r\nwfd_client_rtp_ports: RTP/AVP/UDP;unicast 1991 0 mode=play\r\n" },
        { RTSP_SEND , RTSP_SEND_M4_REQUEST , "SET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0\r\nCSeq: 3\r\nContent-Type: text/parameters\r\nContent-Length: 246\r\n\r\nwfd_video_formats: 00 00 02 04 00000080 00000000 00000000 00 0000 0000 00 none none\r\nwfd_audio_codecs: AAC 00000001 00\r\nwfd_presentation_URL: rtsp://192.168.49.1/wfd1.0/streamid=0 none\r\nwfd_client_rtp_ports: RTP/AVP/UDP;unicast 1990 0 mode=play\r\n" },
        { RTSP_RECV , RTSP_RECV_M4_RESPONSE , "RTSP/1.0 200 OK\r\nCSeq: 3\r\n" },
        { RTSP_SEND , RTSP_SEND_M5_REQUEST , "SET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0\r\nCSeq: 4\r\nContent-Type: text/parameters\r\nContent-Length: 27\r\n\r\nwfd_trigger_method: SETUP\r\n" },
        { RTSP_RECV , RTSP_RECV_M5_RESPONSE , "RTSP/1.0 200 OK\r\nCSeq: 4\r\n\r\n" },
        { RTSP_RECV , RTSP_RECV_M6_REQUEST , "SETUP rtsp://192.168.49.1/wfd1.0/streamid=0 RTSP/1.0\r\nTransport: RTP/AVP/UDP;unicast;client_port=1990\r\nCSeq: %s\r\n"},
        { RTSP_SEND , RTSP_SEND_M6_RESPONSE , "RTSP/1.0 200 OK\r\nCSeq: %s\r\nSession: 1804289383;timeout=30\r\nTransport: RTP/AVP/UDP;unicast;client_port=1991-1992;server_port=19000-19001\r\n" },
        { RTSP_RECV , RTSP_RECV_M7_REQUEST , "RTSP/1.0 200 OK\r\nCSeq: %s\r\nSession: 1804289383;timeout=30\r\nRange: npt=now-\r\n" },
        { RTSP_SEND , RTSP_SEND_M7_RESPONSE , "RTSP/1.0 200 OK\r\nCSeq: %s\r\nSession: 1804289383;timeout=30\r\nRange: npt=now-\r\n" },
        { RTSP_SEND , RTSP_SEND_M16_REQUEST , "GET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0\r\nCSeq: 5\r\nSession: 1804289383\r\n"},
        { RTSP_RECV , RTSP_RECV_M16_RESPONSE , "RTSP/1.0 200 OK\r\nCSeq: 5\r\n" }
        //{ RTSP_SEND , RTSP_SEND_TEARDOWN_REQUEST , "SET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0\r\nCSeq: 6\r\nContent-Type: text/parameters\r\nContent-Length: 30\r\n\r\nwfd_trigger_method: TEARDOWN\r\n" },
        //{ RTSP_RECV , RTSP_RECV_TEADOWN_RESPONSE , "RTSP/1.0 200 OK\r\nCSeq: 6\r\n" }
    };

    int default_rtsp_srcMsgSize = static_cast<int>(sizeof(default_rtsp_srcMsgbuffer) / sizeof(default_rtsp_srcMsgbuffer[0]));
    int server_fd = -1, client_fd = -1;
    struct sockaddr_in server_addr, client_addr;
    int opt = 1;

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

    static void removeFile(const char* fileName)
    {
        if (std::remove(fileName) != 0)
        {
            TEST_LOG("ERROR: deleting File [%s] ...",strerror(errno));
        }
        else
        {
            TEST_LOG("File %s successfully deleted", fileName);
        }
    }

    static void createFile(const char* fileName, const char* fileContent)
    {
        removeFile(fileName);

        std::ofstream fileContentStream(fileName);
        fileContentStream << fileContent;
        fileContentStream << "\n";
        fileContentStream.close();
        TEST_LOG("File %s successfully created", fileName);
    }

    const char* get_RequestResponseFormat(RTSP_MSG_HANDLER_FORMAT *pstRTSPSrcHldrFmt , int index , size_t rtsp_msg_fmt_count )
    {
        if (index >= 0 && index < static_cast<int>(rtsp_msg_fmt_count))
        {
            return pstRTSPSrcHldrFmt[index].template_name;
        }
        return "";
    }

    std::string parse_received_parser_field_value(std::string rtsp_msg_buffer , const char* given_tag )
    {
        std::string seq_str = "";
        std::stringstream ss(rtsp_msg_buffer);
        std::string prefix = "";
        std::string line;

        while (std::getline(ss, line))
        {
            if (line.find(given_tag) != std::string::npos)
            {
                prefix = given_tag;
                seq_str = line.substr(prefix.length());
                REMOVE_R(seq_str);
                REMOVE_N(seq_str);
                break;
            }
        }
        return seq_str;
    }

    void send_rtsp_msg( int sockfd , const std::string &msg_buffer )
    {
        usleep(500000);
        TEST_LOG("Send Msg[%lu][%s]...",msg_buffer.size(),msg_buffer.c_str());
        ssize_t bytes_sent = send(sockfd, msg_buffer.c_str(), msg_buffer.size(), 0);

        if (bytes_sent < 0)
        {
            TEST_LOG("ERROR: send failed [%s]...", strerror(errno));
        }
        else if (static_cast<size_t>(bytes_sent) < msg_buffer.size())
        {
            // Partial send occurred
            TEST_LOG("Partial message sent: %ld bytes out of %lu", bytes_sent, msg_buffer.size());
        }
        else
        {
            TEST_LOG("Sent %ld bytes successfully", bytes_sent);
        }
    }

    /*
    * Wait for data returned by the socket for specified time
    */
    bool wait_data_timeout(int m_Sockfd, unsigned int ms)
    {
        struct timeval timeout = {0};
        fd_set readFDSet;
        bool returnValue = false;

        FD_ZERO(&readFDSet);
        FD_SET(m_Sockfd, &readFDSet);

        timeout.tv_sec = (ms / 1000);
        timeout.tv_usec = ((ms % 1000) * 1000);

        if (select(m_Sockfd + 1, &readFDSet, nullptr, nullptr, &timeout) > 0)
        {
            returnValue = FD_ISSET(m_Sockfd, &readFDSet);
        }
        return returnValue;
    }

    bool recv_rtsp_msg(int socket_fd, void *buffer, size_t buffer_len, int* bytes_processed_ptr )
    {
        int recv_return = -1;
        bool status = true;

        if (nullptr == bytes_processed_ptr)
        {
            TEST_LOG("ERROR: Invalid Argument");
            return false;
        }

        TEST_LOG("Entering ");
        if (!wait_data_timeout(socket_fd, 15000 ))
        {
            TEST_LOG("ERROR: Exiting Timeout ");
            return false;
        }
        else
        {
            recv_return = recv(socket_fd, buffer, buffer_len, 0);
        }

        if (recv_return <= 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                TEST_LOG("ERROR:recv timed out ...");
            }
            else
            {
                TEST_LOG("ERROR: recv failed [%s] ...",strerror(errno));
            }
            status = false;
            *bytes_processed_ptr = 0;
        }
        else
        {
            *bytes_processed_ptr = recv_return;
            TEST_LOG("recv string [%s][%d] ...", (char*)buffer,recv_return);
        }
        return status;
    }

    bool initialize_ServerSocket(void)
    {
        // Create socket file descriptor
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("socket failed");
            return false;
        }

        if (fcntl(server_fd, F_SETFL, O_NONBLOCK) < 0) {
            perror("fcntl failed");
            close(server_fd);
            server_fd = -1;
            return false;
        }
        TEST_LOG("#### NON_BLOCKING Socket Enabled ####");

        // Forcefully attaching socket to the port 7236
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
            perror("setsockopt");
            close(server_fd);
            server_fd = -1;
            return false;
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(PORT);

        // Bind the socket to localhost port 7236
        if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("bind failed");
            close(server_fd);
            server_fd = -1;
            return false;
        }
        return true;
    }

    void release_SocketDescriptor(void)
    {
        if ( -1 != server_fd )
        {
            close(server_fd);
            server_fd = -1;
        }
        if ( -1 != client_fd )
        {
            close(client_fd);
            client_fd = -1;
        }
    }

    void runRTSPSourceHandler(RTSP_MSG_HANDLER_FORMAT* custom_src_msg_buffer, size_t rtsp_msg_count , std::string & response_buffer )
    {
        int addrlen = sizeof(server_addr);
        char buffer[BUFFER_SIZE] = {0};

        response_buffer = "FAIL";

        if( -1 == server_fd )
        {
            return;
        }
        // Listen for incoming connections
        if (listen(server_fd, 3) < 0) {
            perror("listen");
            return;
        }

        if (!wait_data_timeout(server_fd, 30000 ))
        {
            // connection timed out or failed
            TEST_LOG("Socket Connection Timedout %s received(%d)...", strerror(errno), errno);
            response_buffer = "FAIL";
            return;
        }
        else
        {
            // Accept an incoming connection
            if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&addrlen)) < 0) {
                perror("accept");
                return;
            }
            TEST_LOG("socket accept done");
        }

        response_buffer = "SUCCESS";

        std::string receivedCSeqNum = "";
        size_t current_msg = 0;

        while ( current_msg < rtsp_msg_count )
        {
            RTSP_SRCMSG_REQ_RESP rtsp_msg_type = custom_src_msg_buffer[current_msg].rtsp_msg_type;
            int rtsp_sendorreceive = custom_src_msg_buffer[current_msg].rtsp_sendorreceive;
            const char* rtsp_req_resp_format = get_RequestResponseFormat(custom_src_msg_buffer,current_msg,rtsp_msg_count);
            std::string msg_buffer = "";

            memset( buffer , 0x00 , sizeof(buffer));
            TEST_LOG("Index[%zu] RTSP Msg[%x]",current_msg,rtsp_msg_type);

            if ( RTSP_SEND == rtsp_sendorreceive )
            {
                TEST_LOG("RTSP_SEND");
                switch (rtsp_msg_type)
                {
                    case RTSP_SEND_M1_REQUEST:
                    case RTSP_SEND_M3_REQUEST:
                    case RTSP_SEND_M4_REQUEST:
                    case RTSP_SEND_M5_REQUEST:
                    case RTSP_SEND_M16_REQUEST:
                    case RTSP_SEND_TEARDOWN_REQUEST:
                        {
                            TEST_LOG("RTSP_SEND REQUEST Messages");
                            msg_buffer = rtsp_req_resp_format;
                        }
                        break;
                    case RTSP_SEND_M2_RESPONSE:
                    case RTSP_SEND_M6_RESPONSE:
                    case RTSP_SEND_M7_RESPONSE:
                    case RTSP_SEND_TEARDOWN_RESPONSE:
                        {
                            std::string temp_buffer = rtsp_req_resp_format;

                            TEST_LOG("RTSP_SEND RESPONSE Messages");
                            if (temp_buffer.find("%s") != std::string::npos)
                            {
                                sprintf( buffer , rtsp_req_resp_format , receivedCSeqNum.c_str());
                                msg_buffer = buffer;
                                TEST_LOG("Response sequence number replaced as [%s]",receivedCSeqNum.c_str());
                                receivedCSeqNum.clear();
                            }
                            else
                            {
                                msg_buffer = rtsp_req_resp_format;
                            }
                        }
                        break;
                    default:
                        {

                        }
                        break;
                }
                if ( !msg_buffer.empty())
                {
                    send_rtsp_msg( client_fd , msg_buffer );
                }
            }
            else
            {
                int bytes_processed = 0;
                TEST_LOG("RTSP_RECV");
                bool status = recv_rtsp_msg( client_fd , buffer , sizeof(buffer), &bytes_processed);
                buffer[bytes_processed] = '\0'; // Null-terminate the received string
                msg_buffer = buffer;
                if ((false == status ) || (msg_buffer.find("TEARDOWN") == 0))
                {
                    TEST_LOG("TEARDOWN initiated from Sink Device[%x]",status);
                    response_buffer = "SUCCESS";
                    break;
                }
                switch (rtsp_msg_type)
                {
                    case RTSP_RECV_M1_RESPONSE:
                    case RTSP_RECV_M4_RESPONSE:
                    case RTSP_RECV_M5_RESPONSE:
                    case RTSP_RECV_M16_RESPONSE:
                    case RTSP_RECV_TEADOWN_RESPONSE:
                        {
                            msg_buffer = rtsp_req_resp_format;

                            if ( 0 != strncmp( buffer , rtsp_req_resp_format , strlen(rtsp_req_resp_format)))
                            {
                                response_buffer = "FAIL";
                                TEST_LOG("ERROR: expected[%s] actual[%s]",rtsp_req_resp_format,buffer);
                            }
                            else if ( strlen(buffer) > strlen(rtsp_req_resp_format) )
                            {
                                if (( RTSP_RECV_M1_RESPONSE == rtsp_msg_type ) ||
                                    ( RTSP_RECV_M5_RESPONSE == rtsp_msg_type ))
                                {
                                    int processedBytes = strlen(rtsp_req_resp_format),
                                        totalLen = strlen(buffer);
                                    std::string temp_buffer = buffer;
                                    std::string response = temp_buffer.substr(0,processedBytes);
                                    EXPECT_EQ(response,string(rtsp_req_resp_format));
                                    std::string request = temp_buffer.substr(processedBytes, totalLen - processedBytes);
                                    receivedCSeqNum = parse_received_parser_field_value( request , "CSeq: " );
                                    TEST_LOG("Parsed Response + Request[%s][%s]",response.c_str(),request.c_str());
                                    TEST_LOG("processedBytes[%d]totalLen[%d]",processedBytes,totalLen);
                                    EXPECT_TRUE(!receivedCSeqNum.empty());
                                    TEST_LOG("Skipping to RTSP_RECV REQUEST Messages");
                                    ++current_msg;
                                }
                            }
                        }
                        break;
                    case RTSP_RECV_M3_RESPONSE:
                        {
                            TEST_LOG("RTSP_RECV M3 RESPONSE Messages");
                            std::string expected_sequence_number = parse_received_parser_field_value( msg_buffer , "CSeq: " ),
                                actual_sequence_number = parse_received_parser_field_value( rtsp_req_resp_format , "CSeq: " );

                            if ( 0 != expected_sequence_number.compare(actual_sequence_number))
                            {
                                response_buffer = "FAIL";
                                TEST_LOG("Error: expected[%s]actual[%s]",msg_buffer.c_str(),rtsp_req_resp_format);
                            }
                        }
                        break;
                    case RTSP_RECV_M2_REQUEST:
                    case RTSP_RECV_M6_REQUEST:
                    case RTSP_RECV_M7_REQUEST:
                        {
                            TEST_LOG("RTSP_RECV REQUEST Messages");
                            receivedCSeqNum = parse_received_parser_field_value( std::move(msg_buffer) , "CSeq: " );
                        }
                        break;
                    default:
                        {
                            break;
                        }
                }
            }
            ++current_msg;

            if ( 0 == response_buffer.compare("FAIL"))
            {
                TEST_LOG("ERROR: RESPONSE FAILED");
                break;
            }
        }

        if ( 0 == response_buffer.compare("FAIL"))
        {
            TEST_LOG("ERROR: RTSP Msg Exchange Failed");
        }
        else
        {
            TEST_LOG("RTSP Msg Exchange Success");
        }
    }
}

class MiracastPlayerTest : public ::testing::Test {
protected:
    Core::ProxyType<Plugin::MiracastPlayer> plugin;
    Core::JSONRPC::Handler& handler;
    DECL_CORE_JSONRPC_CONX connection;
    Core::JSONRPC::Message message;
    string response;

    WrapsImplMock *p_wrapsImplMock = nullptr;
    Core::ProxyType<Plugin::MiracastPlayerImplementation> miracastPlayerImpl;

    NiceMock<COMLinkMock> comLinkMock;
    NiceMock<ServiceMock> service;
    PLUGINHOST_DISPATCHER* dispatcher;
    Core::ProxyType<WorkerPoolImplementation> workerPool;
    
    NiceMock<FactoriesImplementation> factoriesImplementation;

    MiracastPlayerTest()
        : plugin(Core::ProxyType<Plugin::MiracastPlayer>::Create())
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
                            miracastPlayerImpl = Core::ProxyType<Plugin::MiracastPlayerImplementation>::Create();
                            TEST_LOG("Pass created miracastPlayerImpl: %p ", &miracastPlayerImpl);
                            return &miracastPlayerImpl;
                    }));
        #else
            ON_CALL(comLinkMock, Instantiate(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
                .WillByDefault(::testing::Return(miracastPlayerImpl));
        #endif /*USE_THUNDER_R4 */
        
        PluginHost::IFactories::Assign(&factoriesImplementation);

        Core::IWorkerPool::Assign(&(*workerPool));
        workerPool->Run();

        dispatcher = static_cast<PLUGINHOST_DISPATCHER*>(
        plugin->QueryInterface(PLUGINHOST_DISPATCHER_ID));
        dispatcher->Activate(&service);

        EXPECT_EQ(string(""), plugin->Initialize(&service));
    }
    virtual ~MiracastPlayerTest() override
    {
        TEST_LOG("MiracastPlayerTest Destructor");

        plugin->Deinitialize(&service);

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
        IarmBus::setImpl(nullptr);
    }
};

class MiracastPlayerEventTest : public MiracastPlayerTest {
protected:
    NiceMock<ServiceMock> service;
    NiceMock<FactoriesImplementation> factoriesImplementation;
    PLUGINHOST_DISPATCHER* dispatcher;
    Core::JSONRPC::Message message;

    MiracastPlayerEventTest()
        : MiracastPlayerTest()
    {
        PluginHost::IFactories::Assign(&factoriesImplementation);

        dispatcher = static_cast<PLUGINHOST_DISPATCHER*>(
            plugin->QueryInterface(PLUGINHOST_DISPATCHER_ID));
        dispatcher->Activate(&service);
    }

    virtual ~MiracastPlayerEventTest() override
    {
        dispatcher->Deactivate();
        dispatcher->Release();

        PluginHost::IFactories::Assign(nullptr);
    }
};

TEST_F(MiracastPlayerTest, RegisteredMethods)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("playRequest")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("stopRequest")));
    EXPECT_EQ(Core::ERROR_NONE, handler.Exists(_T("setVideoRectangle")));
}

TEST_F(MiracastPlayerTest, GetInformation)
{
    EXPECT_EQ("This MiracastPlayer Plugin Facilitates Miracast session like RTSP communication and GStreamer Playback", plugin->Information());
}

TEST_F(MiracastPlayerEventTest, APP_REQUESTED_TO_STOP)
{
    std::string rtsp_response = "";
    Core::Event Initiated(false, true);
    Core::Event Inprogress(false, true);
    Core::Event Playing(false, true);
    Core::Event Stopped(false, true);

    EXPECT_TRUE(initialize_ServerSocket());
    std::thread serverThread = std::thread([&]() { runRTSPSourceHandler( default_rtsp_srcMsgbuffer , default_rtsp_srcMsgSize , rtsp_response ); });

    EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillOnce(::testing::Invoke(
                    [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                    string text;
                    EXPECT_TRUE(json->ToString(text));
                    EXPECT_EQ(text,string(_T("{"
                            "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                            "\"params\":{\"name\":\"Sample-Android-Test-1\","
                            "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                            "\"state\":\"INITIATED\","
                            "\"reason_code\":\"200\","
                            "\"reason\":\"SUCCESS\""
                            "}}"
                        )));
                    Initiated.SetEvent();
                    return Core::ERROR_NONE;
                    }))
    .WillOnce(::testing::Invoke(
                [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json){
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text,string(_T("{"
                        "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                        "\"params\":{\"name\":\"Sample-Android-Test-1\","
                        "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                        "\"state\":\"INPROGRESS\","
                        "\"reason_code\":\"200\","
                        "\"reason\":\"SUCCESS\""
                        "}}"
                    )));
                Inprogress.SetEvent();
                return Core::ERROR_NONE;
                }))
    .WillOnce(::testing::Invoke(
                [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text,string(_T("{"
                        "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                        "\"params\":{\"name\":\"Sample-Android-Test-1\","
                        "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                        "\"state\":\"PLAYING\","
                        "\"reason_code\":\"200\","
                        "\"reason\":\"SUCCESS\""
                        "}}"
                    )));
                Playing.SetEvent();
                return Core::ERROR_NONE;
                }))
    .WillOnce(::testing::Invoke(
                [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text,string(_T("{"
                        "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                        "\"params\":{\"name\":\"Sample-Android-Test-1\","
                        "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                        "\"state\":\"STOPPED\","
                        "\"reason_code\":\"201\","
                        "\"reason\":\"APP_REQ_TO_STOP\""
                        "}}"
                    )));
                Stopped.SetEvent();
                return Core::ERROR_NONE;
                }));

    EVENT_SUBSCRIBE(0, _T("onStateChange"), _T("client.events"), message);

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("playRequest"), _T("{\"device_parameters\": {\"source_dev_ip\":\"127.0.0.1\",\"source_dev_mac\": \"A1:B2:C3:D4:E5:F6\",\"source_dev_name\":\"Sample-Android-Test-1\",\"sink_dev_ip\":\"192.168.59.1\"},\"video_rectangle\": {\"X\": 0,\"Y\" : 0,\"W\": 1920,\"H\": 1080}}"), response));

    EXPECT_EQ(Core::ERROR_NONE, Initiated.Lock(10000));
    EXPECT_EQ(Core::ERROR_NONE, Inprogress.Lock(10000));

    serverThread.join();
    EXPECT_EQ(rtsp_response, string("SUCCESS"));

    EXPECT_EQ(Core::ERROR_NONE, Playing.Lock(10000));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setVideoRectangle"), _T("{\"X\": 0,\"Y\": 0,\"W\": 1280,\"H\": 720}"), response));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setVideoRectangle"), _T("{\"X\": 0,\"Y\": 0,\"W\": 1280,\"H\": 720}"), response));
    EXPECT_EQ(response, string("{\"message\":\"Invalid Video Rectangle Coords\",\"success\":false}"));

    sleep(2);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("stopRequest"), _T("{\"reason_code\": 303}"), response));
    EXPECT_EQ(response, string("{\"message\":\"UNKNOWN STOP REASON CODE RECEIVED\",\"success\":false}"));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("stopRequest"), _T("{\"reason_code\": 300}"), response));
    
    EXPECT_EQ(Core::ERROR_NONE, Stopped.Lock(10000));
    release_SocketDescriptor();

    EVENT_UNSUBSCRIBE(0, _T("onStateChange"), _T("client.events"), message);
}

TEST_F(MiracastPlayerEventTest, APP_REQ_TO_STOP_FOR_NEW_CONNECTION)
{
    std::string rtsp_response = "";
    Core::Event Initiated(false, true);
    Core::Event Inprogress(false, true);
    Core::Event Playing(false, true);
    Core::Event Stopped(false, true);

    EXPECT_TRUE(initialize_ServerSocket());
    std::thread serverThread = std::thread([&]() { runRTSPSourceHandler( default_rtsp_srcMsgbuffer , default_rtsp_srcMsgSize , rtsp_response ); });

    EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillOnce(::testing::Invoke(
                    [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                    string text;
                    EXPECT_TRUE(json->ToString(text));
                    EXPECT_EQ(text,string(_T("{"
                            "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                            "\"params\":{\"name\":\"Sample-Android-Test-1\","
                            "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                            "\"state\":\"INITIATED\","
                            "\"reason_code\":\"200\","
                            "\"reason\":\"SUCCESS\""
                            "}}"
                        )));
                    Initiated.SetEvent();
                    return Core::ERROR_NONE;
                    }))
    .WillOnce(::testing::Invoke(
                [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text,string(_T("{"
                        "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                        "\"params\":{\"name\":\"Sample-Android-Test-1\","
                        "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                        "\"state\":\"INPROGRESS\","
                        "\"reason_code\":\"200\","
                        "\"reason\":\"SUCCESS\""
                        "}}"
                    )));
                Inprogress.SetEvent();
                return Core::ERROR_NONE;
                }))
    .WillOnce(::testing::Invoke(
                [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text,string(_T("{"
                        "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                        "\"params\":{\"name\":\"Sample-Android-Test-1\","
                        "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                        "\"state\":\"PLAYING\","
                        "\"reason_code\":\"200\","
                        "\"reason\":\"SUCCESS\""
                        "}}"
                    )));
                Playing.SetEvent();
                return Core::ERROR_NONE;
                }))
    .WillOnce(::testing::Invoke(
                [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text,string(_T("{"
                        "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                        "\"params\":{\"name\":\"Sample-Android-Test-1\","
                        "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                        "\"state\":\"STOPPED\","
                        "\"reason_code\":\"208\","
                        "\"reason\":\"NEW_SRC_DEV_CONNECT_REQ\""
                        "}}"
                    )));
                Stopped.SetEvent();
                return Core::ERROR_NONE;
                }));

    EVENT_SUBSCRIBE(0, _T("onStateChange"), _T("client.events"), message);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("playRequest"), _T("{\"device_parameters\": {\"source_dev_ip\":\"127.0.0.1\",\"source_dev_mac\": \"A1:B2:C3:D4:E5:F6\",\"source_dev_name\":\"Sample-Android-Test-1\",\"sink_dev_ip\":\"192.168.59.1\"},\"video_rectangle\": {\"X\": 0,\"Y\" : 0,\"W\": 1920,\"H\": 1080}}"), response));

    EXPECT_EQ(Core::ERROR_NONE, Initiated.Lock(10000));
    EXPECT_EQ(Core::ERROR_NONE, Inprogress.Lock(10000));

    serverThread.join();
    EXPECT_EQ(rtsp_response, string("SUCCESS"));

    EXPECT_EQ(Core::ERROR_NONE, Playing.Lock(10000));

    sleep(2);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("stopRequest"), _T("{\"reason_code\": 303}"), response));
    EXPECT_EQ(response, string("{\"message\":\"UNKNOWN STOP REASON CODE RECEIVED\",\"success\":false}"));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("stopRequest"), _T("{\"reason_code\": 301}"), response));
    
    EXPECT_EQ(Core::ERROR_NONE, Stopped.Lock(10000));
    release_SocketDescriptor();

    EVENT_UNSUBSCRIBE(0, _T("onStateChange"), _T("client.events"), message);
}

TEST_F(MiracastPlayerEventTest, SRC_DEV_REQUESTED_TO_STOP)
{
    std::string rtsp_response = "";
    Core::Event Initiated(false, true);
    Core::Event Inprogress(false, true);
    Core::Event Playing(false, true);
    Core::Event Stopped(false, true);

    EXPECT_TRUE(initialize_ServerSocket());
    std::thread serverThread = std::thread([&]() { runRTSPSourceHandler( default_rtsp_srcMsgbuffer , default_rtsp_srcMsgSize , rtsp_response ); });

    EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillOnce(::testing::Invoke(
                    [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                    string text;
                    EXPECT_TRUE(json->ToString(text));
                    EXPECT_EQ(text,string(_T("{"
                            "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                            "\"params\":{\"name\":\"Sample-Android-Test-1\","
                            "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                            "\"state\":\"INITIATED\","
                            "\"reason_code\":\"200\","
                            "\"reason\":\"SUCCESS\""
                            "}}"
                        )));
                    Initiated.SetEvent();
                    return Core::ERROR_NONE;
                    }))
    .WillOnce(::testing::Invoke(
                [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text,string(_T("{"
                        "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                        "\"params\":{\"name\":\"Sample-Android-Test-1\","
                        "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                        "\"state\":\"INPROGRESS\","
                        "\"reason_code\":\"200\","
                        "\"reason\":\"SUCCESS\""
                        "}}"
                    )));
                Inprogress.SetEvent();
                return Core::ERROR_NONE;
                }))
    .WillOnce(::testing::Invoke(
                [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text,string(_T("{"
                        "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                        "\"params\":{\"name\":\"Sample-Android-Test-1\","
                        "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                        "\"state\":\"PLAYING\","
                        "\"reason_code\":\"200\","
                        "\"reason\":\"SUCCESS\""
                        "}}"
                    )));
                Playing.SetEvent();
                return Core::ERROR_NONE;
                }))
    .WillOnce(::testing::Invoke(
                [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text,string(_T("{"
                        "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                        "\"params\":{\"name\":\"Sample-Android-Test-1\","
                        "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                        "\"state\":\"STOPPED\","
                        "\"reason_code\":\"202\","
                        "\"reason\":\"SRC_DEV_REQ_TO_STOP\""
                        "}}"
                    )));
                Stopped.SetEvent();
                return Core::ERROR_NONE;
                }));

    EVENT_SUBSCRIBE(0, _T("onStateChange"), _T("client.events"), message);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("playRequest"), _T("{\"device_parameters\": {\"source_dev_ip\":\"127.0.0.1\",\"source_dev_mac\": \"A1:B2:C3:D4:E5:F6\",\"source_dev_name\":\"Sample-Android-Test-1\",\"sink_dev_ip\":\"192.168.59.1\"},\"video_rectangle\": {\"X\": 0,\"Y\" : 0,\"W\": 1920,\"H\": 1080}}"), response));

    EXPECT_EQ(Core::ERROR_NONE, Initiated.Lock(10000));
    EXPECT_EQ(Core::ERROR_NONE, Inprogress.Lock(10000));

    serverThread.join();
    EXPECT_EQ(rtsp_response, string("SUCCESS"));

    EXPECT_EQ(Core::ERROR_NONE, Playing.Lock(10000));

    char buffer[BUFFER_SIZE] = {0};
    std::string teardown_request = "SET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0\r\nCSeq: 6\r\nContent-Type: text/parameters\r\nContent-Length: 30\r\n\r\nwfd_trigger_method: TEARDOWN\r\n";
    std::string teardown_response = "RTSP/1.0 200 OK\r\nCSeq: 6\r\n\r\n",
                temp_buffer = "";
    int processed_bytes = 0;

    send_rtsp_msg(client_fd,teardown_request);
    recv_rtsp_msg( client_fd , buffer , sizeof(buffer), &processed_bytes);
    buffer[processed_bytes] = '\0'; // Null-terminate the received string
    temp_buffer = buffer;

    EXPECT_EQ(teardown_response, temp_buffer);
    
    EXPECT_EQ(Core::ERROR_NONE, Stopped.Lock(10000));
    release_SocketDescriptor();

    EVENT_UNSUBSCRIBE(0, _T("onStateChange"), _T("client.events"), message);
}

TEST_F(MiracastPlayerEventTest, RTSP_TimeOut)
{
    std::string rtsp_response = "";
    Core::Event Initiated(false, true);
    Core::Event Inprogress(false, true);
    Core::Event Stopped(false, true);
    RTSP_MSG_HANDLER_FORMAT rtsp_srcMsgbuffer[] =
    {
        { RTSP_SEND , RTSP_SEND_M1_REQUEST , "OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nServer: AllShareCast/Galaxy/Android13\r\nRequire: org.wfa.wfd1.0\r\n"},
        { RTSP_RECV , RTSP_RECV_M1_RESPONSE , "RTSP/1.0 200 OK\r\nPublic: \"org.wfa.wfd1.0, GET_PARAMETER, SET_PARAMETER\"\r\nCSeq: 1\r\n\r\n"},
        { RTSP_RECV , RTSP_RECV_M2_REQUEST , "OPTIONS * RTSP/1.0\r\nRequire: org.wfa.wfd1.0\r\nCSeq: %s"},
        { RTSP_SEND , RTSP_SEND_M2_RESPONSE , "RTSP/1.0 200 OK\r\nCSeq: %s\r\nPublic: org.wfa.wfd1.0, SETUP, TEARDOWN, PLAY, PAUSE, GET_PARAMETER, SET_PARAMETER\r\nGET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0\r\nCSeq: 2\r\nContent-Type: text/parameters\r\nContent-Length: 211\r\n\r\nwfd_video_formats\r\nwfd_audio_codecs\r\nwfd_uibc_capability\r\nwfd_client_rtp_ports\r\nwfd_content_protection\r\nwfd_sec_screensharing\r\nwfd_sec_portrait_display\r\nwfd_sec_rotation\r\nwfd_sec_hw_rotation\r\nwfd_sec_framerate\r\n" },
        { RTSP_RECV , RTSP_RECV_M3_RESPONSE , "RTSP/1.0 200 OK\r\nContent-Length: 210\r\nContent-Type: text/parameters\r\nCSeq: 2\r\n\r\nwfd_content_protection: none\r\nwfd_video_formats: 00 00 03 10 0001ffff 1fffffff 00001fff 00 0000 0000 10 none none\r\nwfd_audio_codecs: AAC 00000007 00\r\nwfd_client_rtp_ports: RTP/AVP/UDP;unicast 1991 0 mode=play\r\n" }
    };

    int rtsp_srcMsgSize = static_cast<int>(sizeof(rtsp_srcMsgbuffer) / sizeof(rtsp_srcMsgbuffer[0]));

    EXPECT_TRUE(initialize_ServerSocket());
    std::thread serverThread = std::thread([&]() { runRTSPSourceHandler( rtsp_srcMsgbuffer , rtsp_srcMsgSize , rtsp_response ); });

    EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillOnce(::testing::Invoke(
                    [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                    string text;
                    EXPECT_TRUE(json->ToString(text));
                    EXPECT_EQ(text,string(_T("{"
                            "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                            "\"params\":{\"name\":\"Sample-Android-Test-1\","
                            "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                            "\"state\":\"INITIATED\","
                            "\"reason_code\":\"200\","
                            "\"reason\":\"SUCCESS\""
                            "}}"
                        )));
                    Initiated.SetEvent();
                    return Core::ERROR_NONE;
                    }))
    .WillOnce(::testing::Invoke(
                [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text,string(_T("{"
                        "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                        "\"params\":{\"name\":\"Sample-Android-Test-1\","
                        "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                        "\"state\":\"INPROGRESS\","
                        "\"reason_code\":\"200\","
                        "\"reason\":\"SUCCESS\""
                        "}}"
                    )));
                Inprogress.SetEvent();
                return Core::ERROR_NONE;
                }))
    .WillOnce(::testing::Invoke(
                [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text,string(_T("{"
                        "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                        "\"params\":{\"name\":\"Sample-Android-Test-1\","
                        "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                        "\"state\":\"STOPPED\","
                        "\"reason_code\":\"204\","
                        "\"reason\":\"RTSP_TIMEOUT\""
                        "}}"
                    )));
                Stopped.SetEvent();
                return Core::ERROR_NONE;
                }));

    EVENT_SUBSCRIBE(0, _T("onStateChange"), _T("client.events"), message);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("playRequest"), _T("{\"device_parameters\": {\"source_dev_ip\":\"127.0.0.1\",\"source_dev_mac\": \"A1:B2:C3:D4:E5:F6\",\"source_dev_name\":\"Sample-Android-Test-1\",\"sink_dev_ip\":\"192.168.59.1\"},\"video_rectangle\": {\"X\": 0,\"Y\" : 0,\"W\": 1920,\"H\": 1080}}"), response));

    EXPECT_EQ(Core::ERROR_NONE, Initiated.Lock(10000));
    EXPECT_EQ(Core::ERROR_NONE, Inprogress.Lock(10000));

    serverThread.join();
    EXPECT_EQ(rtsp_response, string("SUCCESS"));

    EXPECT_EQ(Core::ERROR_NONE, Stopped.Lock(25000));

    release_SocketDescriptor();

    EVENT_UNSUBSCRIBE(0, _T("onStateChange"), _T("client.events"), message);
}

TEST_F(MiracastPlayerEventTest, RTSP_Failure)
{
    std::string rtsp_response = "";
    Core::Event Initiated(false, true);
    Core::Event Inprogress(false, true);
    Core::Event Stopped(false, true);
    RTSP_MSG_HANDLER_FORMAT rtsp_srcMsgbuffer[] =
    {
        { RTSP_SEND , RTSP_SEND_M1_REQUEST , "OPTIONS * RTSP/1.0\r\nCSeq: 1\r\nServer: AllShareCast/Galaxy/Android13\r\nRequire: org.wfa.wfd1.0\r\n"},
        { RTSP_RECV , RTSP_RECV_M1_RESPONSE , "RTSP/1.0 200 OK\r\nPublic: \"org.wfa.wfd1.0, GET_PARAMETER, SET_PARAMETER\"\r\nCSeq: 1\r\n\r\n"},
        { RTSP_RECV , RTSP_RECV_M2_REQUEST , "OPTIONS * RTSP/1.0\r\nRequire: org.wfa.wfd1.0\r\nCSeq: %s"},
        { RTSP_SEND , RTSP_SEND_M2_RESPONSE , "RTSP/1.0 200 OK\r\nCSeq: %s\r\nPublic: org.wfa.wfd1.0, SETUP, TEARDOWN, PLAY, PAUSE, GET_PARAMETER, SET_PARAMETER\r\n" },
        { RTSP_SEND , RTSP_SEND_M3_REQUEST , "GET_PARAMETER rtsp://localhost/wfd1.0 RTSP/1.0\r\nCSeq: 2\r\nContent-Type: text/parameters\r\nContent-Length: 211\r\n\r\nwfd_video_formats\r\nwfd_audio_codecs\r\nwfd_uibc_capability\r\nwfd_client_rtp_ports\r\nwfd_content_protection\r\nwfd_sec_screensharing\r\n" },
        { RTSP_SEND , RTSP_SEND_M3_REQUEST , "wfd_sec_portrait_display\r\nwfd_sec_rotation\r\nwfd_sec_hw_rotation\r\nwfd_sec_framerate\r\n" }
    };

    int rtsp_srcMsgSize = static_cast<int>(sizeof(rtsp_srcMsgbuffer) / sizeof(rtsp_srcMsgbuffer[0]));

    EXPECT_TRUE(initialize_ServerSocket());
    std::thread serverThread = std::thread([&]() { runRTSPSourceHandler( rtsp_srcMsgbuffer , rtsp_srcMsgSize , rtsp_response ); });

    EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillOnce(::testing::Invoke(
                    [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                    string text;
                    EXPECT_TRUE(json->ToString(text));
                    EXPECT_EQ(text,string(_T("{"
                            "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                            "\"params\":{\"name\":\"Sample-Android-Test-1\","
                            "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                            "\"state\":\"INITIATED\","
                            "\"reason_code\":\"200\","
                            "\"reason\":\"SUCCESS\""
                            "}}"
                        )));
                    Initiated.SetEvent();
                    return Core::ERROR_NONE;
                    }))
    .WillOnce(::testing::Invoke(
                [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text,string(_T("{"
                        "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                        "\"params\":{\"name\":\"Sample-Android-Test-1\","
                        "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                        "\"state\":\"INPROGRESS\","
                        "\"reason_code\":\"200\","
                        "\"reason\":\"SUCCESS\""
                        "}}"
                    )));
                Inprogress.SetEvent();
                return Core::ERROR_NONE;
                }))
    .WillOnce(::testing::Invoke(
                [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text,string(_T("{"
                        "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                        "\"params\":{\"name\":\"Sample-Android-Test-1\","
                        "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                        "\"state\":\"STOPPED\","
                        "\"reason_code\":\"203\","
                        "\"reason\":\"RTSP_FAILURE\""
                        "}}"
                    )));
                Stopped.SetEvent();
                return Core::ERROR_NONE;
                }));

    EVENT_SUBSCRIBE(0, _T("onStateChange"), _T("client.events"), message);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("playRequest"), _T("{\"device_parameters\": {\"source_dev_ip\":\"127.0.0.1\",\"source_dev_mac\": \"A1:B2:C3:D4:E5:F6\",\"source_dev_name\":\"Sample-Android-Test-1\",\"sink_dev_ip\":\"192.168.59.1\"},\"video_rectangle\": {\"X\": 0,\"Y\" : 0,\"W\": 1920,\"H\": 1080}}"), response));

    EXPECT_EQ(Core::ERROR_NONE, Initiated.Lock(10000));
    EXPECT_EQ(Core::ERROR_NONE, Inprogress.Lock(10000));

    serverThread.join();
    EXPECT_EQ(rtsp_response, string("SUCCESS"));
    sleep(2);
    release_SocketDescriptor();
    EXPECT_EQ(Core::ERROR_NONE, Stopped.Lock(10000));

    EVENT_UNSUBSCRIBE(0, _T("onStateChange"), _T("client.events"), message);
}

TEST_F(MiracastPlayerEventTest, setPlayerState)
{
    std::string rtsp_response = "";
    Core::Event Initiated(false, true);
    Core::Event Inprogress(false, true);
    Core::Event Playing(false, true);
    Core::Event Stopped(false, true);

    EXPECT_TRUE(initialize_ServerSocket());
    std::thread serverThread = std::thread([&]() { runRTSPSourceHandler( default_rtsp_srcMsgbuffer , default_rtsp_srcMsgSize , rtsp_response ); });

    EXPECT_CALL(service, Submit(::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillOnce(::testing::Invoke(
                    [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                    string text;
                    EXPECT_TRUE(json->ToString(text));
                    EXPECT_EQ(text,string(_T("{"
                            "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                            "\"params\":{\"name\":\"Sample-Android-Test-1\","
                            "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                            "\"state\":\"INITIATED\","
                            "\"reason_code\":\"200\","
                            "\"reason\":\"SUCCESS\""
                            "}}"
                        )));
                    Initiated.SetEvent();
                    return Core::ERROR_NONE;
                    }))
    .WillOnce(::testing::Invoke(
                [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text,string(_T("{"
                        "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                        "\"params\":{\"name\":\"Sample-Android-Test-1\","
                        "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                        "\"state\":\"INPROGRESS\","
                        "\"reason_code\":\"200\","
                        "\"reason\":\"SUCCESS\""
                        "}}"
                    )));
                Inprogress.SetEvent();
                return Core::ERROR_NONE;
                }))
    .WillOnce(::testing::Invoke(
                [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text,string(_T("{"
                        "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                        "\"params\":{\"name\":\"Sample-Android-Test-1\","
                        "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                        "\"state\":\"PLAYING\","
                        "\"reason_code\":\"200\","
                        "\"reason\":\"SUCCESS\""
                        "}}"
                    )));
                Playing.SetEvent();
                return Core::ERROR_NONE;
                }))
    .WillOnce(::testing::Invoke(
                [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text,string(_T("{"
                        "\"jsonrpc\":\"2.0\",\"method\":\"client.events.onStateChange\","
                        "\"params\":{\"name\":\"Sample-Android-Test-1\","
                        "\"mac\":\"A1:B2:C3:D4:E5:F6\","
                        "\"state\":\"STOPPED\","
                        "\"reason_code\":\"200\","
                        "\"reason\":\"SUCCESS\""
                        "}}"
                    )));
                Stopped.SetEvent();
                return Core::ERROR_NONE;
                }));

    EVENT_SUBSCRIBE(0, _T("onStateChange"), _T("client.events"), message);
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("playRequest"), _T("{\"device_parameters\": {\"source_dev_ip\":\"127.0.0.1\",\"source_dev_mac\": \"A1:B2:C3:D4:E5:F6\",\"source_dev_name\":\"Sample-Android-Test-1\",\"sink_dev_ip\":\"192.168.59.1\"},\"video_rectangle\": {\"X\": 0,\"Y\" : 0,\"W\": 1920,\"H\": 1080}}"), response));

    EXPECT_EQ(Core::ERROR_NONE, Initiated.Lock(10000));
    EXPECT_EQ(Core::ERROR_NONE, Inprogress.Lock(10000));

    serverThread.join();
    EXPECT_EQ(rtsp_response, string("SUCCESS"));

    EXPECT_EQ(Core::ERROR_NONE, Playing.Lock(10000));
        
    char buffer[BUFFER_SIZE] = {0};
    RTSP_HLDR_MSGQ_STRUCT rtsp_hldr_msgq_data = {0};
    std::string trigger_response = "",
            temp_buffer = "";

    if (Plugin::MiracastPlayerImplementation::_instance->m_miracast_rtsp_obj)
    {
        int processed_bytes = 0;
        rtsp_hldr_msgq_data.state = RTSP_PAUSE_FROM_SINK2SRC;
        Plugin::MiracastPlayerImplementation::_instance->m_miracast_rtsp_obj->send_msgto_rtsp_msg_hdler_thread(rtsp_hldr_msgq_data);
        recv_rtsp_msg( client_fd , buffer , sizeof(buffer), &processed_bytes);
        buffer[processed_bytes] = '\0'; // Null-terminate the received string
        temp_buffer = buffer;
        std::string receivedCSeqNum = parse_received_parser_field_value( temp_buffer , "CSeq: " );
        EXPECT_TRUE(temp_buffer.find("PAUSE") == 0);
        trigger_response = "RTSP/1.0 200 OK\r\nCSeq: " + receivedCSeqNum + "\r\n\r\n";
        send_rtsp_msg(client_fd,trigger_response);
        receivedCSeqNum.clear();
        memset(buffer,0x00,sizeof(buffer));

        sleep(2);

        rtsp_hldr_msgq_data.state = RTSP_PLAY_FROM_SINK2SRC;
        Plugin::MiracastPlayerImplementation::_instance->m_miracast_rtsp_obj->send_msgto_rtsp_msg_hdler_thread(rtsp_hldr_msgq_data);
        recv_rtsp_msg( client_fd , buffer , sizeof(buffer), &processed_bytes);
        buffer[processed_bytes] = '\0'; // Null-terminate the received string
        temp_buffer = buffer;
        receivedCSeqNum = parse_received_parser_field_value( temp_buffer , "CSeq: " );
        EXPECT_TRUE(temp_buffer.find("PLAY") == 0);
        trigger_response = "RTSP/1.0 200 OK\r\nCSeq: " + receivedCSeqNum + "\r\n\r\n";
        send_rtsp_msg(client_fd,trigger_response);
        receivedCSeqNum.clear();
        memset(buffer,0x00,sizeof(buffer));

        sleep(2);

        rtsp_hldr_msgq_data.state = RTSP_TEARDOWN_FROM_SINK2SRC;
        Plugin::MiracastPlayerImplementation::_instance->m_miracast_rtsp_obj->send_msgto_rtsp_msg_hdler_thread(rtsp_hldr_msgq_data);
        recv_rtsp_msg( client_fd , buffer , sizeof(buffer), &processed_bytes);
        buffer[processed_bytes] = '\0'; // Null-terminate the received string
        temp_buffer = buffer;
        receivedCSeqNum = parse_received_parser_field_value( temp_buffer , "CSeq: " );
        EXPECT_TRUE(temp_buffer.find("TEARDOWN") == 0);
        trigger_response = "RTSP/1.0 200 OK\r\nCSeq: " + receivedCSeqNum + "\r\n\r\n";
        send_rtsp_msg(client_fd,trigger_response);
        receivedCSeqNum.clear();
        memset(buffer,0x00,sizeof(buffer));
        EXPECT_EQ(Core::ERROR_NONE, Stopped.Lock(10000));
        release_SocketDescriptor();
    }
    EVENT_UNSUBSCRIBE(0, _T("onStateChange"), _T("client.events"), message);
}

TEST_F(MiracastPlayerTest, AutoConnectOptFlag)
{
    std::string rtsp_response = "";
    createFile("/opt/miracast_autoconnect","GTest");

    sleep(2);
    EXPECT_TRUE(initialize_ServerSocket());
    std::thread serverThread = std::thread([&]() { runRTSPSourceHandler( default_rtsp_srcMsgbuffer , default_rtsp_srcMsgSize , rtsp_response ); });

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("playRequest"), _T("{\"device_parameters\": {\"source_dev_ip\":\"127.0.0.1\",\"source_dev_mac\": \"A1:B2:C3:D4:E5:F6\",\"source_dev_name\":\"Sample-Android-Test-1\",\"sink_dev_ip\":\"192.168.59.1\"},\"video_rectangle\": {\"X\": 0,\"Y\" : 0,\"W\": 1920,\"H\": 1080}}"), response));

    serverThread.join();
    sleep(2);
    release_SocketDescriptor();
    removeFile("/opt/miracast_autoconnect");
    sleep(2);
}

TEST_F(MiracastPlayerTest, SetOrUnsetEnvArguments)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setWesterosEnvironment"), _T("{\"westerosArgs\":[{\"argName\":\"WAYLAND_DISPLAY\",\"argValue\":\"westeros-testplayer-0\"},{\"argName\":\"XDG_RUNTIME_DIR\",\"argValue\":\"/tmp\"}],\"appName\":\"MiracastApp\"}"), response));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("unsetWesterosEnvironment"), _T("{}"), response));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setWesterosEnvironment"), _T("{\"westerosArgs\":[{\"argName\":\"DISPLAY\",\"argValue\":\"display-testplayer-0\"},{\"argName\":\"XDG_RUNTIME_DIR\",\"argValue\":\"/tmp\"}],\"appName\":\"MiracastApp\"}"), response));
    EXPECT_EQ(response, string("{\"message\":\"Failed, Missing Wayland Display Name\",\"success\":false}"));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnvArguments"), _T("{\"envArgs\":[{\"argName\":\"WAYLAND_DISPLAY\",\"argValue\":\"westeros-testplayer-0\"},{\"argName\":\"XDG_RUNTIME_DIR\",\"argValue\":\"/tmp\"}],\"appName\":\"MiracastApp\"}"), response));
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("unsetEnvArguments"), _T("{}"), response));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("setEnvArguments"), _T("{\"envArgs\":[{\"argName\":\"DISPLAY\",\"argValue\":\"display-testplayer-0\"},{\"argName\":\"XDG_RUNTIME_DIR\",\"argValue\":\"/tmp\"}],\"appName\":\"MiracastApp\"}"), response));
    EXPECT_EQ(response, string("{\"message\":\"Failed, Missing Wayland Display Name\",\"success\":false}"));
}
