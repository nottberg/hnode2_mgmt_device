#ifndef _HN_SCGI_SINK_H_
#define _HN_SCGI_SINK_H_

#include <sys/epoll.h>

#include <string>
#include <map>
#include <set>

#include "hnode2/HNodeID.h"

// Forward declaration
class HNSCGIRunner;

typedef enum HNSCGISinkResultEnum
{
    HNSS_RESULT_SUCCESS,
    HNSS_RESULT_FAILURE
}HNSS_RESULT_T;

class HNSCGISink
{

    private:

        // A map of known hnode2 devices
        //std::map< std::string, HNMDARecord > mdrMap;

        std::string m_instanceName;
        
        // The thread helper
        void *m_thelp;

        // Should the monitor still be running.
        bool m_runMonitor;

        int m_epollFD;
        int m_acceptFD;
    
        struct epoll_event m_event;
        struct epoll_event *m_events;

        std::set< int > m_clientSet;

        HNSS_RESULT_T openSCGISocket();

        HNSS_RESULT_T addSocketToEPoll( int sfd );
        HNSS_RESULT_T removeSocketFromEPoll( int sfd );
        HNSS_RESULT_T processNewClientConnections();
        HNSS_RESULT_T closeClientConnection( int clientFD );
        HNSS_RESULT_T processClientRequest( int cfd );

    protected:
        void runSCGILoop();
        void killSCGILoop();

    public:
        HNSCGISink();
       ~HNSCGISink();

        //HNMDL_RESULT_T notifyDiscoverAdd( HNMDARecord &record );
        //HNMDL_RESULT_T notifyDiscoverRemove( HNMDARecord &record );

        void start( std::string instance );
        void shutdown();

        void debugPrint();

    friend HNSCGIRunner;
};

#endif // _HN_MANAGED_DEVICE_ARBITER_H_
