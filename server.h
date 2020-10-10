//-----------------------------------------------------------------------------
#include <windows.h>
#include "unilog.h"
#include <time.h>
//-----------------------------------------------------------------------------
#define LOGID logg,0				// log identifiner
#define LOG_FNAME	"km5.log"		// log name
#define CFG_FILE	"km5.ini"		// cfg name
#define PORTNAME_LEN 15				// port name lenght
#define PORT_NUM_MAX 2				// maximum number of ports	
#define COMMANDS_NUM_MAX 100		// maximum number of commands
#define TAGS_NUM_MAX 500			// maximum number of tags
#define DATALEN_MAX 120				// maximum lenght of the tags
#define DEVICE_NUM_MAX	10			// maximum number of "logiks" on one APC79
#define MAX_KM_NUM		10			// ������������ ����� ��-5
#define ARCHIVE_NUM_MAX		300		// ������������ ���������� ��������� ������
//-----------------------------------------------------------------------------
#define	CUR_REQUEST		44
#define	NSTR_REQUEST	52
#define	STR_REQUEST		65
#define MAX_CHANNELS	50

class DeviceKM5;
class DeviceKM5 {
public:    
    UINT	idt;
    UINT	device;    		// device id
	UINT	com;			// com port
	UINT	comn;			// com port
	UINT	channels;		// number of channels
    UINT	pipe[MAX_CHANNELS];	// identificator
    UINT	cur[MAX_CHANNELS];	// nparametr for read values
    UINT	prm[MAX_CHANNELS];	// prm identificator    
    UINT	n_hour[MAX_CHANNELS];	// sm
    UINT	n_day[MAX_CHANNELS];	// sm
    UINT	addr[MAX_CHANNELS];		// sm
	UINT	tags[MAX_CHANNELS];		// tags link
public:    
    // constructor
    DeviceKM5 () {};
    ~DeviceKM5 () {};
    int ReadDataCurrent (UINT tags_num, UINT sens_num);
    int ReadAllArchive (UINT tags_num, UINT  sens_num, UINT tp);
    BOOL send_km (UINT op, UINT prm, UINT frame, UINT index);
    UINT read_km (BYTE* dat, BYTE type);    
};

typedef struct _TR TR;		// channels info
struct _TR {
  char *name;
  UINT	type;
  UINT	getCmd; 
  UINT	pipe;
  UINT	adr;
  UINT	del;
  UINT	prm;
  CHAR  value[700];
  SHORT status;		// ������� ������, (0-��� �����, 1-���������, 2-:).
};

TR TagR[TAGS_NUM_MAX];
TR Tag[] =
 {{"������ G1/������� �������� ������� (�.�)",	0,	44,	0,	5,	6,	22,"", 0},
  {"������ G1/������� �������� ������� (�.�)",	1,	65,	0,	5,	37,	22,"", 0},
  {"������ G1/������� �������� ������� (�.�)",	2,	65,	0,	5,	37,	22,"", 0},
  {"������ G2/������� �������� ������� (�.�)",	0,	44,	1,	7,	6,	22,"", 0},
  {"������ G2/������� �������� ������� (�.�)",	1,	65,	1,	7,	41,	22,"", 0},
  {"������ G2/������� �������� ������� (�.�)",	2,	65,	1,	7,	41,	22,"", 0},
  {"����������� T1/����������� ��������� ������������ �������(C)",	0,	44,	0,	5,	18,	4,"", 0},
  {"����������� T1/����������� ��������� ������������ �������(C)",	1,	65,	0,	7,	25,	4,"", 0},
  {"����������� T1/����������� ��������� ������������ �������(C)",	2,	65,	0,	7,	25,	4,"", 0},
  {"����������� T2/����������� ��������� ������������ �������(C)",	0,	44,	1,	5,	22,	4,"", 0},
  {"����������� T2/����������� ��������� ������������ �������(C)",	1,	65,	1,	7,	29,	4,"", 0},
  {"����������� T2/����������� ��������� ������������ �������(C)",	2,	65,	1,	7,	29,	4,"", 0},
  {"�������� ������� Q/�������� ������� �������(���)",	1,	65,	0,	8,	57,	23,"", 0},
  {"�������� ������� Q/�������� ������� �������(���)",	2,	65,	0,	8,	57,	23,"", 0}};
