#ifndef __HN_MANAGEMENT_DEVICE_H__
#define __HN_MANAGEMENT_DEVICE_H__

#include <string>
#include <vector>
#include <set>

#include "Poco/Util/ServerApplication.h"
#include "Poco/Util/OptionSet.h"

#include <hnode2/HNodeDevice.h>
#include <hnode2/HNodeConfig.h>
#include <hnode2/HNAvahiBrowser.h>
#include <hnode2/HNSigSyncQueue.h>

#include "HNSCGISink.h"
#include "HNManagedDeviceArbiter.h"
#include "HNMgmtProxy.h"

#define MAXEVENTS  8

#define HNODE_MGMT_DEF_INSTANCE  "default"
#define HNODE_MGMT_DEVTYPE   "hnode2-management-device"

typedef enum HNManagementDeviceResultEnum
{
  HNMD_RESULT_SUCCESS,
  HNMD_RESULT_FAILURE
}HNMD_RESULT_T;

class HNManagementDevice : public Poco::Util::ServerApplication, public HNDEPDispatchInf
{
    private:
        bool _helpRequested   = false;
        bool _debugLogging    = false;
        bool _instancePresent = false;

        std::string _instance; 

        std::string m_instanceName;

        int epollFD;
    
        struct epoll_event event;
        struct epoll_event *events;

        HNodeDevice m_hnodeDev;

        HNManagedDeviceArbiter arbiter;
        HNSCGISink             reqsink;
        HNProxySequencer       m_proxySeq;
        
        HNSigSyncQueue         m_scgiRequestQueue;

        HNSigSyncQueue         m_proxyResponseQueue;

        std::vector< HNRestPath > m_proxyPathList;

        bool quit;

        void displayHelp();

        HNMD_RESULT_T addSocketToEPoll( int sfd );
        HNMD_RESULT_T removeSocketFromEPoll( int sfd );

        HNRestPath* addProxyPath( std::string dispatchID, std::string operationID, HNRestDispatchInterface *dispatchInf );
        void registerProxyEndpointsFromOpenAPI( std::string openAPIJson );
        HNProxyTicket* checkForProxyRequest( HNProxyHTTPReqRsp *reqRR );
        HNOperationData* mapProxyRequest( HNProxyHTTPReqRsp *reqRR );
        void handleLocalSCGIRequest( HNProxyHTTPReqRsp *reqRR, HNOperationData *opData );

        bool configExists();
        HNMD_RESULT_T initConfig();
        HNMD_RESULT_T readConfig();
        HNMD_RESULT_T updateConfig();

    protected:
        
        // HNDevice REST callback
        virtual void dispatchEP( HNodeDevice *parent, HNOperationData *opData );

        void defineOptions( Poco::Util::OptionSet& options );
        void handleOption( const std::string& name, const std::string& value );
        int main( const std::vector<std::string>& args );

};

#endif // __HN_MANAGEMENT_DEVICE_H__
