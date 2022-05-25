#ifndef __HN_MGMT_PROXY_H__
#define __HN_MGMT_PROXY_H__

#include <string>
#include <vector>

//#include "Poco/Net/HTTPServer.h"
#include "Poco/Net/HTTPRequestHandler.h"
//#include "Poco/Net/HTTPRequestHandlerFactory.h"
//#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"
//#include "Poco/Net/ServerSocket.h"
#include "Poco/URI.h"
#include "Poco/String.h"
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <hnode2/HNRestHandler.h>

namespace pjs = Poco::JSON;
namespace pdy = Poco::Dynamic;
namespace pn = Poco::Net;

// Define these locally so it is not in the header file.
class HNProxyHandler: public pn::HTTPRequestHandler
{
    private:
        HNOperationData *m_opData;

    public:
        HNProxyHandler( HNOperationData *operationData );
       ~HNProxyHandler();

        void handleRequest( pn::HTTPServerRequest& request, pn::HTTPServerResponse& response );
};

class HNProxyHandlerFactory
{
    public:
        HNProxyHandlerFactory();

        HNRestPath *addPath( std::string dispatchID, std::string operationID, HNRestDispatchInterface *dispatchInf );

        HNProxyHandler *createRequestHandler( const HNProxyRequest &request );
        
        //pn::HTTPRequestHandler* createRequestHandler( const pn::HTTPServerRequest& request );

    private:
        std::vector< HNRestPath > pathList;
};

class HNMgmtProxy
{
    public:
        HNMgmtProxy();
       ~HNMgmtProxy();

        void registerEndpointsFromOpenAPI( std::string dispatchID, HNRestDispatchInterface *dispatchInf, std::string openAPIJson );
        void registerProxyEndpoint( std::string dispatchID, HNRestDispatchInterface *dispatchInf, std::string opID, std::string rootURI );        

        void startRequest( HNProxyRequest *reqObj, HNProxyResponse *rspObj );

    private:

        HNProxyHandlerFactory m_factory;

        //void *m_srvPtr;
};

#endif // __HN_MGMT_PROXY_H__
