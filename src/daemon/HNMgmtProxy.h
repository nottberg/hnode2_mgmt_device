#ifndef __HN_MGMT_PROXY_H__
#define __HN_MGMT_PROXY_H__

#include <sys/epoll.h>

#include <string>
#include <map>
#include <list>
#include <fstream>
#include <string>
#include <vector>

//#include "Poco/Net/HTTPServer.h"
//#include "Poco/Net/HTTPRequestHandler.h"
//#include "Poco/Net/HTTPRequestHandlerFactory.h"
//#include "Poco/Net/HTTPServerParams.h"
//#include "Poco/Net/HTTPServerRequest.h"
//#include "Poco/Net/HTTPServerResponse.h"
//#include "Poco/Net/ServerSocket.h"
//#include "Poco/URI.h"
//#include "Poco/String.h"
//#include <Poco/JSON/Object.h>
//#include <Poco/JSON/Parser.h>

#include <hnode2/HNSigSyncQueue.h>

#include "HNProxyReqRsp.h"

//namespace pjs = Poco::JSON;
//namespace pdy = Poco::Dynamic;
//namespace pn = Poco::Net;

typedef enum HNProxySequencerResultEnum
{
    HNPS_RESULT_SUCCESS,
    HNPS_RESULT_FAILURE
}HNPS_RESULT_T;

// A class to track the progress of a proxy request
class HNProxyTicket
{
    public:
        HNProxyTicket( HNProxyHTTPReqRsp *parentRR );
       ~HNProxyTicket();

        void setCRC32ID( std::string id );
        void setAddress( std::string addr );
        void setPort( uint16_t port );

        std::string getCRC32ID();
        std::string getAddress();
        uint16_t getPort();
        std::string getPath();

        HNProxyHTTPReqRsp* getRR();

    private:
        HNProxyHTTPReqRsp  *m_parentRR;

        std::string  m_crc32ID;
        std::string  m_address;
        uint16_t     m_port;


};

// Perform the proxy request operations
class HNProxySequencer
{
    public:
        HNProxySequencer();
       ~HNProxySequencer();

        void setParentResponseQueue( HNSigSyncQueue *parentResponseQueue );
        HNSigSyncQueue* getRequestQueue();

        void start();
        void runProxySequencerLoop();
        void shutdown();
        void killProxySequencerLoop();

        HNPS_RESULT_T addSocketToEPoll( int sfd );
        HNPS_RESULT_T removeSocketFromEPoll( int sfd );

        HNPS_RESULT_T executeProxyRequest( HNProxyTicket *request );

    private:
            // The thread helper
        void *m_thelp;

        // Should the monitor still be running.
        bool m_runMonitor;

        int m_epollFD;
        int m_acceptFD;
    
        struct epoll_event m_event;
        struct epoll_event *m_events;

        HNSigSyncQueue  m_requestQueue;

        HNSigSyncQueue *m_responseQueue;
};

#endif // __HN_MGMT_PROXY_H__
