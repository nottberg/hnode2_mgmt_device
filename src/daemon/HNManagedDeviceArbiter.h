#ifndef _HN_MANAGED_DEVICE_ARBITER_H_
#define _HN_MANAGED_DEVICE_ARBITER_H_

#include <string>
#include <map>

#include "hnode2/HNodeID.h"

// Forward declaration for friend class below
class HNMDARunner;

typedef enum HNManagedDeviceRecordManagementStateEnum
{
    HNMDR_MGMT_STATE_NOTSET,        // Default value
    HNMDR_MGMT_STATE_DISCOVERED,    // Added via Avahi Discovery
    HNMDR_MGMT_STATE_RECOVERED,     // Added from config file, prior association
    HNMDR_MGMT_STATE_SELF,          // This record is for myself - the management device itself
    HNMDR_MGMT_STATE_OPT_INFO,      // REST read to aquire basic operating info
    HNMDR_MGMT_STATE_OWNER_INFO,    // REST read for current ownership
    HNMDR_MGMT_STATE_UNCLAIMED,     // Device is waiting to be claimed 
    HNMDR_MGMT_STATE_NOT_AVAILABLE, // Device is not currently owned, but not available for claiming 
    HNMDR_MGMT_STATE_OTHER_MGR,     // Device is currently owner by other manager
    HNMDR_MGMT_STATE_ACTIVE,        // Device is active, responding to period health checks  
    HNMDR_MGMT_STATE_OWNER_CLAIM,   // REST write to establish device management
    HNMDR_MGMT_STATE_OWNER_AFFIRM,  // REST write to reassert management configuration settings.
    HNMDR_MGMT_STATE_DISAPPEARING,  // Avahi notification that device is offline
    HNMDR_MGMT_STATE_OFFLINE        // Recent attempts to contact device have been unsuccessful
}HNMDR_MGMT_STATE_T;

//typedef enum HNManagedDeviceRecordOwnerStateEnum
//{
//    HNMDR_OWNER_STATE_NOTSET,
//    HNMDR_OWNER_STATE_UNKNOWN,
//}HNMDR_OWNER_STATE_T;

typedef enum HNManagedDeviceListResultEnum
{
    HNMDL_RESULT_SUCCESS,
    HNMDL_RESULT_FAILURE
}HNMDL_RESULT_T;

typedef enum HNMDARAddressTypeEnum
{
    HMDAR_ADDRTYPE_NOTSET,
    HMDAR_ADDRTYPE_UNKNOWN,
    HMDAR_ADDRTYPE_IPV4,
    HNDAR_ADDRTYPE_LOOPBACK_IPV4,
    HNDAR_ADDRTYPE_CAST_IPV4,
    HNDAR_ADDRTYPE_INET_IPV4,
    HMDAR_ADDRTYPE_IPV6,
    HNDAR_ADDRTYPE_LOOPBACK_IPV6,
    HNDAR_ADDRTYPE_CAST_IPV6,
    HNDAR_ADDRTYPE_INET_IPV6 
}HMDAR_ADDRTYPE_T;

class HNMDARAddress
{
    private:
        HMDAR_ADDRTYPE_T   m_type;
        std::string        m_dnsName;
        std::string        m_address;
        uint16_t           m_port;

    public:
        HNMDARAddress();
       ~HNMDARAddress();

        void setAddressInfo( std::string dnsName, std::string address, uint16_t port );

        HMDAR_ADDRTYPE_T  getType();
        std::string getTypeAsStr();

        std::string getDNSName();
        std::string getAddress();
        uint16_t    getPort();

        std::string getURL( std::string protocol, std::string path );

        void debugPrint( uint offset );
};

class HNMDARecord
{
    private:
        HNMDR_MGMT_STATE_T   m_mgmtState;
        //HNMDR_OWNER_STATE_T  ownershipState;

        std::string discID;
        HNodeID     hnodeID;
        std::string devType;
        std::string devVersion;
        std::string name;

        std::vector< HNMDARAddress > m_addrList;

    public:
        HNMDARecord();
       ~HNMDARecord();

        void setManagementState( HNMDR_MGMT_STATE_T value );
        //void setOwnershipState( HNMDR_OWNER_STATE_T value );

        void setDiscoveryID( std::string value );
        void setDeviceType( std::string value );
        void setDeviceVersion( std::string value );
        void setHNodeIDFromStr( std::string value );
        void setName( std::string value );

        void addAddressInfo( std::string dnsName, std::string address, uint16_t port );
     
        HNMDR_MGMT_STATE_T  getManagementState();
        std::string getManagementStateStr();

        //HNMDR_OWNER_STATE_T getOwnershipState();
        //std::string getOwnershipStateStr();

        std::string getDiscoveryID();
        std::string getDeviceType();
        std::string getDeviceVersion();
        std::string getHNodeIDStr();
        std::string getName();
        std::string getCRC32ID();

        bool isOwnedByMe( std::string mgmtCRC32ID );
        bool isOwnedByOther();
        bool isUnclaimed();
        bool isAvailableToPair();

        void getAddressList( std::vector< HNMDARAddress > &addrList );

        HNMDL_RESULT_T findPreferredConnection( HMDAR_ADDRTYPE_T preferredType, HNMDARAddress &connInfo );
        
        HNMDL_RESULT_T updateRecord( HNMDARecord &newRecord );

        void debugPrint( uint offset );
};

typedef enum HNManagedDeviceConfigDataFileMaskEnum
{
    HNMDC_FLDMASK_NONE    = 0x00000000,
    HNMDC_FLDMASK_CRC32ID = 0x00000001,    
    HNMDC_FLDMASK_CLAIM   = 0x00000002    
}HNMDC_FLDMASK_T;

class HNMDConfigData
{
    public:
         HNMDConfigData();
        ~HNMDConfigData();

        void clear();

        uint getFieldMask();

        void setCRC32ID( std::string value );

        void setClaim( bool value );

        HNMDL_RESULT_T setFromJSON( std::string crc32ID, std::istream *bodyStream );

    private:
        uint m_fieldMask;

        std::string m_crc32ID;

        bool m_claim;
};

class HNManagedDeviceArbiter
{
    private:

        // The CRC32ID for the managment node itself.
        std::string m_selfCRC32ID;

        // A map of known hnode2 devices
        std::map< std::string, HNMDARecord > mdrMap;

        // The thread helper
        void *thelp;

        // Should the monitor still be running.
        bool runMonitor;

        uint m_monitorWaitTime;

        void setNextMonitorState( HNMDARecord &device, HNMDR_MGMT_STATE_T nextState, uint minValue );

        HNMDL_RESULT_T updateDeviceOperationalInfo( HNMDARecord &device );
        HNMDL_RESULT_T updateDeviceOwnerInfo( HNMDARecord &device );

    protected:
        void runMonitoringLoop();
        void killMonitoringLoop();

    public:
        HNManagedDeviceArbiter();
       ~HNManagedDeviceArbiter();

        HNMDL_RESULT_T notifyDiscoverAdd( HNMDARecord &record );
        HNMDL_RESULT_T notifyDiscoverRemove( HNMDARecord &record );

        void setSelfCRC32ID( std::string crc32ID );
        std::string getSelfCRC32ID();

        void start();
        void shutdown();

        HNMDL_RESULT_T getDeviceCopy( std::string crc32ID, HNMDARecord &device );
        void getDeviceListCopy( std::vector< HNMDARecord > &deviceList );

        HNMDL_RESULT_T lookupConnectionInfo( std::string crc32ID, HMDAR_ADDRTYPE_T preferredType, HNMDARAddress &connInfo );

        void debugPrint();

    friend HNMDARunner;
};

#endif // _HN_MANAGED_DEVICE_ARBITER_H_
