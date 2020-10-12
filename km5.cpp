#define _CRT_SECURE_NO_WARNINGS 1

#define _WIN32_DCOM				// Enables DCOM extensions
#define INITGUID				// Initialize OLE constants
#define ECL_SID "opc.km5"		// identificator of OPC server

#include <stdio.h>
#include <math.h>				// some mathematical function
#include "server.h"				// server variable
#include "unilog.h"				// universal utilites for creating log-files
#include <locale.h>				// set russian codepage
#include "opcda.h"				// basic function for OPC:DA
#include "lightopc.h"			// light OPC library header file
#include "serialport.h"			// serial port header

#include <sql.h>
#include <sqlext.h>
#include <stdlib.h>
#include <mbstring.h>
#include "dbaccess.h"
//---------------------------------------------------------------------------------
static const loVendorInfo vendor = {0,1,11,0,"KM-5 OPC Server" };	// OPC vendor info (Major/Minor/Build/Reserv)
static void a_server_finished(VOID*, loService*, loClient*);		// OnServer finish his work
static int OPCstatus=OPC_STATUS_RUNNING;							// status of OPC server
loService *my_service;			// name of light OPC Service
dbaccess dbase;					// Database pointer
//---------------------------------------------------------------------------------
DeviceKM5  	km[MAX_KM_NUM];
SerialPort	port[PORT_NUM_MAX];				// com-port
UINT		com_num[PORT_NUM_MAX]={2};		// COM-port numbers
UINT		speed[PORT_NUM_MAX]={1200};		// COM-port speed
UINT		parity=2;						// Parity
UINT		databits=8;						// Data bits 
UINT		preconfig=0;
UINT		res=0;							// SQL request result
WCHAR		bufMB[2501];
UINT		chan_num[MAX_KM_NUM]={0};	// 
UINT		sqlerror=0;
//---------------------------------------------------------------------------------
UINT	Com_number=0;				// порты и ипшники, которые найдены в конфиге
UINT	KMNum=0;					// device numbers
UINT	devNum=0;					// main device nums
UINT	tag_num=0;					// tags counter
//-----------------------------------------------------------------------------
BOOL    send_km (UINT op);			// send to KM-5
UINT    read_km (BYTE* dat);		// read from KM-5
BYTE    CRC(const BYTE* const Data, const BYTE DataSize, BYTE type);
      
static  BOOL    StoreData (UINT dv, UINT prm, UINT pipe, UINT status, FLOAT value);
static  BOOL    StoreData (UINT dv, UINT prm, UINT pipe, UINT type, UINT status, FLOAT value, CHAR* data);
static  BOOL 	StoreDataC (UINT dv, UINT prm, UINT prm2, UINT type, UINT status, FLOAT value, CHAR* date);
VOID	PollDeviceCOM (LPVOID lpParam);		// polling group device thread
//---------------------------------------------------------------------------------
unilog *logg=NULL;				// new structure of unilog
FILE	*CfgFile;					// pointer to .ini file
//---------------------------------------------------------------------------------
VOID ReadMConf (INT dev, SHORT blok, CHAR* name, UINT tagn);
UINT PollDevice(UINT device);	// polling single device
UINT ScanBus();					// bus scanned programm
INT	 init_tags(VOID);			// Init tags
UINT InitDriver();				// func of initialising port and creating service
UINT DestroyDriver();			// function of detroying driver and service
VOID WriteToPort (UINT com, UINT device, CHAR* Out);
//---------------------------------------------------------------------------------
static CRITICAL_SECTION lk_values;	// protects ti[] from simultaneous access 
static INT mymain(HINSTANCE hInstance, INT argc, CHAR *argv[]);
static INT show_error(LPCTSTR msg);		// just show messagebox with error
static INT show_msg(LPCTSTR msg);		// just show messagebox with message
static VOID poll_device(VOID);			// function polling device
CHAR* ReadParam (CHAR *SectionName,CHAR *Value);	// read parametr from .ini file
CRITICAL_SECTION drv_access;
BOOL WorkEnable=TRUE;
//---------------------------------------------------------------------------------
//CHAR WincName[TAGS_NUM_MAX][10];
CHAR argv0[FILENAME_MAX + 32];	// lenght of command line (file+path (260+32))
static CHAR *tn[TAGS_NUM_MAX];		// Tag name
static loTagValue tv[TAGS_NUM_MAX];	// Tag value
static loTagId ti[TAGS_NUM_MAX];	// Tag id
UINT	tTotal=0;				// total quantity of tags
CHAR	tmp[200];				// temporary array for strings
CHAR	dataset[2000];			// temporary array for data
CHAR	query[500];				// temporary array for DB query
//---------------------------------------------------------------------------------
// {1D72DB4E-13D7-43cb-9E5B-D0ED562D0CD0}
DEFINE_GUID(GID_km5OPCserverDll, 
0x1d72db4e, 0x13d7, 0x43cb, 0x9e, 0x5b, 0xd0, 0xed, 0x56, 0x2d, 0xc, 0xd0);
// {893A2756-1777-4ed3-B598-8C55EEF2DCD9}
DEFINE_GUID(GID_km5OPCserverExe, 
0x893a2756, 0x1777, 0x4ed3, 0xb5, 0x98, 0x8c, 0x55, 0xee, 0xf2, 0xdc, 0xd9);
//---------------------------------------------------------------------------------
inline void cleanup_common(void)	// delete log-file
{ UL_INFO((LOGID, "Finish KM-5 OPC Server"));  
  unilog_Delete(logg); logg = NULL;
  UL_INFO((LOGID, "Total Finish")); }
inline void init_common(void)		// create log-file
{ logg = unilog_Create(ECL_SID, absPath(LOG_FNAME), NULL, 0, ll_DEBUG); // level [ll_FATAL...ll_DEBUG] 
   UL_INFO((LOGID, "Start KM-5 OPC Server")); printf ("Start KM-5 OPC Server\n");}

INT show_error(LPCTSTR msg)			// just show messagebox with error
{ ::MessageBox(NULL, msg, ECL_SID, MB_ICONSTOP|MB_OK);
  return 1;}
INT show_msg(LPCTSTR msg)			// just show messagebox with message
{ ::MessageBox(NULL, msg, ECL_SID, MB_OK);
  return 1;}
//---------------------------------------------------------------------------------
inline void cleanup_all(DWORD objid)
{ // Informs OLE that a class object, previously registered is no longer available for use  
  if (FAILED(CoRevokeClassObject(objid)))  UL_WARNING((LOGID, "CoRevokeClassObject() failed..."));
  DestroyDriver();					// close port and destroy driver
  CoUninitialize();					// Closes the COM library on the current thread
  cleanup_common();					// delete log-file
}
//---------------------------------------------------------------------------------
#include "opc_main.h"	//	main server 
//---------------------------------------------------------------------------------
INT APIENTRY WinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance,LPSTR lpCmdLine,INT nCmdShow)
{ static char *argv[3] = { "dummy.exe", NULL, NULL };	// defaults arguments
  argv[1] = lpCmdLine;									// comandline - progs keys
  return mymain(hInstance, 2, argv);}
INT main(INT argc, CHAR *argv[])
{  return mymain(GetModuleHandle(NULL), argc, argv); }
//---------------------------------------------------------------------------------
INT mymain(HINSTANCE hInstance, INT argc, CHAR *argv[]) 
{
 const char eClsidName [] = ECL_SID;			// desription 
 const char eProgID [] = ECL_SID;				// name
 CHAR *cp; DWORD objid;
 objid=::GetModuleFileName(NULL, argv0, sizeof(argv0));	// function retrieves the fully qualified path for the specified module
 if(objid==0 || objid+50 > sizeof(argv0)) return 0;		// not in border
 init_common();									// create log-file
 if(NULL==(cp = setlocale(LC_ALL, ".1251")))	// sets all categories, returning only the string cp-1251
	{ 
	 UL_ERROR((LOGID, "setlocale() - Can't set 1251 code page"));	// in bad case write error in log
	 cleanup_common();							// delete log-file
     return 0;
	}
 INT finish=1;		// flag of comlection
 cp = argv[1];		
 if(cp)				// check keys of command line 
	{     
     if (strstr(cp, "/r"))	//	attempt registred server
		{
	     if (loServerRegister(&GID_km5OPCserverExe, eProgID, eClsidName, argv0, 0)) 
			{ show_error("Registration Failed"); UL_ERROR((LOGID, "Registration <%s> <%s> Failed", eProgID, argv0));  } 
		 else
			{ show_msg("KM-5 OPC Registration Ok"); UL_INFO((LOGID, "Registration <%s> <%s> Ok", eProgID, argv0));  }
		} 
	else 
		if (strstr(cp, "/u")) 
			{
			 if (loServerUnregister(&GID_km5OPCserverExe, eClsidName)) 
				{ show_error("UnRegistration Failed"); UL_ERROR((LOGID, "UnReg <%s> <%s> Failed", eClsidName, argv0));  } 
			 else 
				{ show_msg("KM-5 OPC Server Unregistered"); UL_INFO((LOGID, "UnReg <%s> <%s> Ok", eClsidName, argv0));}
			} 
		else  // only /r and /u options
			if (strstr(cp, "/?")) 
				 show_msg("Use: \nKey /r to register server.\nKey /u to unregister server.\nKey /? to show this help.");
				 else
					{
					 UL_WARNING((LOGID, "Ignore unknown option <%s>", cp));
					 finish = 0;		// nehren delat
					}
		if (finish) {      cleanup_common();      return 0;    } 
	}
if ((CfgFile = fopen(CFG_FILE, "r+")) == NULL)
	{	
	 show_error("Error open .ini file");
	 UL_ERROR((LOGID, "Error open .ini file"));	// in bad case write error in log
	 return 0;
	}
if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) 
	{	// Initializes the COM library for use by the calling thread
     UL_ERROR((LOGID, "CoInitializeEx() failed. Exiting..."));
     cleanup_common();	// close log-file
     return 0;
	}
UL_INFO((LOGID, "CoInitializeEx() Ok...."));	// write to log
if (InitDriver()) {		// open and set com-port
    CoUninitialize();	// Closes the COM library on the current thread
    cleanup_common();	// close log-file
    return 0;
  }
UL_INFO((LOGID, "InitDriver() Ok...."));	// write to log
UL_INFO((LOGID, "CoRegisterClassObject(%ul, %d, %d)",GID_km5OPCserverExe, my_CF, objid));
if (FAILED(CoRegisterClassObject(GID_km5OPCserverExe, &my_CF, 
				   CLSCTX_LOCAL_SERVER|CLSCTX_REMOTE_SERVER|CLSCTX_INPROC_SERVER, 
				   REGCLS_MULTIPLEUSE, &objid)))
    { UL_ERROR((LOGID, "CoRegisterClassObject() failed. Exiting..."));
      cleanup_all(objid);		// close comport and unload all librares
      return 0; }
UL_INFO((LOGID, "CoRegisterClassObject() Ok...."));	// write to log

Sleep(1000);
my_CF.Release();		// avoid locking by CoRegisterClassObject() 
my_CF.Release();		// avoid locking by CoRegisterClassObject() 
//UL_INFO((LOGID, "my_CF.in_use(%d)",my_CF.in_use()));	// write to log
if (OPCstatus!=OPC_STATUS_RUNNING)	// ???? maybe Status changed and OPC not currently running??
	{	while(my_CF.in_use()) Sleep(1000);	// wait
		cleanup_all(objid);
		return 0;	}
while(my_CF.in_use())					// while server created or client connected
	{
	 if (WorkEnable) poll_device();		// polling devices else do nothing (and be nothing)	 
	}
UL_INFO((LOGID, "end cleanup_all()"));	// write to log
cleanup_all(objid);						// destroy himself
return 0;
}
//-------------------------------------------------------------------
loTrid ReadTags(const loCaller *, unsigned  count, loTagPair taglist[],
		VARIANT   values[],	WORD      qualities[],	FILETIME  stamps[],
		HRESULT   errs[],	HRESULT  *master_err,	HRESULT  *master_qual,
		const VARTYPE vtype[],	LCID lcid)
{  return loDR_STORED; }
//-------------------------------------------------------------------
INT WriteTags(const loCaller *ca,
              unsigned count, loTagPair taglist[],
              VARIANT values[], HRESULT error[], HRESULT *master, LCID lcid)
{  return loDW_TOCACHE; }
//-------------------------------------------------------------------
VOID activation_monitor(const loCaller *ca, INT count, loTagPair *til){}
//-------------------------------------------------------------------
UINT InitDriver()
{
 CHAR host[100],login[100],pass[100];
 strcpy (host,ReadParam ("database","host"));
 strcpy (login,ReadParam ("database","login"));
 strcpy (pass,ReadParam ("database","pass"));
 UL_INFO((LOGID, "KM-5 InitDriver()")); printf ("KM-5 InitDriver()\n");
 loDriver ld;		// structure of driver description
 LONG ecode;		// error code 
 CHAR	name[100];	// device name
 tTotal = TAGS_NUM_MAX;		// total tag quantity
 UL_ERROR((LOGID, "Attempt connect to database on host [%s] with login: [%s] | pass: [%s]",host,login,pass)); printf ("Attempt connect to database on host [%s] with login: %s | pass: %s\n",host,login,pass);
 dbase.dblogon();
 if (!dbase.sqlconn((UCHAR FAR *) host,(UCHAR FAR *) login,(UCHAR FAR *) pass))
	{
	 UL_ERROR((LOGID, "Cannot connect to host!")); printf ("Cannot connect to host!\n");
	}
 else {UL_ERROR((LOGID, "Connect to host success")); printf ("Connect to host success\n");}

 if (my_service) {	
      UL_ERROR((LOGID, "Driver already initialized!"));
      return 0;
  }
 memset(&ld, 0, sizeof(ld));   
 ld.ldRefreshRate =5000;		// polling time 
 ld.ldRefreshRate_min = 4000;	// minimum polling time
 ld.ldWriteTags = WriteTags;	// pointer to function write tag
 ld.ldReadTags = ReadTags;		// pointer to function read tag
 ld.ldSubscribe = activation_monitor;	// callback of tag activity
 ld.ldFlags = loDF_IGNCASE;				// ignore case
 ld.ldBranchSep = '/';					// hierarchial branch separator
 ecode = loServiceCreate(&my_service, &ld, tTotal);		//	creating loService 
 UL_TRACE((LOGID, "%!e loServiceCreate()=", ecode));	// write to log returning code
 if (ecode) return 1;									// error to create service	
 InitializeCriticalSection(&lk_values);
 EnterCriticalSection(&lk_values);
 // COM ports ------------------------------------------------------------------------------------------------
 COMMTIMEOUTS timeouts;
 timeouts.ReadIntervalTimeout = 3;
 timeouts.ReadTotalTimeoutMultiplier = 0;		//0
 timeouts.ReadTotalTimeoutConstant = 80;		// !!! (180)
 timeouts.WriteTotalTimeoutMultiplier = 00;		//0
 timeouts.WriteTotalTimeoutConstant = 25;		//25
 
 for (UINT i=1,j=0;i<PORT_NUM_MAX;i++)
	{
	 sprintf (argv0,"Port%d",i);
	 com_num[j] = atoi(ReadParam (argv0,"COM"));
	 speed[j]	= atoi(ReadParam (argv0,"Speed"));
	 UL_INFO((LOGID, "com_num[%d] && speed[%d]",com_num[j],speed[j]));
	 if (com_num[j] && speed[j])
		{
		 UL_INFO((LOGID, "Opening port COM%d on speed %d with parity %d and databits %d",com_num[j],speed[j], parity, databits));
		 if (parity==0) if (!port[j].Open(com_num[j],speed[j], SerialPort::EvenParity, databits, SerialPort::OneStopBit, SerialPort::NoFlowControl, FALSE)) { UL_ERROR((LOGID, "Error open COM-port")); continue;}
		 if (parity==2) if (!port[j].Open(com_num[j],speed[j], SerialPort::NoParity, databits, SerialPort::OneStopBit, SerialPort::NoFlowControl, FALSE)) { UL_ERROR((LOGID, "Error open COM-port")); continue;}
		 port[j].SetTimeouts(timeouts);
		 UL_INFO((LOGID, "Set COM-port timeouts %d:%d:%d:%d:%d",timeouts.ReadIntervalTimeout,timeouts.ReadTotalTimeoutMultiplier,timeouts.ReadTotalTimeoutConstant,timeouts.WriteTotalTimeoutMultiplier,timeouts.WriteTotalTimeoutConstant));
		 for (UINT pad=0; pad<=10;pad++)
			{
			 name[0]=0;
			 sprintf (argv0,"com%d",com_num[j]);
			 sprintf (tmp,"%02d",pad);
			 strcpy (name,ReadParam (argv0,tmp));
			 if (name && atoi(name)) 
				{ 
				km[KMNum].device=atoi(name); 
				km[KMNum].com=com_num[j]; 
				km[KMNum].comn=j;
				UL_INFO((LOGID, "[%d] found KM-5 [%d] (com = %d)",KMNum,km[KMNum].device,km[KMNum].com));  
				printf ("[%d] found KM-5 [%d] (com = %d)\n",KMNum,km[KMNum].device,km[KMNum].com);
				KMNum++; 
				}
			}
		 j++;
		}
	}
 LeaveCriticalSection(&lk_values);
 UL_INFO((LOGID, "Total %d devices found",KMNum)); 
 if (!KMNum) { UL_ERROR((LOGID, "No devices found")); return 1; } 

 if (init_tags())	return 1; 
 else				return 0;
}
//-------------------------------------------------------------------
UINT DestroyDriver()
{
  if (my_service)		
    {
      INT ecode = loServiceDestroy(my_service);
      UL_INFO((LOGID, "%!e loServiceDestroy(%p) = ", ecode));	// destroy derver
      DeleteCriticalSection(&lk_values);						// destroy CS
      my_service = 0;		
    }
 for (UINT i=0; i<Com_number; i++) port[i].Close();
 UL_INFO((LOGID, "Close COM-port"));						// write in log
 return	1;
}


//-----------------------------------------------------------------------------------
VOID poll_device()
{
 FILETIME ft;
 INT ecode=0;
 DWORD start=GetTickCount();
 for (UINT d=0; d<KMNum; d++)
 for (UINT r=0; r<km[d].channels; r++)
    {
	 UL_DEBUG((LOGID, "[%d/%d] (%d) type=%d",d,r,km[d].tags[r],TagR[km[d].tags[r]].type));
     if (TagR[km[d].tags[r]].type==0) km[d].ReadDataCurrent (km[d].tags[r],r);
     if (TagR[km[d].tags[r]].type==1) km[d].ReadAllArchive (km[d].tags[r],r,12);
	 if (TagR[km[d].tags[r]].type==2) km[d].ReadAllArchive (km[d].tags[r],r,7);
	 //Sleep (10000);
	}
 UL_DEBUG((LOGID, "Polling complete (%d seconds) [%d|%d]",GetTickCount()-start,KMNum,tag_num));
 ecode=Com_number;
 Sleep (200);
 GetSystemTimeAsFileTime(&ft);
 EnterCriticalSection(&lk_values);
 UL_DEBUG((LOGID, "Data to tag (%d)",tag_num));
 for (UINT ci=0;ci<tag_num; ci++)
	{
	 UL_DEBUG((LOGID, "[%d] ci = %d | v = %s",TagR[ci].type,ci,TagR[ci].value));
	 VARTYPE tvVt = tv[ci].tvValue.vt;
	 VariantClear(&tv[ci].tvValue);	  
	 if (!TagR[ci].type)
		{
		 CHAR   *stopstring;
		 V_R4(&tv[ci].tvValue) = (FLOAT) strtod(TagR[ci].value, &stopstring);
		}
	 else
		{
		 SysFreeString (bufMB);  bufMB[0]=0;
		 if (strlen (TagR[ci].value)>1000) UL_DEBUG((LOGID, "TagR[ci].value string lenght %d",strlen (TagR[ci].value)));
		 if (strlen(TagR[ci].value)>1 && TagR[ci].value[0]!=0) 
			{
			 LCID lcid = MAKELCID(0x0409, SORT_DEFAULT); // This macro creates a locale identifier from a language identifier. Specifies how dates, times, and currencies are formatted
	 	 	 MultiByteToWideChar(CP_ACP,	// ANSI code page
									  0,	// flags
						   TagR[ci].value,	// points to the character string to be converted
				 strlen(TagR[ci].value)+1,	// size in bytes of the string pointed to 
									bufMB,	// Points to a buffer that receives the translated string
			 sizeof(bufMB)/sizeof(bufMB[0]));	// function maps a character string to a wide-character (Unicode) string
			}		 
		 V_BSTR(&tv[ci].tvValue) = SysAllocString(bufMB);
		 if (wcslen (bufMB)>1000) UL_DEBUG((LOGID, "string lenght %d",wcslen (bufMB)));
		}
	 V_VT(&tv[ci].tvValue) = tvVt;
	 if (!TagR[ci].status) tv[ci].tvState.tsQuality = OPC_QUALITY_GOOD;
	 else tv[ci].tvState.tsQuality = OPC_QUALITY_UNCERTAIN;
	 tv[ci].tvState.tsQuality = OPC_QUALITY_GOOD;
	 tv[ci].tvState.tsTime = ft;
	}
 loCacheUpdate(my_service, tag_num, tv, 0);
 LeaveCriticalSection(&lk_values);
 Sleep(100);
}
//------------------------------------------------------------------------------------------------------------
int DeviceKM5::ReadDataCurrent (UINT tags_num, UINT  sens_num)
{
 UINT   rs;
 float  fl;
 BYTE   data[400];

 if (sens_num==0)
    {
     rs=send_km (CUR_REQUEST, this->addr[sens_num], this->addr[sens_num], 0);
     if (rs)  rs = this->read_km(data, 0);
    }
 rs=send_km (CUR_REQUEST, this->addr[sens_num], this->addr[sens_num], 0);
 if (rs)  rs = this->read_km(data, 0); 
 if (rs>5) 
    { 
     fl=*(float*)(data+this->cur[sens_num]+3);
     if (fl>100000000 || fl<-1000)
        {
         fl=0;
         //StoreData (this->device, this->prm[sens_num], this->pipe[sens_num], 10, fl);
		}
	 else
		{
		 if (this->prm[sens_num]==4) 
			{
			 if (fl>10 && fl<100) StoreData (1000+TagR[tags_num].pipe*10+TagR[tags_num].prm, this->prm[sens_num], this->pipe[sens_num], 0, fl);
			 else TagR[tag_num].status=1;
			}
	     else 
			{
			 if (fl>0)
			 if (this->prm[sens_num]>20)StoreData (1000+TagR[tags_num].pipe*10+TagR[tags_num].prm, this->prm[sens_num]-10, this->pipe[sens_num], 0, fl);
			 else StoreData (1000+TagR[tags_num].pipe*10+TagR[tags_num].prm, this->prm[sens_num], this->pipe[sens_num], 0, fl);
			 TagR[tags_num].status=0;
			}
		 UL_DEBUG((LOGID,"[km] [%d][%d] [%d][%d] [0x%x 0x%x 0x%x 0x%x] [%f]",1000+TagR[tags_num].pipe*10+TagR[tags_num].prm,this->prm[sens_num],sens_num,this->cur[sens_num],data[this->cur[sens_num]+3],data[this->cur[sens_num]+4],data[this->cur[sens_num]+5],data[this->cur[sens_num]+6],fl));
		 printf ("[km] [%d][%d] [%d][%d] [0x%x 0x%x 0x%x 0x%x] [%f]\n",1000+TagR[tags_num].pipe*10+TagR[tags_num].prm,this->prm[sens_num],sens_num,this->cur[sens_num],data[this->cur[sens_num]+3],data[this->cur[sens_num]+4],data[this->cur[sens_num]+5],data[this->cur[sens_num]+6],fl);
		}
	 sprintf (TagR[tags_num].value,"%f",fl);
	} 
 return 0;
}
//-----------------------------------------------------------------------------
// ReadDataArchive - read single device. Readed data will be stored in DB
int DeviceKM5::ReadAllArchive (UINT tags_num, UINT  sens_num, UINT tp)
{
 BOOL   rs;
 BYTE   data[400];
 CHAR   date[20];
 float  value;
 UINT   vsk=0,index=0;
 
 if (this->cur[sens_num])
    {    
     rs=(BOOL)send_km (NSTR_REQUEST, TagR[tags_num].type-1, 0, 1);
     Sleep (2000);
     rs = this->read_km(data, 1);
     //index=(data[6]&0xf0)>>4*10+(data[6]&0xf)+(data[7]&0xf0)>>4*1000+(data[6]&0xf)*100;
     index=data[7]*256+data[6];
     vsk=tp;
    
     while (index>=0 && vsk>0)
	 {	  
       rs=send_km (STR_REQUEST, TagR[tags_num].type-1, index, 2);
	   Sleep (2000);
       rs = this->read_km(data, 2);
	   //UL_DEBUG((LOGID,"[km] [rs=%d]",rs));
       if (data[5]!=0xee) break;
	   if (rs)  
	    {	     
	     if (TagR[tags_num].type==1) 
				{
				 int hrr=((data[10]&0xf0)>>4)*10+(data[10]&0xf);
				 if (hrr>0) hrr--;
				 else hrr=0;
				 sprintf (date,"%04d%02d%02d%02d0000",((data[8]&0xf0)>>4)*10+(data[8]&0xf)+2000,((data[7]&0xf0)>>4)*10+(data[7]&0xf),((data[6]&0xf0)>>4)*10+(data[6]&0xf),hrr);
				}
		 if (TagR[tags_num].type==2) sprintf (date,"%04d%02d%02d000000",((data[8]&0xf0)>>4)*10+(data[8]&0xf)+2000,((data[7]&0xf0)>>4)*10+(data[7]&0xf),((data[6]&0xf0)>>4)*10+(data[6]&0xf));
	    }
	  if (rs)  value=*(float*)(data+4+this->cur[sens_num]);
	  sprintf (TagR[tags_num].value,"(%s) %f",date,value);

      if (rs)  
			{
			 UL_DEBUG((LOGID,"[km] [%d][%d] [%d] [%d] [%x %x %x %x][%f]",1000+TagR[tags_num].pipe*10+TagR[tags_num].prm,index,sens_num,this->cur[sens_num],data[4+this->cur[sens_num]],data[5+this->cur[sens_num]],data[6+this->cur[sens_num]],data[7+this->cur[sens_num]],value));
			 printf ("[km] [%d] [%d] [%d] [%d] [%x %x %x %x][%f]\n",1000+TagR[tags_num].pipe*10+TagR[tags_num].prm,index,sens_num,this->cur[sens_num],data[4+this->cur[sens_num]],data[5+this->cur[sens_num]],data[6+this->cur[sens_num]],data[7+this->cur[sens_num]],value);
			}
      if (rs)  if (value<10000000 && value>0) 

	  if ((((data[8]&0xf0)>>4)*10+(data[8]&0xf))>8)
	    {	     
		 TagR[tags_num].status=0;
	     if (value>0) StoreData (1000+TagR[tags_num].pipe*10+TagR[tags_num].prm, this->prm[sens_num], this->pipe[sens_num], TagR[tags_num].type, 0, value, date);
	     if (this->prm[sens_num]==23) if (value>0) StoreDataC (1000+TagR[tags_num].pipe*10+TagR[tags_num].prm, 23, 1000+TagR[tags_num].pipe*10+TagR[tags_num].prm+5, TagR[tags_num].type, 0, value, date);
	     if (this->prm[sens_num]==22) if (value>0) StoreDataC (1000+TagR[tags_num].pipe*10+TagR[tags_num].prm, 22, 1000+TagR[tags_num].pipe*10+TagR[tags_num].prm+5, TagR[tags_num].type, 0, value, date);
	    }	  
	  if (index==0) break;
	  index--; vsk--;
	 }
    }
 return 0;
}
//---------------------------------------------------------------------------------------------------
BOOL StoreData (UINT dv, UINT prm, UINT pipe, UINT status, FLOAT value)
{
 sprintf (query,"SELECT * FROM currentsdata WHERE ID_Channel=%d",dv);
 //printf ("%s\n",query); UL_INFO((LOGID, "!!!!!!!!!!!!!!!!!!!!! (%s)",query));
 res=dbase.sqlexec((UCHAR FAR *)query,dataset);
 if (res>1) sprintf (query,"UPDATE currentsdata SET Value=%f,MeasureDate=NOW() WHERE ID_Channel=%d",value,dv);
 else 
    {
     sprintf (query,"INSERT INTO currentsdata(ID_Channel,MeasureDate,Value) VALUES('%d',NOW(),'%f')",dv,value);
    }
 res=dbase.sqlexec((UCHAR FAR *)query,dataset);  
 UL_INFO((LOGID, "SQLDirect (%s) retcode = %d",query,res));
 return true;
}
//---------------------------------------------------------------------------------------------------
BOOL StoreData (UINT dv, UINT prm, UINT pipe, UINT type, UINT status, FLOAT value, CHAR* data)
{
 //SQLRETURN  retcode;
 SQLCloseCursor(dbase.hstmt);
 if (type==1) sprintf (query,"SELECT * FROM mains WHERE ID_Channel=%d AND MeasureDate=%s",dv,data);
 if (type==2) sprintf (query,"SELECT * FROM daydata WHERE ID_Channel=%d AND MeasureDate=%s",dv,data);
 SQLRETURN retcode=SQLExecDirect (dbase.hstmt, (UCHAR *)query, SQL_NTS);
 UL_INFO((LOGID, "SQLDirect (%s) retcode = %d",query,retcode));
 if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) 
	{
	 retcode = SQLFetch(dbase.hstmt);
	 if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
	    {
		 if (type==1) sprintf (query,"UPDATE mains SET Value='%f',State='%d',MeasureDate=MeasureDate WHERE MeasureDate='%s' AND ID_Channel=%d",value,status,data,dv);
		 if (type==2) sprintf (query,"UPDATE daydata SET Value='%f',State='%d',MeasureDate=MeasureDate WHERE MeasureDate='%s' AND ID_Channel=%d",value,status,data,dv);
		 SQLCloseCursor(dbase.hstmt);
		 retcode=SQLExecDirect (dbase.hstmt, (UCHAR *)query, SQL_NTS);
		 UL_INFO((LOGID, "SQLDirect (%s) retcode = %d",query,res));
		 return true;     
		}
	else 
		{
		 if (type==1) sprintf (query,"INSERT INTO mains(ID_Channel,MeasureDate,Value,State) VALUES('%d','%s','%f','%d')",dv,data,value,status);
		 if (type==2) sprintf (query,"INSERT INTO daydata(ID_Channel,MeasureDate,Value,State) VALUES('%d','%s','%f','%d')",dv,data,value,status);
		}
	}
 //res=dbase.sqlexec((UCHAR FAR *)query, dataset);
 SQLCloseCursor(dbase.hstmt);
 retcode=SQLExecDirect (dbase.hstmt, (UCHAR *)query, SQL_NTS);
 if (retcode==-1)
		{
		 sqlerror++;
		 if (sqlerror>3)
			 {
 				dbase.sqldisconn();			 
				CHAR host[100],login[100],pass[100];
				strcpy (host,ReadParam ("database","host"));
				strcpy (login,ReadParam ("database","login"));
				strcpy (pass,ReadParam ("database","pass"));
				while (1)
					{
					 if (!dbase.sqlconn((UCHAR FAR *) host,(UCHAR FAR *) login,(UCHAR FAR *) pass))
						UL_ERROR((LOGID, "Cannot connect to host!"));
					 else { UL_ERROR((LOGID, "Connect to host success")); break; }
					}
			}
		}
 else sqlerror=0;
 UL_INFO((LOGID, "SQLDirect (%s) retcode = %d",query,res));
 SQLCloseCursor(dbase.hstmt);
 return true;
}
//---------------------------------------------------------------------------------------------------
BOOL StoreDataC (UINT dv, UINT prm, UINT prm2, UINT type, UINT status, FLOAT value, CHAR* date)
{ 
 CHAR dat[40];
 SYSTEMTIME curr;
 TIMESTAMP_STRUCT	btime;
 DOUBLE		flt=0;

 GetLocalTime (&curr);
 if (type==1) sprintf (dat,"%04d%02d%02d%02d0000",curr.wYear,curr.wMonth,curr.wDay,curr.wMinute);
 if (type==2) sprintf (dat,"%04d%02d%02d000000",curr.wYear,curr.wMonth,curr.wDay);
 time_t tim;
 struct tm ct;
 SQLCloseCursor(dbase.hstmt);
 if (type==1) sprintf (query,"SELECT * FROM mains WHERE ID_Channel=%d AND MeasureDate<%s ORDER BY MeasureDate DESC",dv,date);
 if (type==2) sprintf (query,"SELECT * FROM daydata WHERE ID_Channel=%d AND MeasureDate<%s ORDER BY MeasureDate DESC",dv,date);
 SQLRETURN  retcode=SQLExecDirect (dbase.hstmt, (UCHAR *)query, SQL_NTS);
 UL_INFO((LOGID, "SQLDirect (%s) retcode = %d",query,retcode));
 if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) 
		{
		 retcode = SQLFetch(dbase.hstmt);
		 if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO)
			{
			 SQLGetData(dbase.hstmt, 2, SQL_C_TYPE_TIMESTAMP, &btime, sizeof (btime), NULL);
			 SQLGetData(dbase.hstmt, 3, SQL_C_DOUBLE, &flt, sizeof (flt), NULL);
			 dat[0]=0;
			 if (type==1) sprintf (dat,"%04d%02d%02d%02d0000",btime.year,btime.month,btime.day,btime.hour);
			 if (type==2) sprintf (dat,"%04d%02d%02d000000",btime.year,btime.month,btime.day);
			 UL_INFO((LOGID, "[km] row=%s %04d%02d%02d%02d0000 [%f-%f]",dat,btime.year,btime.month,btime.day,btime.hour,value,flt));
		     SQLCloseCursor(dbase.hstmt);
			 DOUBLE	dt=value-flt;
			 if (type==1) sprintf (query,"SELECT * FROM mains WHERE ID_Channel=%d AND MeasureDate=%s",prm2,date);
			 if (type==2) sprintf (query,"SELECT * FROM daydata WHERE ID_Channel=%d AND MeasureDate=%s",prm2,date);
			 retcode=SQLExecDirect (dbase.hstmt, (UCHAR *)query, SQL_NTS);
			 UL_INFO((LOGID, "SQLDirect (%s) retcode = %d",query,retcode));
			 if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) 
			 if (dt>0 && dt<10000 && btime.year>2009 && btime.year<2100)
				{
				 retcode = SQLFetch(dbase.hstmt);
				 if (retcode == SQL_SUCCESS || retcode == SQL_SUCCESS_WITH_INFO) 
					{
					 if (type==2) sprintf (query,"UPDATE daydata SET Value='%f',State='%d',MeasureDate=MeasureDate WHERE MeasureDate='%s' AND ID_Channel=%d",dt,status,date,prm2);
					}
			     else 
					{
					 if (type==1) sprintf (query,"INSERT INTO mains(ID_Channel,MeasureDate,Value,State) VALUES('%d','%s','%f','%d')",prm2,date,dt,status);
					 if (type==2) sprintf (query,"INSERT INTO daydata(ID_Channel,MeasureDate,Value,State) VALUES('%d','%s','%f','%d')",prm2,date,dt,status);
					}
				 retcode=SQLExecDirect (dbase.hstmt, (UCHAR *)query, SQL_NTS);
				 UL_INFO((LOGID, "SQLDirect (%s) retcode = %d",query,retcode));
				}
			 SQLCloseCursor(dbase.hstmt);
			}
		}
 return true;
}
//-----------------------------------------------------------------------------
BOOL DeviceKM5::send_km (UINT op, UINT prm, UINT index, UINT frame)
    {
     UINT       crc=0;          //(* CRC checksum *)
     UINT       nbytes = 0;     //(* number of bytes in send packet *)
     BYTE       data[100];      //(* send sequence *)
     //BYTE		sn[2];
	 DWORD dwErrors=CE_FRAME|CE_IOE|CE_TXFULL|CE_RXPARITY|CE_RXOVER|CE_OVERRUN|CE_MODE|CE_BREAK;
	 port[this->comn].ClearWriteBuffer();
	 port[this->comn].Purge(PURGE_TXABORT|PURGE_RXABORT|PURGE_TXCLEAR|PURGE_RXCLEAR);
	 port[this->comn].ClearError(dwErrors);
	 //411709	 
     data[0]=(this->device&0xff0000)>>16;
     data[1]=(this->device&0xff00)>>8; 
     data[2]=this->device&0xff;
     data[3]=0x0;     
     if (frame==0)
        {
		 data[4]=op;
		 data[5]=prm;
		 data[6]=0x0;
		 data[7]=0x0; data[8]=0x0; data[9]=0x0; data[10]=0x0; data[11]=0x0; data[12]=0x0; data[13]=0x0;
         data[14]=CRC (data, 14, 1);
         data[15]=CRC (data, 14, 2);
         UL_INFO((LOGID, "[km] wr[0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]",data[0],data[1],data[2],data[3],data[4],data[5],data[6],data[7],data[8],data[9],data[10],data[11],data[12],data[13],data[14],data[15]));
		 port[this->comn].Write(data, 16);
		 //write (fd,&data,16);
        }     
     if (frame==1)
        {
		 data[4]=op;
		 data[5]=prm;
		 SYSTEMTIME curr;
		 GetLocalTime (&curr);
		 UL_INFO((LOGID, "[km] wr[%d,%d,%d]",curr.wYear,curr.wMonth,curr.wDay));
		 data[6]=((curr.wDay/10)<<4)+(curr.wDay%10); data[7]=((((curr.wMonth)/10)<<4)+(curr.wMonth)%10); data[8]=(((curr.wYear-100)/10)<<4)+(curr.wYear-100)%10; data[9]=((curr.wMinute/10)<<4)+curr.wMinute%10; 
		 data[10]=0x0; data[11]=0x0; data[12]=0x0; data[13]=0x0;
         data[14]=CRC (data, 14, 1);
         data[15]=CRC (data, 14, 2);
         UL_INFO((LOGID, "[km] wr[0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]",data[0],data[1],data[2],data[3],data[4],data[5],data[6],data[7],data[8],data[9],data[10],data[11],data[12],data[13],data[14],data[15]));
		 port[this->comn].Write(data, 16);
		 //write (fd,&data,16);
        }     
	 //rs=send_km (STR_REQUEST, this->cur[sens_num], index, 2);
     if (frame==2)
        {
		 data[4]=op;
		 data[5]=prm;
		 data[7]=index/256; data[6]=index%256;
		 data[8]=0x0; data[9]=0x0; data[10]=0x0; data[11]=0x0; data[12]=0x0; data[13]=0x0;
         data[14]=CRC (data, 14, 1);
         data[15]=CRC (data, 14, 2);
         UL_INFO((LOGID, "[km] wr[0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]",data[0],data[1],data[2],data[3],data[4],data[5],data[6],data[7],data[8],data[9],data[10],data[11],data[12],data[13],data[14],data[15]));
		 port[this->comn].Write(data, 16);
		 //write (fd,&data,16);
        }     
     if (frame==3)
        {
		 data[4]=4;
		 data[5]=0;
		 data[6]=0;
		 data[7]=0x0; data[8]=0x0; data[9]=0x0; data[10]=0x0; data[11]=0x0; data[12]=0x0; data[13]=0x0;
         data[14]=CRC (data, 14, 1);
         data[15]=CRC (data, 14, 2);
         UL_INFO((LOGID, "[km] wr[0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]",data[0],data[1],data[2],data[3],data[4],data[5],data[6],data[7],data[8],data[9],data[10],data[11],data[12],data[13],data[14],data[15]));
		 port[this->comn].Write(data, 16);
		 //write (fd,&data,16);
        }
     return true;     
    }
//-----------------------------------------------------------------------------    
UINT  DeviceKM5::read_km (BYTE* dat, BYTE type)
    {
     UINT       crc=0,crc2=0;   //(* CRC checksum *)
     INT        nbytes = 0;     //(* number of bytes in recieve packet *)
     INT        bytes = 0;      //(* number of bytes in packet *)
     BYTE       data[500];      //(* recieve sequence *)
     UINT       i=0;            //(* current position *)
     UCHAR      ok=0xFF;        //(* flajochek *)
     CHAR       op=0;           //(* operation *)

     Sleep (1000);
	 nbytes=port[this->comn].Read(data, 100);
     //UL_INFO((LOGID, "[km] nbytes=%d",nbytes));
     if (nbytes>5)
        {
         UL_INFO((LOGID, "[km] [%d][0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x]",nbytes,data[0],data[1],data[2],data[3],data[4],data[5],data[6],data[7],data[8],data[9],data[10],data[11],data[12],data[13],data[14],data[15],data[16],data[17]));  
		 crc=CRC (data, nbytes-2, 1);
		 crc2=CRC (data, nbytes-2, 2);
         UL_INFO((LOGID, "[km][crc][0x%x,0x%x | 0x%x,0x%x]",data[nbytes-2],data[nbytes-1],crc,crc2));
		 if (crc!=data[nbytes-2] || crc2!=data[nbytes-1]) nbytes=0;
	     if (type==0x0)
            {
             memcpy (dat,data,32);
             return nbytes;
            }
         if (type==2)
            {
             memcpy (dat,data,nbytes);
             return nbytes;
            }
         if (type==1)
            {
             memcpy (dat,data,18);
             return nbytes;
            }
         return 0;
        }
     return 0;
    }
//-------------------------------------------------------------------
VOID WriteToPort (UINT com, UINT device, CHAR* Out)
{
 DWORD enddt=0, dwbr1=0;
 COMSTAT stat; CHAR sBuf1[1000];
 DWORD dwErrors=CE_FRAME|CE_IOE|CE_TXFULL|CE_RXPARITY|CE_RXOVER|CE_OVERRUN|CE_MODE|CE_BREAK;
 port[com].ClearWriteBuffer();
 port[com].Purge(PURGE_TXABORT|PURGE_RXABORT|PURGE_TXCLEAR|PURGE_RXCLEAR);
 port[com].ClearError(dwErrors);
 port[com].Read(sBuf1, 100);
 for (UINT po=0;po<=strlen(Out);po++)
	{
	 port[com].Write (Out+po, 1);
	 //UL_INFO((LOGID,"[%d] O[%d] = 0x%x",device,po,(UCHAR)Out[po]));
	}
 for (UINT rr=0; rr<40; rr++)
	{
	 port[com].GetStatus(stat);
     if (stat.cbInQue)
		{
		 dwbr1 = port[com].Read(sBuf1, 200);
		 break;
		}
	 Sleep (100);
	}
 //if (dwbr1>0 && dwbr1<100) for (UINT i=0;i<dwbr1;i++) UL_INFO((LOGID,"[%d]  [%d] = 0x%x [%c]",device,i,(UCHAR)sBuf1[i],(UCHAR)sBuf1[i]));
}
//-----------------------------------------------------------------------------        
BYTE CRC(const BYTE* const Data, const BYTE DataSize, BYTE type)
    {
     BYTE _CRC = 0;     
     BYTE* _Data = (BYTE*)Data; 
        for(unsigned int i = 0; i < DataSize; i++) 
		{
	         if (type==1) _CRC ^= *_Data++;
        	 if (type==2) _CRC += *_Data++;
		}
        return _CRC;
    }
//-----------------------------------------------------------------------------------
CHAR* ReadParam (CHAR *SectionName,CHAR *Value)
{
 CHAR buf[150], string1[50], string2[50]; CHAR ret[150]={0};
 CHAR *pbuf=buf; CHAR *bret=ret;
 UINT s_ok=0;
 sprintf(string1,"[%s]",SectionName);
 sprintf(string2,"%s=",Value);
 //UL_INFO((LOGID, "%s %s [%d]",string1,string2,CfgFile));
 rewind(CfgFile);
 while(!feof(CfgFile))
 if(fgets(buf,80,CfgFile)!=NULL)
	{
	 if (strstr(buf,string1))
		{ s_ok=1; break; }
	}
 if (s_ok)
	{
	 while(!feof(CfgFile))
		{
		 if(fgets(buf,100,CfgFile)!=NULL)
			{
			 //UL_INFO((LOGID, "fgets %s",buf));
			 if (strstr(buf,"[")==NULL && strstr(buf,"]")==NULL)
				{
				 //UL_INFO((LOGID, "fgets []",buf));
				 for (s_ok=0;s_ok<strlen(buf)-1;s_ok++) if (buf[s_ok]==';') buf[s_ok+1]='\0';
				 if (strstr(buf,string2))
					{
					 //UL_INFO((LOGID, "%s present",string2));
					 for (s_ok=0;s_ok<strlen(buf)-1;s_ok++)
						if (s_ok>=strlen(string2)) buf[s_ok-strlen(Value)-1]=buf[s_ok];
							 buf[s_ok-strlen(string2)]='\0';					
					 strcpy(ret,buf);
					 printf ("found: ret = %s\n",ret);
					 return bret;
					}
				}
			  else { buf[0]=0; buf[1]=0; strcpy(ret,buf); return bret; }
			 }
		}	 	
 	 if (SectionName=="Port")	{ buf[0]='1'; buf[1]=0;}
	 buf[0]=0; buf[1]=0;
	 strcpy(ret,buf); return bret;
	}
else{
	 sprintf(buf, "error");			// if something go wrong return error
	 strcpy(ret,buf); return bret;
	}	
}
//------------------------------------------------------------------------------------
INT init_tags(VOID)
{
  UL_INFO((LOGID, "init_tags()"));
  FILETIME ft;		//  64-bit value representing the number of 100-ns intervals since January 1,1601
  UINT rights=0;	// tag type (read/write)
  UINT ecode,i=0;
  UINT	tags_per_km=sizeof(Tag)/sizeof(TR);
  WCHAR buf[DATALEN_MAX];
  GetSystemTimeAsFileTime(&ft);	// retrieves the current system date and time
  EnterCriticalSection(&lk_values);
  LCID lcid = MAKELCID(0x0409, SORT_DEFAULT); // This macro creates a locale identifier from a language identifier. Specifies how dates, times, and currencies are formatted
  for (UINT kkm=0; kkm<KMNum; kkm++)
	{
	 rights = OPC_READABLE; km[kkm].channels=0;	 
	 for (UINT r=0; r<tags_per_km; r++)
		{
		 tn[i] = new char[DATALEN_MAX];	// reserve memory for massive
		 sprintf(tn[i],"com%d-%0.2d/%s",km[kkm].com,km[kkm].device,Tag[r].name);
		 VariantInit(&tv[i].tvValue);
	 	 MultiByteToWideChar(CP_ACP, 0,tn[i], strlen(tn[i])+1,	buf, sizeof(buf)/sizeof(buf[0])); // function maps a character string to a wide-character (Unicode) string
		 if (!Tag[r].type)
			{
			 V_R4(&tv[i].tvValue) = 0.0;
			 TagR[i].type = 0;		// instant
			 V_VT(&tv[i].tvValue) = VT_R4;
			}
		 else
			{
			 V_BSTR(&tv[i].tvValue) = SysAllocString(L"");
			 TagR[i].type = Tag[r].type;		// archive
			 V_VT(&tv[i].tvValue) = VT_BSTR;
			}
		 ecode = loAddRealTag_aW(my_service, &ti[i], (loRealTag)(i+1), buf, 0, rights, &tv[i].tvValue, 0, 0);
		 tv[i].tvTi = ti[i];
		 tv[i].tvState.tsTime = ft;
		 tv[i].tvState.tsError = S_OK;
		 tv[i].tvState.tsQuality = OPC_QUALITY_NOT_CONNECTED;
		 UL_TRACE((LOGID, "%!e loAddRealTag(%s) = %u (t=%d)", ecode, tn[i], ti[i], TagR[i].type));
		 printf ("loAddRealTag(%s) = %u (t=%d)\n", tn[i], ti[i], TagR[i].type);
		 TagR[i].name = new char[DATALEN_MAX];
		 
		 sprintf (TagR[i].name,"%s",tn[i]);
		 TagR[i].pipe=Tag[r].pipe; 
		 TagR[i].prm=Tag[r].prm; 
		 km[kkm].pipe[r]=Tag[r].pipe; km[kkm].addr[r]=Tag[r].adr; km[kkm].cur[r]=Tag[r].del; km[kkm].prm[r]=Tag[r].prm; km[kkm].tags[r]=i;
		 i++; tag_num++; km[kkm].channels++;
		}
	 } 
  LeaveCriticalSection(&lk_values);
  for (i=0; i<tag_num; i++) delete tn[i];
  if(ecode) 
  {
    UL_ERROR((LOGID, "%!e driver_init()=", ecode));
    return -1;
  }
  return 0;
}
//-----------------------------------------------------------------------------------
CHAR *absPath(CHAR *fileName)					// return abs path of file
{ static char path[sizeof(argv0)]="\0";			// path - massive of comline
  CHAR *cp;
  if(*path=='\0') strncpy(path, argv0, 255);
  if(NULL==(cp=strrchr(path,'\\'))) cp=path; else cp++;
  cp=strncpy(cp,fileName,255);
  return path;}
//----------------------------------------------------------------------------------
