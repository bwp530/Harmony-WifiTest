/*******************************************************************************
  MPLAB Harmony Application Source File
  
  Company:
    Microchip Technology Inc.
  
  File Name:
    app.c

  Summary:
    This file contains the source code for the MPLAB Harmony application.

  Description:
    This file contains the source code for the MPLAB Harmony application.  It 
    implements the logic of the application's state machine and it may call 
    API routines of other MPLAB Harmony modules in the system, such as drivers,
    system services, and middleware.  However, it does not call any of the
    system interfaces (such as the "Initialize" and "Tasks" functions) of any of
    the modules in the system or make any assumptions about when those functions
    are called.  That is the responsibility of the configuration-specific system
    files.
 *******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
Copyright (c) 2013-2014 released Microchip Technology Inc.  All rights reserved.

Microchip licenses to you the right to use, modify, copy and distribute
Software only when embedded on a Microchip microcontroller or digital signal
controller that is integrated into your product or third party product
(pursuant to the sublicense terms in the accompanying license agreement).

You should refer to the license agreement accompanying this Software for
additional information regarding your rights and obligations.

SOFTWARE AND DOCUMENTATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF
MERCHANTABILITY, TITLE, NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE.
IN NO EVENT SHALL MICROCHIP OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER
CONTRACT, NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR
OTHER LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES
INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE OR
CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT OF
SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
(INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.
 *******************************************************************************/
// DOM-IGNORE-END


// *****************************************************************************
// *****************************************************************************
// Section: Included Files 
// *****************************************************************************
// *****************************************************************************

#include "app.h"

// *****************************************************************************
// *****************************************************************************
// Section: Global Data Definitions
// *****************************************************************************
// *****************************************************************************

#if (OSAL_USE_RTOS == 1) /* It means FreeRTOS V8.x.x is used. */
#define APP_OSAL_MUTEX_LOCK() APP_OSAL_MutexLock(&s_appLock, OSAL_WAIT_FOREVER)
#define APP_OSAL_MUTEX_UNLOCK() APP_OSAL_MutexUnlock(&s_appLock)
#else
#define APP_OSAL_MUTEX_LOCK() do {} while (0)
#define APP_OSAL_MUTEX_UNLOCK() do {} while (0)
#endif




#if defined(TCPIP_IF_MRF24W)
#define WIFI_INTERFACE_NAME "MRF24W"
#elif defined(TCPIP_IF_MRF24WN)
#define WIFI_INTERFACE_NAME "MRF24WN"
#endif


#if defined(TCPIP_STACK_USE_ZEROCONF_MDNS_SD)
#define IS_MDNS_RUN() true
#else
#define IS_MDNS_RUN() false
#define TCPIP_MDNS_ServiceRegister(a, b, c, d, e, f, g, h) do {} while (0)
#define TCPIP_MDNS_ServiceDeregister(a) do {} while (0)
#endif /* defined(TCPIP_STACK_USE_ZEROCONF_MDNS_SD) */

#if defined(TCPIP_STACK_USE_NBNS)
#define IS_NBNS_RUN() true
#else
#define IS_NBNS_RUN() false
#endif /* defined(TCPIP_STACK_USE_NBNS) */

#define IS_WF_INTF(x) ((strcmp(x, "MRF24W") == 0) || (strcmp(x, "MRF24WN") == 0))
// *****************************************************************************
/* Application Data

  Summary:
    Holds application data

  Description:
    This structure holds the application's data.

  Remarks:
    This structure should be initialized by the APP_Initialize function.
    
    Application strings and buffers are be defined outside this structure.
*/

APP_DATA appData;

static IWPRIV_GET_PARAM s_app_get_param;
static IWPRIV_SET_PARAM s_app_set_param;
static IWPRIV_EXECUTE_PARAM s_app_execute_param;

bool g_redirect_signal = false;
WF_CONFIG_DATA g_wifi_cfg;
WF_DEVICE_INFO g_wifi_deviceInfo;
WF_REDIRECTION_CONFIG g_redirectionConfig;
IPV4_ADDR gatewayAddr;


// *****************************************************************************
// *****************************************************************************
// Section: Application Callback Functions
// *****************************************************************************
// *****************************************************************************
static void APP_CONSOLE_HeaderDisplay(void);
static void APP_WIFI_RedirectionConfigInit(void);
static void APP_WIFI_IPv6MulticastFilter_Set(TCPIP_NET_HANDLE netH);
static void APP_WIFI_PowerSave_Config(bool enable);
static void APP_WIFI_DHCPS_Sync(TCPIP_NET_HANDLE netH);
static void APP_TCPIP_IFModules_Disable(TCPIP_NET_HANDLE netH);
static void APP_TCPIP_IFModules_Enable(TCPIP_NET_HANDLE netH);
static void APP_TCPIP_IF_Down(TCPIP_NET_HANDLE netH);
static void APP_TCPIP_IF_Up(TCPIP_NET_HANDLE netH);
/* TODO:  Add any necessary callback functions.
*/
uint8_t APP_WIFI_Prescan(void);
void UDP_Sever(void);
// *****************************************************************************
// *****************************************************************************
// Section: Application Local Functions
// *****************************************************************************
// *****************************************************************************


/* TODO:  Add any necessary local functions.
*/


// *****************************************************************************
// *****************************************************************************
// Section: Application Initialization and State Machine Functions
// *****************************************************************************
// *****************************************************************************

/*******************************************************************************
  Function:
    void APP_Initialize ( void )

  Remarks:
    See prototype in app.h.
 */

void APP_Initialize ( void )
{
    /* Place the App state machine in its initial state. */
    appData.state = APP_STATE_INIT;
//    appData.serverState = APP_TCPIP_WAIT_FOR_IP;
    appData.scanState = APP_WIFI_PRESCAN_INIT;
    s_app_set_param.conn.initConnAllowed = false;
    iwpriv_set(INITCONN_OPTION_SET, &s_app_set_param);    
    /* TODO: Initialize your application's state machine and other
     * parameters.
     */
}


/******************************************************************************
  Function:
    void APP_Tasks ( void )

  Remarks:
    See prototype in app.h.
 */

void APP_Tasks ( void )
{
    SYS_STATUS          tcpipStat;
    const char          *netName, *netBiosName;
    IPV4_ADDR           ipAddr;
    int                 i, nNets;
    TCPIP_NET_HANDLE    netH;
    
    static bool isWiFiPowerSaveConfigured = false;
    static bool wasNetUp[2] = {true, true}; // this app supports 2 interfaces so far
    static uint32_t startTick = 0, reConnectTick = 0;
    static int reConnectWifiCount = 0, reConnectTcpCount = 0;
    static IPV4_ADDR defaultIPWiFi = {-1};
    static IPV4_ADDR dwLastIP[2] = { {-1}, {-1} }; // this app supports 2 interfaces so far
    static TCPIP_NET_HANDLE netHandleWiFi;  
    static bool connectFlag = false;
    DRV_WIFI_DEVICE_INFO devInfo;
    
    
    SYS_CMD_READY_TO_READ();
    /* Check the application's current state. */
    switch ( appData.state )
    {
        /* Application's initial state. */
        case APP_STATE_INIT:
        {
            
            s_app_set_param.conn.initConnAllowed = false;
            iwpriv_set(INITCONN_OPTION_SET, &s_app_set_param);  
            startTick = SYS_TMR_TickCountGet();
            reConnectTick = SYS_TMR_TickCountGet();
            tcpipStat = TCPIP_STACK_Status(sysObj.tcpip);
            if(tcpipStat < 0)
            {   // some error occurred
                SYS_CONSOLE_MESSAGE(" APP: TCP/IP stack initialization failed!\r\n");
                break;
            }else if(tcpipStat == SYS_STATUS_READY){
                    netH = TCPIP_STACK_IndexToNet(i);
                    netName = TCPIP_STACK_NetNameGet(netH);
                    netBiosName = TCPIP_STACK_NetBIOSName(netH);

#if defined(TCPIP_STACK_USE_NBNS)
                    SYS_CONSOLE_PRINT("    Interface %s on host %s - NBNS enabled\r\n", netName, netBiosName);
#else
                    SYS_CONSOLE_PRINT("    Interface %s on host %s - NBNS disabled\r\n", netName, netBiosName);
#endif  // defined(TCPIP_STACK_USE_NBNS)

//                reConnectTick = SYS_TMR_TickCountGet();
                appData.state = APP_WIFI_GETINFO;            
            }       

            break;
        }         
             
    case APP_WIFI_GETINFO:{         
            /*
             * Following "if condition" is useless when demo firstly
             * boots up, since stack's status has already been checked in
             * APP_TCPIP_WAIT_INIT. But it is necessary in redirection or
             * Wi-Fi interface reset due to connection errors.
             */
            iwpriv_get(INITSTATUS_GET, &s_app_get_param);
            if (s_app_get_param.driverStatus.initStatus == IWPRIV_READY) {
                s_app_get_param.devInfo.data = &g_wifi_deviceInfo;
                iwpriv_get(DEVICEINFO_GET, &s_app_get_param);
                defaultIPWiFi.Val = TCPIP_STACK_NetAddress(netHandleWiFi);
                netHandleWiFi = TCPIP_STACK_NetHandleGet(WIFI_INTERFACE_NAME);
               
                s_app_set_param.scan.prescanAllowed = true;
                iwpriv_set(PRESCAN_OPTION_SET, &s_app_set_param);
                appData.state = APP_TCPIP_MODULES_ENABLE;
    
            } else {
                break;
            }
            
            
         } 
            
        
        case APP_WIFI_PRESCAN: {
            // if pre-scan option is set to false,
            // this state would just run once and pass,
            // APP_WIFI_Prescan() function would not actually
            // do anything
            SYS_CONSOLE_MESSAGE(" ---------APP_WIFI_PRESCAN------------\r\n");
            //uint8_t scanStatus ;//APP_WIFI_Prescan();
            uint8_t scanStatus = APP_WIFI_Prescan();
            iwpriv_get(INITSTATUS_GET, &s_app_get_param); //waiting init end
            if (s_app_get_param.driverStatus.initStatus == IWPRIV_READY)
            {
                scanStatus = IWPRIV_READY;
            }else{
                break;
            }            
            if (scanStatus == IWPRIV_READY) {
                appData.state = APP_TCPIP_MODULES_ENABLE;
            } else if (scanStatus == IWPRIV_ERROR) {
                SYS_CONSOLE_MESSAGE("Wi-Fi Prescan Error\r\n");
                appData.state = APP_TCPIP_MODULES_ENABLE;
            } else {
                break;
            }
        }        
        
        case APP_TCPIP_MODULES_ENABLE:
            // check the available interfaces
            DRV_WIFI_Connect();
            SYS_CONSOLE_MESSAGE(" ---------APP_TCPIP_MODULES_ENABLE------------\r\n");
            nNets = TCPIP_STACK_NumberOfNetworksGet();
            SYS_PRINT("  Interfaces number:%d\r\n", nNets);
            for (i = 0; i < nNets; ++i)
                APP_TCPIP_IFModules_Enable(TCPIP_STACK_IndexToNet(i));
            appData.state = APP_TCPIP_WAIT_FOR_IP;
            SYS_CONSOLE_MESSAGE(" APP_TCPIP_TRANSACT_Begin\n\r ");
            break;   
        case APP_TCPIP_WAIT_FOR_IP:
        {
            // if the IP address of an interface has changed
            // display the new value on the system console
         //   SYS_CONSOLE_MESSAGE(" ---------APP_TCPIP_WAIT_FOR_IP------------\r\n");
           // TCPIP_DHCP_Renew(netH);
            nNets = TCPIP_STACK_NumberOfNetworksGet();

            for (i = 0; i < nNets; i++)
            {
                netH = TCPIP_STACK_IndexToNet(i);
                ipAddr.Val = TCPIP_STACK_NetAddress(netH);
                if(dwLastIP[i].Val != ipAddr.Val)
                {
                    dwLastIP[i].Val = ipAddr.Val;

                    SYS_CONSOLE_MESSAGE(TCPIP_STACK_NetNameGet(netH));
                    SYS_CONSOLE_MESSAGE(" IP Address: ");
                    SYS_CONSOLE_PRINT("%d.%d.%d.%d \r\n", ipAddr.v[0], ipAddr.v[1], ipAddr.v[2], ipAddr.v[3]);
                    if (ipAddr.v[0] != 0 && ipAddr.v[0] != 169) // Wait for a Valid IP
                    {
                        SYS_CONSOLE_MESSAGE(" ---------APP_TCPIP_WAIT_FOR_IP------------\r\n");
                        appData.state = APP_TCPIP_OPENING_SERVER;
                    }
                }
            }        
            break;
        }
        case APP_TCPIP_OPENING_SERVER:
        {
            SYS_CONSOLE_MESSAGE(" ---------APP_TCPIP_OPENING_SERVER------------\r\n");
            gatewayAddr.Val = TCPIP_STACK_NetAddressGateway(netHandleWiFi);
            if(gatewayAddr.Val != 0) {
                IPV4_ADDR addr;
                addr.v[0] = SERVER_V0;
                addr.v[1] = SERVER_V1;
                addr.v[2] = SERVER_V2;
                addr.v[3] = SERVER_V3;
                //SYS_DEBUG_PRINT(debugflag,"Server Address: %d.%d.%d.%d \r\n", addr.v[0], addr.v[1], addr.v[2], addr.v[3]);
                //SYS_DEBUG_MESSAGE(debugflag,"TCP client connecting...\r\n");
                TCPIP_TCP_Close(appData.socket);
                appData.socket = TCPIP_TCP_ClientOpen(IP_ADDRESS_TYPE_IPV4, SERVER_PORT, (IP_MULTI_ADDRESS*) &addr);  
                if(appData.socket != INVALID_SOCKET) {
                    //SYS_DEBUG_PRINT(debugflag,"Tcp Socket: %d\r\n", tcpSocket);
                    TCPIP_TCP_Connect(appData.socket);
                    appData.state = APP_TCPIP_WAIT_FOR_CONNECTION;
                } else {
                    SYS_DEBUG_MESSAGE(0, "TCP connection failed...\r\n");
                    appData.state = APP_TCPIP_WAIT_FOR_CONNECTION;
                }
            }
            break;
        }
        

        case APP_TCPIP_WAIT_FOR_CONNECTION:
        {
            if (!TCPIP_TCP_IsConnected(appData.socket))
            {
                return;
            }
            else
            {
                // We got a connection
                appData.state = APP_TCPIP_SERVING_CONNECTION;
                SYS_CONSOLE_MESSAGE("Received a connection\r\n");
            }
            
            break;
        }
        

        case APP_TCPIP_SERVING_CONNECTION:
        {
            if (!TCPIP_TCP_IsConnected(appData.socket))
            {
                appData.state = APP_TCPIP_CLOSING_CONNECTION;
                SYS_CONSOLE_MESSAGE("Connection was closed\r\n");
                break;
            }
            int16_t wMaxGet, wMaxPut, wCurrentChunk;
            uint16_t w, w2;
            uint8_t AppBuffer[32];
            memset(AppBuffer, 0, 32);
            // Figure out how many bytes have been received and how many we can transmit.
            wMaxGet = TCPIP_TCP_GetIsReady(appData.socket);	// Get UDP RX FIFO byte count
            wMaxPut = TCPIP_TCP_PutIsReady(appData.socket);

            //SYS_CONSOLE_PRINT("\t%d bytes are available.\r\n", wMaxGet);
            if (wMaxGet == 0)
            {
                break;
            }

            if (wMaxPut < wMaxGet)
            {
                wMaxGet = wMaxPut;
            }

            SYS_CONSOLE_PRINT("RX Buffer has %d bytes in it\n", wMaxGet);

            // Process all bytes that we can
            // This is implemented as a loop, processing up to sizeof(AppBuffer) bytes at a time.
            // This limits memory usage while maximizing performance.  Single byte Gets and Puts are a lot slower than multibyte GetArrays and PutArrays.
            wCurrentChunk = sizeof(AppBuffer);
            for(w = 0; w < wMaxGet; w += sizeof(AppBuffer))
            {
                // Make sure the last chunk, which will likely be smaller than sizeof(AppBuffer), is treated correctly.
                if(w + sizeof(AppBuffer) > wMaxGet)
                    wCurrentChunk = wMaxGet - w;

                // Transfer the data out of the TCP RX FIFO and into our local processing buffer.
                int rxed = TCPIP_TCP_ArrayGet(appData.socket, AppBuffer, sizeof(AppBuffer));

                SYS_CONSOLE_PRINT("\tReceived a message of '%s' and length %d\r\n", AppBuffer, rxed);

                // Perform the "ToUpper" operation on each data byte
                for(w2 = 0; w2 < wCurrentChunk; w2++)
                {
                    i = AppBuffer[w2];
                    if(i >= 'a' && i <= 'z')
                    {
                            i -= ('a' - 'A');
                            AppBuffer[w2] = i;
                    }
                    else if(i == '\e')   //escape
                    {
                        SYS_CONSOLE_MESSAGE("Connection was closed\r\n");
                    }
                }

                SYS_CONSOLE_PRINT("\tSending a messages '%s'\r\n", AppBuffer);

                // Transfer the data out of our local processing buffer and into the TCP TX FIFO.
                TCPIP_TCP_ArrayPut(appData.socket, AppBuffer, wCurrentChunk);

                //TCPIP_UDP_Flush(appData.socket);

                appData.state = APP_TCPIP_CLOSING_CONNECTION;
            }
            //TCPIP_UDP_Discard(appData.socket);
            break;
        }
        
        case APP_TCPIP_CLOSING_CONNECTION:
        {
            			// Close the socket connection.
            TCPIP_TCP_Close(appData.socket);

            appData.state = APP_TCPIP_OPENING_SERVER;
            break;
        }            
                          
        default:
        {
            /* TODO: Handle error in application's state machine. */
            break;
        }
    }
    
    
          iwpriv_get(CONNSTATUS_GET, &s_app_get_param);
            if (s_app_get_param.conn.status == IWPRIV_CONNECTION_FAILED || g_redirect_signal) {
                APP_TCPIP_IFModules_Disable(netHandleWiFi);
                APP_TCPIP_IF_Down(netHandleWiFi);
                APP_TCPIP_IF_Up(netHandleWiFi);
                isWiFiPowerSaveConfigured = false;
                appData.state = APP_WIFI_CONFIG;              
            } else if (s_app_get_param.conn.status == IWPRIV_CONNECTION_REESTABLISHED) {
                // restart dhcp client and config power save
                iwpriv_get(OPERATIONMODE_GET, &s_app_get_param);
                if (!s_app_get_param.opMode.isServer) {
                    TCPIP_DHCP_Disable(netHandleWiFi);
                    TCPIP_DHCP_Enable(netHandleWiFi);
                    isWiFiPowerSaveConfigured = false;
                }
            }
           // SYS_CONSOLE_MESSAGE(" APP_TCPIP_TRANSACT_Begin\n\r ");
            /*
             * If we get a new IP address that is different than the default one,
             * we will run PowerSave configuration.
             */
            if (!isWiFiPowerSaveConfigured &&
                TCPIP_STACK_NetIsUp(netHandleWiFi) &&
                (TCPIP_STACK_NetAddress(netHandleWiFi) != defaultIPWiFi.Val)) {
                APP_WIFI_PowerSave_Config(true);
                isWiFiPowerSaveConfigured = true;
            }
    
    
    
    
    
    
    
    
    
    
    if (SYS_TMR_TickCountGet() - reConnectTick >= SYS_TMR_TickCounterFrequencyGet()) {
        uint8_t status;
         reConnectTick = SYS_TMR_TickCountGet();
      //   PLIB_PORTS_PinToggle(PORTS_ID_0, PORT_CHANNEL_E, PORTS_BIT_POS_5 );
         DRV_WIFI_ConnectionStateGet(&status);
         switch(status) {
             case DRV_WIFI_CSTATE_NOT_CONNECTED:
                reConnectWifiCount++;
                if(reConnectWifiCount >= 60 ) {
                    reConnectWifiCount = 0;   
                    SYS_DEBUG_MESSAGE(0, "reInit WIFI module\r\n");                           
                    appData.state = APP_WIFI_GETINFO;
                } 
                SYS_CONSOLE_MESSAGE("DRV_WIFI_CSTATE_NOT_CONNECTED\r\n");
                break;
             case DRV_WIFI_CSTATE_CONNECTION_IN_PROGRESS:
                SYS_CONSOLE_MESSAGE("DRV_WIFI_CSTATE_CONNECTION_IN_PROGRESS\r\n");
                break;
             case DRV_WIFI_CSTATE_CONNECTED_INFRASTRUCTURE: 
                 reConnectWifiCount = 0;
                 if (!TCPIP_TCP_IsConnected(appData.socket)) {
                    reConnectTcpCount ++;
                    if(reConnectTcpCount >= 1 ) {
                        reConnectTcpCount = 0;   
                        DRV_WIFI_DeviceInfoGet(&devInfo);
                        //SYS_DEBUG_PRINT(debugflag, "MRF24WG romVersion:0x%02X, patchVersion:0x%02X\r\n",  devInfo.romVersion, devInfo.patchVersion);                           
                        connectFlag = true;
                        TCPIP_TCP_Disconnect(appData.socket);
                        appData.state = APP_TCPIP_OPENING_SERVER;
                    }
                 } else {
                     if(connectFlag == true) {
                        connectFlag = false;
                     }
                     reConnectTcpCount = 0;
                 }
                 break;
             case DRV_WIFI_CSTATE_CONNECTED_ADHOC:
                 SYS_CONSOLE_MESSAGE("DRV_WIFI_CSTATE_CONNECTED_ADHOC\r\n");
                 break;
             case DRV_WIFI_CSTATE_RECONNECTION_IN_PROGRESS:
                 SYS_CONSOLE_MESSAGE("DRV_WIFI_CSTATE_RECONNECTION_IN_PROGRESS\r\n");
                 break;
             case DRV_WIFI_CSTATE_CONNECTION_PERMANENTLY_LOST:
                 SYS_CONSOLE_MESSAGE("DRV_WIFI_CSTATE_CONNECTION_PERMANENTLY_LOST\r\n");
                 break;
         }
     }        
   
}

uint8_t APP_WIFI_Prescan(void)
{
    static APP_WIFI_PRESCAN_STATE scanState = APP_WIFI_PRESCAN_INIT;
    switch (scanState) {
        case APP_WIFI_PRESCAN_INIT:
            iwpriv_get(PRESCAN_OPTION_GET, &s_app_get_param); // param->scan.prescanAllowed,if allows to scan
            if (s_app_get_param.scan.prescanAllowed) {
                iwpriv_get(NETWORKTYPE_GET, &s_app_get_param); //DRV_WIFI_NetworkTypeGet,read from module
                uint8_t type = s_app_get_param.netType.type;
                iwpriv_get(CONNSTATUS_GET, &s_app_get_param); //param->conn.status,get connection state
                if (type == WF_NETWORK_TYPE_SOFT_AP && s_app_get_param.conn.status == IWPRIV_CONNECTION_SUCCESSFUL)
                    return IWPRIV_ERROR;
                iwpriv_execute(PRESCAN_START, &s_app_execute_param); //_prescan_start(),write command to module
                scanState = APP_WIFI_PRESCAN_WAIT;
                break;
            } else {
                return IWPRIV_READY;
            }
            
        case APP_WIFI_PRESCAN_WAIT:
            iwpriv_get(PRESCAN_ISFINISHED_GET, &s_app_get_param);//s_prescan_inprogress && g_drv_wifi_priv.isScanDone --> param->scan.prescanFinished, read from module
            if (s_app_get_param.scan.prescanFinished)
            {
                iwpriv_get(SCANSTATUS_GET, &s_app_get_param);
                if (s_app_get_param.scan.scanStatus == IWPRIV_SCAN_SUCCESSFUL) { //param->scan.scanStatus, if scan successful
                    scanState = APP_WIFI_PRESCAN_SAVE;
                } else {
                    scanState = APP_WIFI_PRESCAN_INIT;
                    return IWPRIV_ERROR;
                }
            } else {
                break;
            }

        case APP_WIFI_PRESCAN_SAVE:
            iwpriv_execute(SCANRESULTS_SAVE, &s_app_execute_param);  //save scan result
            if (s_app_execute_param.scan.saveStatus == IWPRIV_IN_PROGRESS)
                break;
            else // IWPRIV_READY
                scanState = APP_WIFI_PRESCAN_RESET;

        case APP_WIFI_PRESCAN_RESET: {
            TCPIP_NET_HANDLE netH = TCPIP_STACK_NetHandleGet(WIFI_INTERFACE_NAME);
            APP_TCPIP_IF_Down(netH); //reset network interface
            APP_TCPIP_IF_Up(netH);
            s_app_set_param.conn.initConnAllowed = true;
            iwpriv_set(INITCONN_OPTION_SET, &s_app_set_param); //init connection
            s_app_set_param.scan.prescanAllowed = false;
            iwpriv_set(PRESCAN_OPTION_SET, &s_app_set_param); //not allow to scan
            scanState = APP_WIFI_PRESCAN_WAIT_RESET;
            break;
        }

        case APP_WIFI_PRESCAN_WAIT_RESET:
            iwpriv_get(INITSTATUS_GET, &s_app_get_param); //waiting init end
            if (s_app_get_param.driverStatus.initStatus == IWPRIV_READY)
                scanState = APP_WIFI_PRESCAN_DONE;
            else
                break;

        case APP_WIFI_PRESCAN_DONE:
            scanState = APP_WIFI_PRESCAN_INIT;
           // iwpriv_execute(SCANRESULTS_DISPLAY, &s_app_get_param);
            return IWPRIV_READY;
    }

    return IWPRIV_IN_PROGRESS;
}



static void APP_CONSOLE_HeaderDisplay(void)
{
    #if defined(WIFI_EASY_CONFIG_DEMO)
        SYS_CONSOLE_MESSAGE("\r\n==================================");
        SYS_CONSOLE_MESSAGE("\r\n*** Wi-Fi TCP/IP EZConfig Demo ***");
        SYS_CONSOLE_MESSAGE("\r\n==================================\r\n");
    #else
        SYS_CONSOLE_MESSAGE("\r\n===================================");
        SYS_CONSOLE_MESSAGE("\r\n*** Microchip Wi-Fi TCP/IP Demo ***");
        SYS_CONSOLE_MESSAGE("\r\n===================================\r\n");
    #endif
}

/*******************************************************************************
  Function:
    static void APP_WIFI_RedirectionConfigInit(void)

  Remarks:
    Initialize redirection configuration variable
 */
static void APP_WIFI_RedirectionConfigInit(void)
{
    g_redirectionConfig.ssid[0] = 0;
    g_redirectionConfig.securityMode = WF_SECURITY_OPEN;
    g_redirectionConfig.securityKey[0] = 0;
    g_redirectionConfig.wepKeyIndex = WF_WEP_KEY_INVALID;
    g_redirectionConfig.networkType = WF_NETWORK_TYPE_INFRASTRUCTURE;
}

static void APP_WIFI_IPv6MulticastFilter_Set(TCPIP_NET_HANDLE netH)
{
#if defined(TCPIP_STACK_USE_IPV6)
    const uint8_t *pMacAddr = TCPIP_STACK_NetAddressMac(netH);
    int i;
    uint8_t linkLocalSolicitedMulticastMacAddr[6];
    uint8_t solicitedNodeMulticastMACAddr[] = {0x33, 0x33, 0xff, 0x00, 0x00, 0x00};
    uint8_t allNodesMulticastMACAddr[] = {0x33, 0x33, 0x00, 0x00, 0x00, 0x01};

    linkLocalSolicitedMulticastMacAddr[0] = 0x33;
    linkLocalSolicitedMulticastMacAddr[1] = 0x33;
    linkLocalSolicitedMulticastMacAddr[2] = 0xff;

    for (i = 3; i < 6; i++)
        linkLocalSolicitedMulticastMacAddr[i] = pMacAddr[i];

    s_app_set_param.multicast.addr = linkLocalSolicitedMulticastMacAddr;
    iwpriv_set(MULTICASTFILTER_SET, &s_app_set_param);
    s_app_set_param.multicast.addr = solicitedNodeMulticastMACAddr;
    iwpriv_set(MULTICASTFILTER_SET, &s_app_set_param);
    s_app_set_param.multicast.addr = allNodesMulticastMACAddr;
    iwpriv_set(MULTICASTFILTER_SET, &s_app_set_param);
#endif
}

static void APP_WIFI_PowerSave_Config(bool enable)
{
#if WF_DEFAULT_POWER_SAVE == WF_ENABLED
    s_app_set_param.powerSave.enabled = enable;
    iwpriv_set(POWERSAVE_SET, &s_app_set_param);
#endif
}

static void APP_WIFI_DHCPS_Sync(TCPIP_NET_HANDLE netH)
{
#if defined(TCPIP_STACK_USE_DHCP_SERVER)
    bool updated;
    TCPIP_MAC_ADDR addr;

    s_app_get_param.clientInfo.addr = addr.v;
    iwpriv_get(CLIENTINFO_GET, &s_app_get_param);
    updated = s_app_get_param.clientInfo.updated;

    if (updated)
        TCPIP_DHCPS_LeaseEntryRemove(netH, (TCPIP_MAC_ADDR *)&addr);
#endif
}

static void APP_TCPIP_IFModules_Disable(TCPIP_NET_HANDLE netH)
{
    const char *netName = TCPIP_STACK_NetNameGet(netH);

    if (IS_WF_INTF(netName) && TCPIP_STACK_NetIsUp(netH))
        APP_WIFI_PowerSave_Config(false);
//    TCPIP_DHCPS_Disable(netH);
    TCPIP_DHCP_Disable(netH);
//    TCPIP_DNSS_Disable(netH);
    TCPIP_DNS_Disable(netH, true);
    TCPIP_MDNS_ServiceDeregister(netH);
}

static void APP_TCPIP_IFModules_Enable(TCPIP_NET_HANDLE netH)
{
    int netIndex = TCPIP_STACK_NetIndexGet(netH);
    const char *netName = TCPIP_STACK_NetNameGet(netH);

    /*
     * If it's not Wi-Fi interface, then leave it to the TCP/IP stack
     * to configure its DHCP server/client status.
     */
    if (IS_WF_INTF(netName)) {
        iwpriv_get(OPERATIONMODE_GET, &s_app_get_param);
        if (s_app_get_param.opMode.isServer) {
            TCPIP_DHCP_Disable(netH); // must stop DHCP client first
//            TCPIP_DHCPS_Enable(netH); // start DHCP server
            TCPIP_DNS_Disable(netH, true);
//            TCPIP_DNSS_Enable(netH);
        } else {
//            TCPIP_DHCPS_Disable(netH); // must stop DHCP server first
            TCPIP_DHCP_Enable(netH); // start DHCP client
 //           TCPIP_DNSS_Disable(netH);
            TCPIP_DHCP_Renew(netH);
            TCPIP_DNS_Enable(netH, TCPIP_DNS_ENABLE_DEFAULT);
        }
        APP_WIFI_IPv6MulticastFilter_Set(netH);
    }
    if (IS_NBNS_RUN()) {
        const char *netBiosName = TCPIP_STACK_NetBIOSName(netH);
        SYS_CONSOLE_PRINT("  Interface %s on host %s - NBNS enabled\r\n", netName, netBiosName);
    }
    if (IS_MDNS_RUN()) {
        char mDNSServiceName[] = "MyWebServiceNameX "; // base name of the service Must not exceed 16 bytes long
        // the last digit will be incremented by interface
        mDNSServiceName[sizeof(mDNSServiceName) - 2] = '1' + netIndex;
        TCPIP_MDNS_ServiceRegister(netH, mDNSServiceName, "_http._tcp.local", 80, ((const uint8_t *)"path=/index.htm"),
            1, NULL, NULL);
    }
}

static void APP_TCPIP_IF_Down(TCPIP_NET_HANDLE netH)
{
    TCPIP_STACK_NetDown(netH);
}

static void APP_TCPIP_IF_Up(TCPIP_NET_HANDLE netH)
{
    SYS_MODULE_OBJ tcpipStackObj;
    TCPIP_STACK_INIT tcpip_init_data;
    const TCPIP_NETWORK_CONFIG *pIfConf;
    uint16_t net_ix = TCPIP_STACK_NetIndexGet(netH);

    tcpipStackObj = TCPIP_STACK_Initialize(0, 0);
    TCPIP_STACK_InitializeDataGet(tcpipStackObj, &tcpip_init_data);
    pIfConf = tcpip_init_data.pNetConf + net_ix;
    TCPIP_STACK_NetUp(netH, pIfConf);
}

/*******************************************************************************
 End of File
 */
