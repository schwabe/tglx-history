#ifdef _INIT_
#define EXTERN
#else
#define EXTERN extern
#endif /* _INIT_ */

typedef struct _SiS_PanelDelayTblStruct
{
 UCHAR timer[2];
} SiS_PanelDelayTblStruct;

typedef struct _SiS_LCDDataStruct
{
	USHORT RVBHCMAX;
	USHORT RVBHCFACT;
	USHORT VGAHT;
	USHORT VGAVT;
	USHORT LCDHT;
	USHORT LCDVT;
} SiS_LCDDataStruct;

typedef struct _SiS_TVDataStruct
{
	USHORT RVBHCMAX;
	USHORT RVBHCFACT;
	USHORT VGAHT;
	USHORT VGAVT;
	USHORT TVHDE;
	USHORT TVVDE;
	USHORT RVBHRS;
	UCHAR FlickerMode;
	USHORT HALFRVBHRS;
	UCHAR RY1COE;
	UCHAR RY2COE;
	UCHAR RY3COE;
	UCHAR RY4COE;
} SiS_TVDataStruct;

typedef struct _SiS_LVDSDataStruct
{
	USHORT VGAHT;
	USHORT VGAVT;
	USHORT LCDHT;
	USHORT LCDVT;
} SiS_LVDSDataStruct;

typedef struct _SiS_LVDSDesStruct
{
	USHORT LCDHDES;
	USHORT LCDVDES;
} SiS_LVDSDesStruct;

typedef struct _SiS_LVDSCRT1DataStruct
{
	UCHAR CR[15];
} SiS_LVDSCRT1DataStruct;

/*add for LCDA*/
typedef struct _SiS_LCDACRT1DataStruct
{
	UCHAR CR[17];
} SiS_LCDACRT1DataStruct;

typedef struct _SiS_CHTVRegDataStruct
{
	UCHAR Reg[16];
} SiS_CHTVRegDataStruct;

typedef struct _SiS_StStruct
{
	UCHAR St_ModeID;
	USHORT St_ModeFlag;
	UCHAR St_StTableIndex;
	UCHAR St_CRT2CRTC;
	UCHAR St_ResInfo;
	UCHAR VB_StTVFlickerIndex;
	UCHAR VB_StTVEdgeIndex;
	UCHAR VB_StTVYFilterIndex;
} SiS_StStruct;

typedef struct _SiS_VBModeStruct
{
	UCHAR  ModeID;
	UCHAR  VB_TVDelayIndex;
	UCHAR  VB_TVFlickerIndex;
	UCHAR  VB_TVPhaseIndex;
	UCHAR  VB_TVYFilterIndex;
	UCHAR  VB_LCDDelayIndex;
	UCHAR  _VB_LCDHIndex;
	UCHAR  _VB_LCDVIndex;
} SiS_VBModeStruct;

typedef struct _SiS_StandTableStruct
{
	UCHAR CRT_COLS;
	UCHAR ROWS;
	UCHAR CHAR_HEIGHT;
	USHORT CRT_LEN;
	UCHAR SR[4];
	UCHAR MISC;
	UCHAR CRTC[0x19];
	UCHAR ATTR[0x14];
	UCHAR GRC[9];
} SiS_StandTableStruct;

typedef struct _SiS_ExtStruct
{
	UCHAR Ext_ModeID;
	USHORT Ext_ModeFlag;
	USHORT Ext_ModeInfo;
	USHORT Ext_Point;
	USHORT Ext_VESAID;
	UCHAR Ext_VESAMEMSize;
	UCHAR Ext_RESINFO;
	UCHAR VB_ExtTVFlickerIndex;
	UCHAR VB_ExtTVEdgeIndex;
	UCHAR VB_ExtTVYFilterIndex;
	UCHAR REFindex;
} SiS_ExtStruct;

typedef struct _SiS_Ext2Struct
{
	USHORT Ext_InfoFlag;
	UCHAR Ext_CRT1CRTC;
	UCHAR Ext_CRTVCLK;
	UCHAR Ext_CRT2CRTC;
	UCHAR  ModeID;
	USHORT XRes;
	USHORT YRes;
	USHORT ROM_OFFSET;
} SiS_Ext2Struct;

typedef struct _SiS_Part2PortTblStruct
{
 	UCHAR CR[12];
} SiS_Part2PortTblStruct;

typedef struct _SiS_CRT1TableStruct
{
	UCHAR CR[17];
} SiS_CRT1TableStruct;

typedef struct _SiS_MCLKDataStruct
{
	UCHAR SR28,SR29,SR2A;
	USHORT CLOCK;
} SiS_MCLKDataStruct;

typedef struct _SiS_ECLKDataStruct
{
	UCHAR SR2E,SR2F,SR30;
	USHORT CLOCK;
} SiS_ECLKDataStruct;

typedef struct _SiS_VCLKDataStruct
{
	UCHAR SR2B,SR2C;
	USHORT CLOCK;
} SiS_VCLKDataStruct;

typedef struct _SiS_VBVCLKDataStruct
{
	UCHAR Part4_A,Part4_B;
	USHORT CLOCK;
} SiS_VBVCLKDataStruct;

typedef struct _SiS_StResInfoStruct
{
	USHORT HTotal;
	USHORT VTotal;
} SiS_StResInfoStruct;

typedef struct _SiS_ModeResInfoStruct
{
	USHORT HTotal;
	USHORT VTotal;
	UCHAR  XChar;
	UCHAR  YChar;
} SiS_ModeResInfoStruct;

EXTERN SiS_StStruct *SiS_SModeIDTable;
EXTERN SiS_StandTableStruct *SiS_StandTable;
EXTERN SiS_ExtStruct  *SiS_EModeIDTable;
EXTERN SiS_Ext2Struct  *SiS_RefIndex;
EXTERN SiS_VBModeStruct *SiS_VBModeIDTable;
EXTERN SiS_CRT1TableStruct  *SiS_CRT1Table;
EXTERN SiS_MCLKDataStruct  *SiS_MCLKData_0;
EXTERN SiS_MCLKDataStruct  *SiS_MCLKData_1;
EXTERN SiS_ECLKDataStruct  *SiS_ECLKData;
EXTERN SiS_VCLKDataStruct  *SiS_VCLKData;
EXTERN SiS_VBVCLKDataStruct  *SiS_VBVCLKData;
EXTERN SiS_StResInfoStruct  *SiS_StResInfo;
EXTERN SiS_ModeResInfoStruct  *SiS_ModeResInfo;
EXTERN UCHAR *SiS_ScreenOffset;

EXTERN UCHAR *pSiS_OutputSelect;
EXTERN UCHAR *pSiS_SoftSetting;
EXTERN UCHAR *pSiS_SR07;

typedef UCHAR DRAM4Type[4];
EXTERN DRAM4Type *SiS_SR15; /* pointer : point to array */
EXTERN DRAM4Type *SiS_CR40; /* pointer : point to array */
EXTERN UCHAR *SiS_CR49;
EXTERN UCHAR *SiS_SR25;

EXTERN UCHAR *pSiS_SR1F;
EXTERN UCHAR *pSiS_SR21;
EXTERN UCHAR *pSiS_SR22;
EXTERN UCHAR *pSiS_SR23;
EXTERN UCHAR *pSiS_SR24;
EXTERN UCHAR *pSiS_SR31;
EXTERN UCHAR *pSiS_SR32;
EXTERN UCHAR *pSiS_SR33;
EXTERN UCHAR *pSiS_CRT2Data_1_2;
EXTERN UCHAR *pSiS_CRT2Data_4_D;
EXTERN UCHAR *pSiS_CRT2Data_4_E;
EXTERN UCHAR *pSiS_CRT2Data_4_10;
EXTERN USHORT *pSiS_RGBSenseData;
EXTERN USHORT *pSiS_VideoSenseData;
EXTERN USHORT *pSiS_YCSenseData;
EXTERN USHORT *pSiS_RGBSenseData2; /*301b*/
EXTERN USHORT *pSiS_VideoSenseData2;
EXTERN USHORT *pSiS_YCSenseData2;

EXTERN UCHAR *SiS_NTSCPhase;
EXTERN UCHAR *SiS_PALPhase;
EXTERN UCHAR *SiS_NTSCPhase2;
EXTERN UCHAR *SiS_PALPhase2;
EXTERN UCHAR *SiS_PALMPhase;
EXTERN UCHAR *SiS_PALNPhase;
EXTERN UCHAR *SiS_PALMPhase2;
EXTERN UCHAR *SiS_PALNPhase2;
EXTERN SiS_LCDDataStruct  *SiS_StLCD1024x768Data;
EXTERN SiS_LCDDataStruct  *SiS_ExtLCD1024x768Data;
EXTERN SiS_LCDDataStruct  *SiS_St2LCD1024x768Data;
EXTERN SiS_LCDDataStruct  *SiS_StLCD1280x1024Data;
EXTERN SiS_LCDDataStruct  *SiS_ExtLCD1280x1024Data;
EXTERN SiS_LCDDataStruct  *SiS_St2LCD1280x1024Data;
EXTERN SiS_LCDDataStruct  *SiS_NoScaleData1024x768;
EXTERN SiS_LCDDataStruct  *SiS_NoScaleData1280x1024;
EXTERN SiS_LCDDataStruct  *SiS_LCD1280x960Data;
EXTERN SiS_TVDataStruct   *SiS_StPALData;
EXTERN SiS_TVDataStruct   *SiS_ExtPALData;
EXTERN SiS_TVDataStruct   *SiS_StNTSCData;
EXTERN SiS_TVDataStruct   *SiS_ExtNTSCData;
EXTERN SiS_TVDataStruct   *SiS_St1HiTVData;
EXTERN SiS_TVDataStruct   *SiS_St2HiTVData;
EXTERN SiS_TVDataStruct   *SiS_ExtHiTVData;
EXTERN UCHAR *SiS_NTSCTiming;
EXTERN UCHAR *SiS_PALTiming;
EXTERN UCHAR *SiS_HiTVExtTiming;
EXTERN UCHAR *SiS_HiTVSt1Timing;
EXTERN UCHAR *SiS_HiTVSt2Timing;
EXTERN UCHAR *SiS_HiTVTextTiming;
EXTERN UCHAR *SiS_HiTVGroup3Data;
EXTERN UCHAR *SiS_HiTVGroup3Simu;
EXTERN UCHAR *SiS_HiTVGroup3Text;

EXTERN SiS_PanelDelayTblStruct *SiS_PanelDelayTbl;
EXTERN SiS_PanelDelayTblStruct *SiS_PanelDelayTblLVDS;
EXTERN SiS_LVDSDataStruct  *SiS_LVDS800x600Data_1;
EXTERN SiS_LVDSDataStruct  *SiS_LVDS800x600Data_2;
EXTERN SiS_LVDSDataStruct  *SiS_LVDS1024x768Data_1;
EXTERN SiS_LVDSDataStruct  *SiS_LVDS1024x768Data_2;
EXTERN SiS_LVDSDataStruct  *SiS_LVDS1280x1024Data_1;
EXTERN SiS_LVDSDataStruct  *SiS_LVDS1280x1024Data_2;
EXTERN SiS_LVDSDataStruct  *SiS_LVDS1280x960Data_1;
EXTERN SiS_LVDSDataStruct  *SiS_LVDS1280x960Data_2;
EXTERN SiS_LVDSDataStruct  *SiS_LVDS1400x1050Data_1;
EXTERN SiS_LVDSDataStruct  *SiS_LVDS1400x1050Data_2;
EXTERN SiS_LVDSDataStruct  *SiS_LVDS1024x600Data_1;
EXTERN SiS_LVDSDataStruct  *SiS_LVDS1024x600Data_2;
EXTERN SiS_LVDSDataStruct  *SiS_LVDS1152x768Data_1;
EXTERN SiS_LVDSDataStruct  *SiS_LVDS1152x768Data_2;
EXTERN SiS_LVDSDataStruct  *SiS_LVDS640x480Data_1;
EXTERN SiS_LVDSDataStruct  *SiS_LVDS320x480Data_1;
EXTERN SiS_LVDSDataStruct  *SiS_LVDSXXXxXXXData_1;
EXTERN SiS_LVDSDataStruct  *SiS_CHTVUNTSCData;
EXTERN SiS_LVDSDataStruct  *SiS_CHTVONTSCData;
EXTERN SiS_LVDSDataStruct  *SiS_CHTVUPALData;
EXTERN SiS_LVDSDataStruct  *SiS_CHTVOPALData;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType00_1;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType01_1;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType02_1;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType03_1;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType04_1;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType05_1;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType06_1;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType07_1;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType08_1;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType09_1;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType0a_1;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType0b_1;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType0c_1;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType0d_1;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType0e_1;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType0f_1;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType00_2;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType01_2;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType02_2;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType03_2;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType04_2;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType05_2;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType06_2;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType07_2;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType08_2;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType09_2;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType0a_2;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType0b_2;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType0c_2;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType0d_2;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType0e_2;
EXTERN SiS_LVDSDesStruct  *SiS_PanelType0f_2;

EXTERN SiS_LVDSDesStruct  *LVDS1024x768Des_1;
EXTERN SiS_LVDSDesStruct  *LVDS1280x1024Des_1;
EXTERN SiS_LVDSDesStruct  *LVDS1280x960Des_1;
EXTERN SiS_LVDSDesStruct  *LVDS1024x768Des_2;
EXTERN SiS_LVDSDesStruct  *LVDS1280x1024Des_2;
EXTERN SiS_LVDSDesStruct  *LVDS1280x960Des_2;

EXTERN SiS_LVDSDesStruct  *SiS_CHTVUNTSCDesData;
EXTERN SiS_LVDSDesStruct  *SiS_CHTVONTSCDesData;
EXTERN SiS_LVDSDesStruct  *SiS_CHTVUPALDesData;
EXTERN SiS_LVDSDesStruct  *SiS_CHTVOPALDesData;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1800x600_1;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11024x768_1;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11280x1024_1;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11400x1050_1;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11024x600_1;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11152x768_1;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1800x600_1_H;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11024x768_1_H;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11280x1024_1_H;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11400x1050_1_H;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11024x600_1_H;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11152x768_1_H;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1800x600_2;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11024x768_2;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11280x1024_2;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11400x1050_2;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11024x600_2;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11152x768_2;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1800x600_2_H;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11024x768_2_H;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11280x1024_2_H;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11400x1050_2_H;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11024x600_2_H;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11152x768_2_H;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1XXXxXXX_1;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1XXXxXXX_1_H;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_CHTVCRT1UNTSC;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_CHTVCRT1ONTSC;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_CHTVCRT1UPAL;
EXTERN SiS_LVDSCRT1DataStruct  *SiS_CHTVCRT1OPAL;

EXTERN SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1320x480_1;

EXTERN SiS_LCDACRT1DataStruct  *SiS_LCDACRT1800x600_1;
EXTERN SiS_LCDACRT1DataStruct  *SiS_LCDACRT11024x768_1;
EXTERN SiS_LCDACRT1DataStruct  *SiS_LCDACRT11280x1024_1;
EXTERN SiS_LCDACRT1DataStruct  *SiS_LCDACRT1800x600_1_H;
EXTERN SiS_LCDACRT1DataStruct  *SiS_LCDACRT11024x768_1_H;
EXTERN SiS_LCDACRT1DataStruct  *SiS_LCDACRT11280x1024_1_H;
EXTERN SiS_LCDACRT1DataStruct  *SiS_LCDACRT1800x600_2;
EXTERN SiS_LCDACRT1DataStruct  *SiS_LCDACRT11024x768_2;
EXTERN SiS_LCDACRT1DataStruct  *SiS_LCDACRT11280x1024_2;
EXTERN SiS_LCDACRT1DataStruct  *SiS_LCDACRT1800x600_2_H;
EXTERN SiS_LCDACRT1DataStruct  *SiS_LCDACRT11024x768_2_H;
EXTERN SiS_LCDACRT1DataStruct  *SiS_LCDACRT11280x1024_2_H;

/* TW: New from 650/301LV BIOS */
EXTERN SiS_Part2PortTblStruct *SiS_CRT2Part2_1024x768_1;
EXTERN SiS_Part2PortTblStruct *SiS_CRT2Part2_1280x1024_1;
EXTERN SiS_Part2PortTblStruct *SiS_CRT2Part2_1024x768_2;
EXTERN SiS_Part2PortTblStruct *SiS_CRT2Part2_1280x1024_2;
EXTERN SiS_Part2PortTblStruct *SiS_CRT2Part2_1024x768_3;
EXTERN SiS_Part2PortTblStruct *SiS_CRT2Part2_1280x1024_3;

EXTERN SiS_CHTVRegDataStruct *SiS_CHTVReg_UNTSC;
EXTERN SiS_CHTVRegDataStruct *SiS_CHTVReg_ONTSC;
EXTERN SiS_CHTVRegDataStruct *SiS_CHTVReg_UPAL;
EXTERN SiS_CHTVRegDataStruct *SiS_CHTVReg_OPAL;
EXTERN UCHAR *SiS_CHTVVCLKUNTSC;
EXTERN UCHAR *SiS_CHTVVCLKONTSC;
EXTERN UCHAR *SiS_CHTVVCLKUPAL;
EXTERN UCHAR *SiS_CHTVVCLKOPAL;



