#if 0
    FLI CCD
    INDI Interface for Finger Lakes Instruments CCDs
    Copyright (C) 2003 Jasem Mutlaq (mutlaqja@ikarustech.com)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "fli/libfli.h"
#include "qfits/qfits.h"
#include "indidevapi.h"
#include "indicom.h"

void ISInit();
int writeFITS(char *filename, char errmsg[]);
int writeRAW (char *filename, char errmsg[]);
int  findcam(flidomain_t domain);
int  setImageArea(char errmsg[]);
int  manageDefaults(char errmsg[]);
int  grabImage();
void ISPoll(void *);
void handleExposure(void *);
void connectCCD(ISState *s);

extern char* me;

#define mydev           "FLI CCD"

#define COMM_GROUP	"Communication"
#define EXPOSE_GROUP	"Expose"
#define IMAGE_GROUP	"Image Settings"
#define OTHER_GROUP	"Other"

#define MAX_CCD_TEMP	45		/* Max CCD temperature */
#define MIN_CCD_TEMP	-55		/* Min CCD temperature */
#define MAX_X_BIN	16.		/* Max Horizontal binning */
#define MAX_Y_BIN	16.		/* Max Vertical binning */
#define MAX_PIXELS	4096		/* Max number of pixels in one dimension */
#define POLLMS		1000		/* Polling time (ms) */
#define TEMP_THRESHOLD  .25		/* Differential temperature threshold (�C)*/
#define NFLUSHES	1		/* Number of times a CCD array is flushed before an exposure */

#define FILENAMESIZ	2048
#define LIBVERSIZ 	1024
#define PREFIXSIZ	64

enum FLIFrames { LIGHT_FRAME = 0, BIAS_FRAME, DARK_FRAME, FLAT_FRAME };


typedef struct {
  flidomain_t domain;
  char *dname;
  char *name;
  char *model;
  long HWRevision;
  long FWRevision;
  double x_pixel_size;
  double y_pixel_size;
  long Array_Area[4];
  long Visible_Area[4];
  int width, height;
  double temperature;
} cam_t;

typedef struct {
int  width;
int  height;
int  frameType;
short  *img;
} img_t;

static flidev_t fli_dev;
static cam_t *FLICam;
static img_t *FLIImg;
static int portSwitchIndex;
static int imagesLeft;
static int imageCount; 

long int Domains[] = { FLIDOMAIN_USB, FLIDOMAIN_SERIAL, FLIDOMAIN_PARALLEL_PORT,  FLIDOMAIN_INET };

/*INDI controls */

/* Connect/Disconnect */
static ISwitch PowerS[]          	= {{"CONNECT" , "Connect" , ISS_OFF},{"DISCONNECT", "Disconnect", ISS_ON}};
static ISwitchVectorProperty PowerSP	= { mydev, "CONNECTION" , "Connection", COMM_GROUP, IP_RW, ISR_1OFMANY, 60, IPS_IDLE, PowerS, NARRAY(PowerS)};

/* Types of Ports */
static ISwitch PortS[]           	= {{"USB", "", ISS_ON}, {"Serial", "", ISS_OFF}, {"Parallel", "", ISS_OFF}, {"INet", "", ISS_OFF}};
static ISwitchVectorProperty PortSP	= { mydev, "Port Type", "", COMM_GROUP, IP_RW, ISR_1OFMANY, 0, IPS_IDLE, PortS, NARRAY(PortS)};

/* Types of Frames */
static ISwitch FrameTypeS[]		= { {"FRAME_LIGHT", "Light", ISS_ON}, {"FRAME_BIAS", "Bias", ISS_OFF}, {"FRAME_DARK", "Dark", ISS_OFF}, {"FRAME_FLAT", "Flat Field", ISS_OFF}};
static ISwitchVectorProperty FrameTypeSP = { mydev, "FRAME_TYPE", "Frame Type", EXPOSE_GROUP, IP_RW, ISR_1OFMANY, 0, IPS_IDLE, FrameTypeS, NARRAY(FrameTypeS)};

/* Images Location */
static IText ImageLocationT[]   	= {{"LOCATION", "Location"}};
static ITextVectorProperty ImageLocationTP = { mydev, "FILE_LOCATION", "Images Location", IMAGE_GROUP, IP_WO, 0, IPS_IDLE, ImageLocationT, NARRAY(ImageLocationT)};

/* Images Prefix */
static IText ImagePrefixT[]      	= {{"PREFIX", "Prefix"}};
static ITextVectorProperty ImagePrefixTP= { mydev, "FILE_PREFIX", "Images Prefix", IMAGE_GROUP, IP_WO, 0, IPS_IDLE, ImagePrefixT, NARRAY(ImagePrefixT)};

/* Images Type */
static ISwitch ImageFormatS[]		= {{ "FITS", "", ISS_ON}, { "RAW", "", ISS_OFF }};
static ISwitchVectorProperty ImageFormatSP= { mydev, "IMAGE_FORMAT", "Image Format", IMAGE_GROUP, IP_RW, ISR_1OFMANY, 0, IPS_IDLE, ImageFormatS, NARRAY(ImageFormatS)};

/* File Name */
static IText FileNameT[]		= {{ "FILE", "File" }};
static ITextVectorProperty FileNameTP	= { mydev, "FILE_NAME", "File name", IMAGE_GROUP, IP_RW, 0, IPS_IDLE, FileNameT, NARRAY(FileNameT)};

/* Frame coordinates. Full frame is default */
static INumber FrameN[]          	= {
 { "X", "", "%.0f", 0.,     MAX_PIXELS, 1., 0.},
 { "Y", "", "%.0f", 0.,     MAX_PIXELS, 1., 0.},
 { "Width", "", "%.0f", 0., MAX_PIXELS, 1., 0.},
 { "Height", "", "%.0f",0., MAX_PIXELS, 1., 0.}};
 static INumberVectorProperty FrameNP = { mydev, "FRAME", "Frame", IMAGE_GROUP, IP_RW, 60, IPS_IDLE, FrameN, NARRAY(FrameN)};
 
 /* Binning */ 
 static INumber BinningN[]       = {
 { "HOR_BIN", "X", "%0.f", 1., MAX_X_BIN, 1., 1.},
 { "VER_BIN", "Y", "%0.f", 1., MAX_Y_BIN, 1., 1.}};
 static INumberVectorProperty BinningNP = { mydev, "BINNING", "Binning", IMAGE_GROUP, IP_RW, 60, IPS_IDLE, BinningN, NARRAY(BinningN)};
 
 /* Exposure time */
  static INumber ExposeTimeN[]    = {{ "EXPOSE_TIME_S", "Duration (s)", "%5.2f", 0., 59., .5, 1.}};
  static INumberVectorProperty ExposeTimeNP = { mydev, "EXPOSE_DURATION", "Expose", EXPOSE_GROUP, IP_RW, 60, IPS_IDLE, ExposeTimeN, NARRAY(ExposeTimeN)};
 
 /* Number of Exposures */
 static INumber NumberOfExpN[]    = {
  { "EXPOSURE_COUNT", "Count", "%.0f", 1. , 0., 0., 1.}};
  static INumberVectorProperty NumberOfExpNP  = { mydev, "NUMBER_OF_EXPOSURES", "# of Exposures", EXPOSE_GROUP, IP_RW, 60, IPS_IDLE, NumberOfExpN, NARRAY(NumberOfExpN)};
  
 /* Delay between exposures */
 static INumber DelayN[]	  = { {"EXPOSURE_DELAY", "Delay", "%0.f", 0., 0., 0., 0.} };
 static INumberVectorProperty DelayNP = { mydev, "DELAY_BETWEEN_EXPOSURES", "Delay (s)", EXPOSE_GROUP, IP_RW, 60, IPS_IDLE, DelayN, NARRAY(DelayN)};
 
 /* Temperature control */
 static INumber TemperatureN[]	  = { {"TEMPERATURE", "Temperature", "%+06.2f", MIN_CCD_TEMP, MAX_CCD_TEMP, .2, 0.}};
 static INumberVectorProperty TemperatureNP = { mydev, "CCD_TEMPERATURE", "Temperature (�C)", EXPOSE_GROUP, IP_RW, 60, IPS_IDLE, TemperatureN, NARRAY(TemperatureN)};
 
 /* Start/Stop expose */
 static ISwitch ExposeS[] 	  = { {"EXPOSE_ON", "Start", ISS_OFF}, {"EXPOSE_OFF", "Cancel", ISS_OFF} };
 static ISwitchVectorProperty ExposeSP = { mydev, "EXPOSE", "Expose", EXPOSE_GROUP, IP_RW, ISR_1OFMANY, 0, IPS_IDLE, ExposeS, NARRAY(ExposeS) };
 
 /* Expose progress */
 static INumber ExposeProgressN[] = { {"Time left", "", "%.0f", 0., 0., 0., 0.} };
 static INumberVectorProperty ExposeProgressNP  = { mydev, "Expose Progress (s)", "", EXPOSE_GROUP, IP_RO, 0, IPS_IDLE, ExposeProgressN, NARRAY(ExposeProgressN)};
  
/* Hardware revision */
static INumber HWRevisionN[] 	= { { "Revision", "", "%.0f", 0., 0., 0., 0. }};
static INumberVectorProperty HWRevisionNP = { mydev, "Hardware Revision", "", OTHER_GROUP, IP_RO, 0, IPS_IDLE, HWRevisionN, NARRAY(HWRevisionN)};

/* Firmware revision */
static INumber FWRevisionN[] 	= { { "Revision", "", "%.0f", 0., 0., 0., 0. }};
static INumberVectorProperty FWRevisionNP = { mydev, "Firmware Revision", "", OTHER_GROUP, IP_RO, 0, IPS_IDLE, FWRevisionN, NARRAY(FWRevisionN)};

/* Pixel size (�m) */
static INumber PixelSizeN[] 	= {
	{ "Width", "", "%.0f", 0. , 0., 0., 0.},
	{ "Height", "", "%.0f", 0. , 0., 0., 0.}};
static INumberVectorProperty PixelSizeNP = { mydev, "Pixel Size (�m)", "", OTHER_GROUP, IP_RO, 0, IPS_IDLE, PixelSizeN, NARRAY(PixelSizeN)};
 

/* send client definitions of all properties */
void ISInit()
{
  static int isInit=0;

 if (isInit)
  return;
 
 /* USB by default {USB, SERIAL, PARALLEL, INET} */
 portSwitchIndex = 0;
 
 FLIImg = malloc (sizeof(img_t));
 
 if (FLIImg == NULL)
 {
   IDMessage(mydev, "Error: unable to initialize driver. Low memory.");
   IDLog("Error: unable to initialize driver. Low memory.");
   return;
 }
 
 ImageLocationT[0].text = strcpy(malloc (sizeof (char) * (FILENAMESIZ - PREFIXSIZ)), "");
 
 ImagePrefixT[0].text   = strcpy(malloc (sizeof (char) * PREFIXSIZ), "image_");
 
 FileNameT[0].text = strcpy(malloc (sizeof (char) * FILENAMESIZ), "image1.fits");
 
 imageCount = 0;
 
 isInit = 1;
 

}

void ISGetProperties (const char *dev)
{ 

   ISInit();
  
  if (dev && strcmp (mydev, dev))
    return;

  /* COMM_GROUP */
  IDDefSwitch(&PowerSP, NULL);
  IDDefSwitch(&PortSP, NULL);
  
  /* Expose */
  /*IDDefSwitch(&ExposeSP, NULL);
  IDDefNumber(&ExposeProgressNP, NULL);*/
  IDDefSwitch(&FrameTypeSP, NULL);  
  IDDefNumber(&ExposeTimeNP, NULL);
  
  /*IDDefNumber(&NumberOfExpNP, NULL);
  IDDefNumber(&DelayNP, NULL);*/
  IDDefNumber(&TemperatureNP, NULL);
  
  /* Image Group */
  /* Note: We're going to handle looping from the client
     level instead of the driver level 
  IDDefText(&ImageLocationTP, NULL);
  IDDefText(&ImagePrefixTP, NULL);
  */
  IDDefSwitch(&ImageFormatSP, NULL);
  IDDefText(&FileNameTP, NULL);
  IDDefNumber(&FrameNP, NULL);
  IDDefNumber(&BinningNP, NULL);
  
  /* Other Group 
  IDDefNumber(&HWRevisionNP, NULL);
  IDDefNumber(&FWRevisionNP, NULL);
  IDDefNumber(&PixelSizeNP, NULL);*/
  
  IEAddTimer (POLLMS, ISPoll, NULL);
  
}
  
void ISNewSwitch (const char *dev, const char *name, ISState *states, char *names[], int n)
{
        char errmsg[2048];
	long err;
	int i;
	ISwitch *sp;
	
	/* ignore if not ours */
	if (dev && strcmp (dev, mydev))
	    return;
	    
	ISInit();
	    
	/* Port type */
	if (!strcmp (name, PortSP.name))
	{
   	  PortSP.s = IPS_IDLE; 
	  portSwitchIndex = getOnSwitch(states, n);
	  
	  if (portSwitchIndex == -1)
	  {
	    IDSetSwitch(&PortSP, "Unknown error. All switches are off.");
	    IDLog("Unknown error. All switches are off.");
	    return;
	  }
	  
	  for (i=0 ; i < n ; i++)
	  {
	    sp = IUFindSwitch(&PortSP, names[i]);
	    
	    if (!sp)
	    {
	      IDSetSwitch(&PortSP, "Unknown error. %s is not a member of %s property.", names[0], name);
	      return;
	    }
	    
	    sp->s = states[i];
	  }
	  
	  PortSP.s = IPS_OK; 
	  IDSetSwitch(&PortSP, NULL);
	  return;
	}
	
	/* Connection */
	if (!strcmp (name, PowerSP.name))
	{
   	  connectCCD(states);
	  return;
	}
	
	/* Expose */
	if (!strcmp (name, ExposeSP.name))
	{
   	  if (checkPowerS(&ExposeSP))
	    return;
	    
	  IUResetSwitches(&ExposeSP); 
	  
	  imagesLeft = (int) NumberOfExpN[0].value;
	  
	  for (i=0; i < n ; i++)
	  {
	    sp = IUFindSwitch(&ExposeSP, names[i]);
	    
	    if (!sp)
	    {
	      IDSetSwitch(&ExposeSP, "Unknown error. %s is not a member of %s property.", names[0], name);
	      return;
	    }
	    
	  if ((sp == &ExposeS[0]) &&  (states[i] == ISS_ON))
	  {
	    imageCount = 0;
	    handleExposure(NULL);
	    return;
	  }
	  else if ((sp == &ExposeS[1]) && (states[i] == ISS_ON))
	  {
	     if ( (err = FLICancelExposure(fli_dev)))
	  	{
	    	ExposeSP.s = IPS_IDLE;
	    	IDSetSwitch(&ExposeSP, "FLICancelExposure() failed. %s.", strerror((int)-err));
	    	IDLog("FLICancelExposure() failed. %s.\n", strerror((int)-err));
	    	return;
	  	}
	
	  ExposeSP.s = IPS_IDLE;
	  ExposeProgressNP.s = IPS_IDLE;
	  ExposeProgressN[0].value = imageCount = 0;
	  
	  IDSetSwitch(&ExposeSP, "Exposure cancelled.");
	  IDSetNumber(&ExposeProgressNP, NULL);
	  IDLog("Exposure Cancelled.\n");
	  return;
	 }
	 else
	 {
	  ExposeSP.s = IPS_IDLE;
	  IDSetSwitch(&ExposeSP, "Unknown error.");
	 }
	
       } /* end for loop */
     }
     
     /* Frame Type */
     if (!strcmp(FrameTypeSP.name, name))
     {
       if (checkPowerS(&FrameTypeSP))
         return;
	 
       FrameTypeSP.s = IPS_IDLE;
	 
       for (i = 0; i < n ; i++)
       {
         sp = IUFindSwitch(&FrameTypeSP, names[i]);
	 
	 if (!sp)
	 {
	      IDSetSwitch(&FrameTypeSP, "Unknown error. %s is not a member of %s property.", names[0], name);
	      return;
	 }
	 
	 /* NORMAL, BIAS, or FLAT */
	 if ( (sp == &FrameTypeS[LIGHT_FRAME] || sp == &FrameTypeS[BIAS_FRAME] 
	    || sp == &FrameTypeS[FLAT_FRAME]) && states[i] == ISS_ON)
	 {
	   if (sp == &FrameTypeS[LIGHT_FRAME])
	     FLIImg->frameType = LIGHT_FRAME;
	   else if (sp == &FrameTypeS[BIAS_FRAME])
	     FLIImg->frameType = BIAS_FRAME;
	   else
	     FLIImg->frameType = FLAT_FRAME;
	   
	   if ((err = FLISetFrameType(fli_dev, FLI_FRAME_TYPE_NORMAL) ))
  	   {
	    IUResetSwitches(&FrameTypeSP);
	    FrameTypeS[LIGHT_FRAME].s = ISS_ON;
            IDSetSwitch(&FrameTypeSP, "FLISetFrameType() failed. %s.\n", strerror((int)-err));
	    IDLog("FLISetFrameType() failed. %s.\n", strerror((int)-err));
	    return;
	   }
	  
	   IUResetSwitches(&FrameTypeSP);
	   sp->s = ISS_ON; 
	   FrameTypeSP.s = IPS_OK;
	   IDSetSwitch(&FrameTypeSP, NULL);
	   break;
	 }
	 /* DARK */
	 else if (sp == &FrameTypeS[DARK_FRAME] && states[i] == ISS_ON)
	 {
	   FLIImg->frameType = DARK_FRAME;
	   
	   if ((err = FLISetFrameType(fli_dev, FLI_FRAME_TYPE_DARK) ))
  	   {
	    IUResetSwitches(&FrameTypeSP);
	    FrameTypeS[LIGHT_FRAME].s = ISS_ON;
            IDSetSwitch(&FrameTypeSP, "FLISetFrameType() failed. %s.\n", strerror((int)-err));
	    IDLog("FLISetFrameType() failed. %s.\n", strerror((int)-err));
	    return;
	   }
	   
	   IUResetSwitches(&FrameTypeSP);
	   sp->s = ISS_ON;
	   FrameTypeSP.s = IPS_OK;
	   IDSetSwitch(&FrameTypeSP, NULL);
	   break;
	 }
	   
  	} /* For loop */
	
	return;
     }
     
     if (!strcmp(name, ImageFormatSP.name))
     {
       	IUUpdateSwitches(&ImageFormatSP, states, names, n);
	ImageFormatSP.s = IPS_OK;
	IDSetSwitch(&ImageFormatSP, NULL);
	return;
     }
}

void ISNewText (const char *dev, const char *name, char *texts[], char *names[], int n)
{
	IText *tp;
	int i;

	ISInit();
 
       /* ignore if not ours */ 
       if (dev && strcmp (mydev, dev))
         return;

	/* suppress warning */
	n=n;
	
	/* Image location */
	if (!strcmp(ImageLocationTP.name, name))
	{
	  tp = IUFindText(&ImageLocationTP, names[0]);
	  ImageLocationTP.s = IPS_IDLE;
	  
	  if (!tp)
	  {
	    IDSetText(&ImageLocationTP, "Error: %s is not a member of %s property.", names[0], name);
	    return;
	  }
	  
	  if ( strlen(texts[0]) > (FILENAMESIZ - PREFIXSIZ))
	  {
	    IDSetText(&ImageLocationTP, "Location is too long.");
	    return;
	  }
	  
	  strcpy(tp->text, texts[0]);
  
	  /* strip trailing / */
	  for (i = strlen(tp->text) - 1 ; i > 0; i--)
	  {
	    if (tp->text[i] == '/')
	      tp->text[i] = '\0';
	    else break;
	  }
	  
	  IDLog("Image Location is: %s\n", tp->text);
	  ImageLocationTP.s = IPS_OK;
	  IDSetText(&ImageLocationTP, NULL);
	  return;
	}
	
	/* Image Prefix */
	if (!strcmp(ImagePrefixTP.name, name))
	{
	  tp = IUFindText(&ImagePrefixTP, names[0]);
	  ImagePrefixTP.s = IPS_IDLE;
	  
	  if (!tp)
	  {
	    IDSetText(&ImagePrefixTP, "Error: %s is not a member of %s property.", names[0], name);
	    IDLog("Error: %s is not a member of %s property.", names[0], name);
	    return;
	  }
	  
	  if ( strlen(texts[0]) > PREFIXSIZ)
	  {
	    IDSetText(&ImagePrefixTP, "Prefix is too long.");
	    IDLog("Prefix is too long.");
	    return;
	  }
	   
	  strcpy(tp->text, texts[0]);
	  
	  IDLog("Image Prefix is: %s\n", tp->text);
	  
	  ImagePrefixTP.s = IPS_OK;
	  IDSetText(&ImagePrefixTP, NULL);
	  return;
	}
	
	if (!strcmp, FileNameTP.name)
	{
	  tp = IUFindText(&FileNameTP, names[0]);
	  FileNameTP.s = IPS_IDLE;
	  
	  if (!tp)
	  {
	    IDSetText(&ImagePrefixTP, "Error: %s is not a member of %s property.", names[0], name);
	    IDLog("Error: %s is not a member of %s property.", names[0], name);
	    return;
	  }
	  
	  strcpy(tp->text, texts[0]);
	  FileNameTP.s = IPS_OK;
	  IDSetText(&FileNameTP, NULL);
	  return;
	  
	}
	  
	      	
}


void ISNewNumber (const char *dev, const char *name, double values[], char *names[], int n)
{
      long err;
      int i;
      INumber *np;
      char errmsg[1024];

	/* ignore if not ours */
	if (dev && strcmp (dev, mydev))
	    return;
	    
	ISInit();
	
    /* Exposure time */
    if (!strcmp (ExposeTimeNP.name, name))
    {
       
       if (checkPowerN(&ExposeTimeNP))
         return;

       if (ExposeTimeNP.s == IPS_BUSY)
       {
          if ( (err = FLICancelExposure(fli_dev)))
	  {
	    	ExposeTimeNP.s = IPS_IDLE;
	    	IDSetNumber(&ExposeTimeNP, "FLICancelExposure() failed. %s.", strerror((int)-err));
	    	IDLog("FLICancelExposure() failed. %s.\n", strerror((int)-err));
	    	return;
	  }
	
	  ExposeTimeNP.s = IPS_IDLE;
	  ExposeTimeN[0].value = 0;

	  IDSetNumber(&ExposeTimeNP, "Exposure cancelled.");
	  IDLog("Exposure Cancelled.\n");
	  return;
        }
    
       ExposeTimeNP.s = IPS_IDLE;
       
       IDLog("Trying to find a member\n");
       np = IUFindNumber(&ExposeTimeNP, names[0]);
	 
       if (!np)
       {
	   IDSetNumber(&ExposeTimeNP, "Error: %s is not a member of %s property.", names[0], name);
	   return;
        }
	 
        np->value = values[0];
	
      /* Set duration */  
     if ( (err = FLISetExposureTime(fli_dev, np->value * 1000.) ))
     {
       IDSetNumber(&ExposeTimeNP, "FLISetExposureTime() failed. %s.\n", strerror((int)-err));
       IDLog("FLISetExposureTime() failed. %s.\n", strerror((int)-err));
       return;
     }
      
      IDLog("Exposure Time (ms) is: %g\n", np->value * 1000.);
      
      handleExposure(NULL);
      return;
    } 
  
  if (!strcmp(NumberOfExpNP.name, name))
  {
    NumberOfExpNP.s = IPS_IDLE;
    
    np = IUFindNumber(&NumberOfExpNP, names[0]);
    
    if (!np)
    {
    IDSetNumber(&NumberOfExpNP, "Unknown error. %s is not a member of %s property.", names[0], name);
    return;
    }
    
    if (values[0] < 1)
    {
      IDSetNumber(&NumberOfExpNP, "The number of exposures must be a positive integer.");
      return;
    }
    
    np->value = values[0];
    
    NumberOfExpNP.s = IPS_OK;
    
    IDLog("Number of Exposures is: %02f\n", np->value);
    
    IDSetNumber(&NumberOfExpNP, NULL);
    return;
  }

  if (!strcmp (DelayNP.name, name))
  {
    DelayNP.s = IPS_IDLE;
    
    np = IUFindNumber(&DelayNP, names[0]);
    
    if (!np)
    {
    IDSetNumber(&DelayNP, "Unknown error. %s is not a member of %s property.", names[0], name);
    return;
    }
    
    if (values[0] < 0)
    {
      IDSetNumber(&DelayNP, "The delay must be greater than zero.");
      return;
    }
    
    np->value = values[0];
    
    IDLog("Delay is: %02f\n", np->value);
    
    DelayNP.s = IPS_OK;
    
    IDSetNumber(&DelayNP, NULL);
    return;
  }
    
    
  if (!strcmp(TemperatureNP.name, name))
  {
    if (checkPowerN(&TemperatureNP))
      return;
      
    TemperatureNP.s = IPS_IDLE;
    
    np = IUFindNumber(&TemperatureNP, names[0]);
    
    if (!np)
    {
    IDSetNumber(&TemperatureNP, "Unknown error. %s is not a member of %s property.", names[0], name);
    return;
    }
    
    if (values[0] < MIN_CCD_TEMP || values[0] > MAX_CCD_TEMP)
    {
      IDSetNumber(&TemperatureNP, "Error: valid range of temperature is from %d to %d", MIN_CCD_TEMP, MAX_CCD_TEMP);
      return;
    }
    
    if ( (err = FLISetTemperature(fli_dev, values[0])))
    {
      IDSetNumber(&TemperatureNP, "FLISetTemperature() failed. %s.", strerror((int)-err));
      IDLog("FLISetTemperature() failed. %s.", strerror((int)-err));
      return;
    }
    
    FLICam->temperature = values[0];
    TemperatureNP.s = IPS_BUSY;
    
    IDSetNumber(&TemperatureNP, "Setting CCD temperature to %+06.2f �C", values[0]);
    IDLog("Setting CCD temperature to %+06.2f �C\n", values[0]);
    return;
   }
   
   if (!strcmp(FrameNP.name, name))
   {
     int nset=0;
     
     if (checkPowerN(&FrameNP))
      return;
      
     FrameNP.s = IPS_IDLE;
     
     for (i=0; i < n ; i++)
     {
       np = IUFindNumber(&FrameNP, names[i]);
       
      if (!np)
      {
       IDSetNumber(&FrameNP, "Unknown error. %s is not a member of %s property.", names[0], name);
       return;
      }
      
      /* X or Width */
      if (np == &FrameN[0] || np==&FrameN[2])
      {
        if (values[i] < 0 || values[i] > FLICam->width)
	 break;
	 
	nset++;
	np->value = values[i];
      }
      /* Y or height */
      else if (np == &FrameN[1] || np==&FrameN[3])
      {
       if (values[i] < 0 || values[i] > FLICam->height)
	 break;
	 
	nset++;
	np->value = values[i];
      }
     }
      
      if (nset < 4)
      {
        IDSetNumber(&FrameNP, "Invalid range. Valid range is (0,0) - (%0.f,%0.f)", FLICam->width, FLICam->height);
	IDLog("Invalid range. Valid range is (0,0) - (%0.f,%0.f)", FLICam->width, FLICam->height);
	return; 
      }
      
      if (setImageArea(errmsg))
      {
        IDSetNumber(&FrameNP, errmsg);
	return;
      }
      	    
      FrameNP.s = IPS_OK;
      
      /* Adjusting image width and height */ 
      FLIImg->width  = FrameN[2].value;
      FLIImg->height = FrameN[3].value;
      
      IDSetNumber(&FrameNP, NULL);
      
   } /* end FrameNP */
      
    
   if (!strcmp(BinningNP.name, name))
   {
     if (checkPowerN(&BinningNP))
       return;
       
     BinningNP.s = IPS_IDLE;
     
     for (i=0 ; i < n ; i++)
     {
       np = IUFindNumber(&BinningNP, names[i]);
       
      if (!np)
      {
       IDSetNumber(&BinningNP, "Unknown error. %s is not a member of %s property.", names[0], name);
       return;
      }
      
      /* X binning */
      if (np == &BinningN[0])
      {
        if (values[i] < 1 || values[i] > MAX_X_BIN)
	{
	  IDSetNumber(&BinningNP, "Error: Valid X bin values are from 1 to %d", MAX_X_BIN);
	  IDLog("Error: Valid X bin values are from 1 to %d", MAX_X_BIN);
	  return;
	}
	
	if ( (err = FLISetHBin(fli_dev, values[i])))
	{
	  IDSetNumber(&BinningNP, "FLISetHBin() failed. %s.", strerror((int)-err));
	  IDLog("FLISetHBin() failed. %s.", strerror((int)-err));
	  return;
	}
	
	np->value = values[i];
      }
      else if (np == &BinningN[1])
      {
        if (values[i] < 1 || values[i] > MAX_Y_BIN)
	{
	  IDSetNumber(&BinningNP, "Error: Valid Y bin values are from 1 to %d", MAX_Y_BIN);
	  IDLog("Error: Valid X bin values are from 1 to %d", MAX_Y_BIN);
	  return;
	}
	
	if ( (err = FLISetVBin(fli_dev, values[i])))
	{
	  IDSetNumber(&BinningNP, "FLISetVBin() failed. %s.", strerror((int)-err));
	  IDLog("FLISetVBin() failed. %s.", strerror((int)-err));
	  return;
	}
	
	np->value = values[i];
      }
     } /* end for */
     
     if (setImageArea(errmsg))
     {
       IDSetNumber(&BinningNP, errmsg, NULL);
       IDLog(errmsg);
       return;
     }
     
     BinningNP.s = IPS_OK;
     
     IDLog("Binning is: %.0f x %.0f\n", BinningN[0].value, BinningN[1].value);
     
     IDSetNumber(&BinningNP, NULL);
     return;
   }
	
}


void ISPoll(void *p)
{
	long err;
	long timeleft;
	double ccdTemp;
	
	if (!isCCDConnected())
	{
	 IEAddTimer (POLLMS, ISPoll, NULL);
	 return;
	}
	 
        /*IDLog("In Poll.\n");*/
	
	switch (ExposeTimeNP.s)
	{
	  case IPS_IDLE:
	    break;
	    
	  case IPS_OK:
	    break;
	    
	  case IPS_BUSY:
	    if ( (err = FLIGetExposureStatus(fli_dev, &timeleft)))
	    { 
	      ExposeTimeNP.s = IPS_IDLE; 
	      ExposeTimeN[0].value = 0;
	      
	      IDSetNumber(&ExposeTimeNP, "FLIGetExposureStatus() failed. %s.", strerror((int)-err));
	      IDLog("FLIGetExposureStatus() failed. %s.\n", strerror((int)-err));
	      break;
	    }
	    
	    /*ExposeProgressN[0].value = (timeleft / 1000.);*/
	    
	    if (timeleft > 0)
	    {
	      ExposeTimeN[0].value = timeleft / 1000.;
	      IDSetNumber(&ExposeTimeNP, NULL); 
	      break;
	    }
	    /*{
	      IDSetNumber(&ExposeProgressNP, NULL);
	      break;
	    }*/
	    
	    /* We're done exposing */
	    ExposeTimeNP.s = IPS_OK; 
	    ExposeTimeN[0].value = 0;
	    /*ExposeProgressNP.s = IPS_IDLE;*/
	    IDSetNumber(&ExposeTimeNP, "Exposure done, downloading image...");
	    IDLog("Exposure done, downloading image...\n");
	    /*IDSetNumber(&ExposeProgressNP, NULL);*/
	    
	    /* grab and save image */
	     if (grabImage())
	      break;
	    
	    /* Multiple image exposure 
	    if ( imagesLeft > 0)
	    { 
	      IDMessage(mydev, "Image #%d will be taken in %0.f seconds.", imageCount+1, DelayN[0].value);
	      IDLog("Image #%d will be taken in %0.f seconds.", imageCount+1, DelayN[0].value);
	      IEAddTimer (DelayN[0].value * 1000., handleExposure, NULL);
	    }*/
	    break;
	    
	  case IPS_ALERT:
	    break;
	 }
	 
	 switch (ExposeProgressNP.s)
	 {
	   case IPS_IDLE:
	   case IPS_OK:
	   	break;
           case IPS_BUSY:
	      ExposeProgressN[0].value--;
	      
	      if (ExposeProgressN[0].value > 0)
	          IDSetNumber(&ExposeProgressNP, NULL);
	      else
	      {
	        ExposeProgressNP.s = IPS_IDLE;
		IDSetNumber(&ExposeProgressNP, NULL);
	      }
	      break;
	      
	  case IPS_ALERT:
	     break;
	  }
		
	 
	 switch (TemperatureNP.s)
	 {
	   case IPS_IDLE:
	   case IPS_OK:
	     if ( (err = FLIGetTemperature(fli_dev, &ccdTemp)))
	     {
	       TemperatureNP.s = IPS_IDLE;
	       IDSetNumber(&TemperatureNP, "FLIGetTemperature() failed. %s.", strerror((int)-err));
	       IDLog("FLIGetTemperature() failed. %s.", strerror((int)-err));
	       return;
	     }
	     
	     if (fabs(TemperatureN[0].value - ccdTemp) >= TEMP_THRESHOLD)
	     {
	       TemperatureN[0].value = ccdTemp;
	       IDSetNumber(&TemperatureNP, NULL);
	     }
	     break;
	     
	   case IPS_BUSY:
	   if ((err = FLIGetTemperature(fli_dev, &ccdTemp)))
	     {
	       TemperatureNP.s = IPS_ALERT;
	       IDSetNumber(&TemperatureNP, "FLIGetTemperature() failed. %s.", strerror((int)-err));
	       IDLog("FLIGetTemperature() failed. %s.", strerror((int)-err));
	       return;
	     }
	     
	     if (fabs(FLICam->temperature - ccdTemp) <= TEMP_THRESHOLD)
	       TemperatureNP.s = IPS_OK;
	     	       
              TemperatureN[0].value = ccdTemp;
	      IDSetNumber(&TemperatureNP, NULL);
	      break;
	      
	    case IPS_ALERT:
	     break;
	  }
  	 
	 p=p; 
 	
	 IEAddTimer (POLLMS, ISPoll, NULL);
}

/* Sets the Image area that the CCD will scan and download.
   We compensate for binning. */
int setImageArea(char errmsg[])
{
  
   long x_1, y_1, x_2, y_2;
   long err;
   
   /* Add the X and Y offsets */
   x_1 = FrameN[0].value + FLICam->Visible_Area[0];
   y_1 = FrameN[1].value + FLICam->Visible_Area[1];
   
   x_2 = x_1 + (FrameN[2].value / BinningN[0].value);
   y_2 = y_1 + (FrameN[3].value / BinningN[1].value);
   
   if (x_2 > FLICam->Visible_Area[2])
     x_2 = FLICam->Visible_Area[2];
   
   if (y_2 > FLICam->Visible_Area[3])
     y_2 = FLICam->Visible_Area[3];
     
   IDLog("The Final image area is (%ld, %ld), (%ld, %ld)\n", x_1, y_1, x_2, y_2);
   
   FLIImg->width  = x_2 - x_1;
   FLIImg->height = y_2 - y_1;
   
   if ( (err = FLISetImageArea(fli_dev, x_1, y_1, x_2, y_2) ))
   {
     sprintf(errmsg, "FLISetImageArea() failed. %s.\n", strerror((int)-err));
     IDLog(errmsg, NULL);
     return -1;
   }
   
   return 0;
}

/* Downloads the image from the CCD row by row and store them
   in a raw file.
 N.B. No processing is done on the image */
int grabImage()
{
  long err;
  int img_size,i;
  static int globalImageCount = 0;
  char filename[FILENAMESIZ];
  char errmsg[1024];
  
  img_size = FLIImg->width * FLIImg->height * sizeof(short);
  
  FLIImg->img = malloc (img_size);
  
  if (FLIImg->img == NULL)
  {
    IDMessage(mydev, "Not enough memory to store image.", NULL);
    IDLog("Not enough memory to store image.\n", NULL);
    return -1;
  }
  
  for (i=0; i < FLIImg->width ; i++)
  {
    if ( (err = FLIGrabRow(fli_dev, &FLIImg->img[i * FLIImg->width], FLIImg->width)))
    {
       free(FLIImg->img);
       IDMessage(mydev, "FLIGrabRow() failed at row %d. %s.", i, strerror((int)-err));
       IDLog("FLIGrabRow() failed at row %d. %s.\n", i, strerror((int)-err));
       return -1;
    }
  }
  
   err = (ImageFormatS[0].s == ISS_ON) ? writeFITS(FileNameT[0].text, errmsg) : writeRAW(FileNameT[0].text, errmsg);
   if (err)
   {
       free(FLIImg->img);
       IDMessage(mydev, errmsg, NULL);
       return -1;
   }
   
  free(FLIImg->img);
  return 0;
 
}

int writeRAW (char *filename, char errmsg[])
{

int fd, i, img_size;

img_size = FLIImg->width * FLIImg->height * sizeof(short);

/*sprintf(filename, "%s.raw", filename);*/

if ((fd = open(filename, O_WRONLY | O_CREAT  | O_EXCL,
		   S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH )) == -1)
   {
      IDMessage(mydev, "Error: open of %s failed.", filename);
      IDLog("Error: open of '%s' failed.\n", filename);
      return -1;
   }
   
 if (write(fd, FLIImg->img, img_size) != img_size)
 {
      IDMessage(mydev, "Error: write to %s failed.", filename);
      IDLog("Error: write to '%s' failed.\n", filename);
      return -1;
 }
   
 close(fd);
   
 /* Success */
 IDMessage(mydev, "Raw image written to %s", filename);
 IDLog("Raw image written to '%s'\n", filename);

 return 0;
 
}

int writeFITS(char *filename, char errmsg[])
{
  FILE * fits_file;
  qfits_header* fits_header;
  qfitsdumper qdump;
  char width_s[8], height_s[8], temp_s[8], frame_s[16], binning_s[8];
  int *buf;
  int i;
  
  /*IDLog("Some stats: Width: %d - Height: %d - Intensity[100]: %d\n", FLIImg->width, FLIImg->height, FLIImg->img[100]);*/
  
  fits_file = fopen(filename, "w");
  
  qdump.npix		= FLIImg->width * FLIImg->height;
  qdump.ptype		= PTYPE_INT;
  
  /* Copy 2 byte buffer to 4 byte buffer */
  
  if ( (buf = malloc (FLIImg->width * FLIImg->height * sizeof( int) )) == NULL)
  {
    fprintf(stderr, "Error: malloc() failed. Not enough memory.");
    return -1;
  }
  
  for (i=0; i < (FLIImg->height * FLIImg->width); i++)
       buf[i] = FLIImg->img[i];
       
  qdump.ibuf		= buf;
  qdump.out_ptype	= BPP_16_SIGNED;
  
  if ( (qdump.filename = malloc(sizeof(char) * (strlen(filename) + 1))) == NULL)
  {
    free(buf);
    fprintf(stderr, "Error: malloc() failed. Not enough memory.");
    return -1;
  }
  
  strcpy(qdump.filename, filename);
  
  if ( (fits_file = fopen(qdump.filename, "w")) == NULL)
  {
    free(buf);
    fprintf(stderr, "Error: failed to open file %s.", qdump.filename);
    return -1;
  }
  
  if ( (fits_header = qfits_header_default()) == NULL)
  {
    free(buf);
    fprintf(stderr, "Error: failed allocate FITS header.");
    return -1;
  }
  
  sprintf(width_s, "%d", FLIImg->width);
  sprintf(height_s, "%d", FLIImg->height);
  sprintf(temp_s, "%g", TemperatureN[0].value);
  sprintf(binning_s, "(%g x %g)", BinningN[0].value, BinningN[0].value);
  switch (FLIImg->frameType)
  {
    case LIGHT_FRAME:
      	strcpy(frame_s, "Light");
	break;
    case BIAS_FRAME:
        strcpy(frame_s, "Bias");
	break;
    case FLAT_FRAME:
        strcpy(frame_s, "Flat Field");
	break;
    case DARK_FRAME:
        strcpy(frame_s, "Dark");
	break;
  }
  
    
  
  /* Obligatory FITS files */
  qfits_header_add(fits_header, "NAXIS2", width_s, "NAXIS2 comment", NULL);
  qfits_header_add(fits_header, "NAXIS1", height_s, "NAXIS1 comment", NULL);
  qfits_header_add(fits_header, "NAXIS",  "2", "NAXIS comment", NULL);
  qfits_header_add(fits_header, "BITPIX",  "16", "BITPIX comment", NULL);
  qfits_header_add(fits_header, "DATE-OBS", get_datetime_iso8601(), "Date of Observation", NULL);
  qfits_header_add(fits_header, "TEMPERATURE", temp_s, "Temperature", NULL);
  qfits_header_add(fits_header, "BINNING", binning_s, "Binning (x,y)", NULL);
  qfits_header_add(fits_header, "FRAME", frame_s, "Frame Type", NULL);
  
  IDLog("Dumping header....\n");
  if ( qfits_header_dump(fits_header, fits_file) !=0 )
  {
    free(buf);
    fprintf(stderr, "Error: dumping FITS header to file failed.");
    qfits_header_destroy(fits_header);
    return -1;
  }
   
  fclose(fits_file);

  /*int *buf = malloc (10000 * sizeof(int));
       for (i=0; i < 10000 ; i++)
            buf[i] = i;*/
            
  IDLog("Dumping pixel information...\n");
  
  if (qfits_pixdump(&qdump)!=0)
  {
    free(buf);
    fprintf(stderr, "Error: qfits_pixdump() failed.");
    qfits_header_destroy(fits_header);
    return -1;
  }
    
  qfits_header_destroy(fits_header);
  free(buf);
  
  /* Success */
 IDMessage(mydev, "FITS image written to %s", filename);
 IDLog("FITS image written to '%s'\n", filename);
  
  return 0;

}

/* Initiates the exposure procedure */
void handleExposure(void *p)
{
  long err;
  
  /* no warning */
  p=p;
  
  /* BIAS frame opens the shutter for a minimum period of time as allowed by the shutter 
  */
  if (FLIImg->frameType == BIAS_FRAME)
  {
     if ((err = FLISetExposureTime(fli_dev, 50)))
     {
       ExposeTimeNP.s = IPS_IDLE;
       IDSetNumber(&ExposeTimeNP, "FLISetExposureTime() failed. %s.\n", strerror((int)-err));
       IDLog("FLISetExposureTime() failed. %s.\n", strerror((int)-err));
       return;
     }
   }
    
  if ((err = FLIExposeFrame(fli_dev)))
  {
    	ExposeTimeNP.s = IPS_IDLE;
    	IDSetNumber(&ExposeTimeNP, "FLIExposeFrame() failed. %s.", strerror((int)-err));
    	IDLog("FLIExposeFrame() failed. %s.\n", strerror((int)-err));
    	return;
  }
 	  
   ExposeTimeNP.s = IPS_BUSY;
		  
   IDSetNumber(&ExposeTimeNP, "Taking a %g seconds frame...", ExposeTimeN[0].value);
   IDLog("Taking a frame...\n");
}

/* Retrieves basic data from the CCD upon connection like temperature, array size, firmware..etc */
void getBasicData()
{

  char buff[2048];
  long err;
  
  IDLog("In getBasicData()\n");
  
  if ((err = FLIGetModel (fli_dev, buff, 2048)))
  {
    IDMessage(mydev, "FLIGetModel() failed. %s.", strerror((int)-err));
    IDLog("FLIGetModel() failed. %s.\n", strerror((int)-err));
    return;
  }
  else
  {
    if ( (FLICam->model = malloc (sizeof(char) * 2048)) == NULL)
    {
      IDMessage(mydev, "malloc() failed.");
      IDLog("malloc() failed.");
      return;
    }
    
    strcpy(FLICam->model, buff);
  }
  
  if (( err = FLIGetHWRevision(fli_dev, &FLICam->HWRevision)))
  {
    IDMessage(mydev, "FLIGetHWRevision() failed. %s.", strerror((int)-err));
    IDLog("FLIGetHWRevision() failed. %s.\n", strerror((int)-err));
    
    return;
  }
  
  if (( err = FLIGetFWRevision(fli_dev, &FLICam->FWRevision)))
  {
    IDMessage(mydev, "FLIGetFWRevision() failed. %s.", strerror((int)-err));
    IDLog("FLIGetFWRevision() failed. %s.\n", strerror((int)-err));
    return;
  }

  if (( err = FLIGetPixelSize(fli_dev, &FLICam->x_pixel_size, &FLICam->y_pixel_size)))
  {
    IDMessage(mydev, "FLIGetPixelSize() failed. %s.", strerror((int)-err));
    IDLog("FLIGetPixelSize() failed. %s.\n", strerror((int)-err));
    return;
  }
  
  FLICam->x_pixel_size *= 1e6;
  FLICam->y_pixel_size *= 1e6; 
  
  if (( err = FLIGetArrayArea(fli_dev, &FLICam->Array_Area[0], &FLICam->Array_Area[1], &FLICam->Array_Area[2], &FLICam->Array_Area[3])))
  {
    IDMessage(mydev, "FLIGetArrayArea() failed. %s.", strerror((int)-err));
    IDLog("FLIGetArrayArea() failed. %s.\n", strerror((int)-err));
    return;
  }
  
  if (( err = FLIGetVisibleArea( fli_dev, &FLICam->Visible_Area[0], &FLICam->Visible_Area[1], &FLICam->Visible_Area[2], &FLICam->Visible_Area[3])))
  {
    IDMessage(mydev, "FLIGetVisibleArea() failed. %s.", strerror((int)-err));
    IDLog("FLIGetVisibleArea() failed. %s.\n", strerror((int)-err));
    return;
  }
  
  if (( err = FLIGetTemperature(fli_dev, &FLICam->temperature)))
  {
    IDMessage(mydev, "FLIGetTemperature() failed. %s.", strerror((int)-err));
    IDLog("FLIGetTemperature() failed. %s.\n", strerror((int)-err));
    return;
  }
  
  IDLog("The CCD Temperature is %f.\n", FLICam->temperature);
  
  HWRevisionN[0].value = FLICam->HWRevision;				/* Hardware revision */
  FWRevisionN[0].value = FLICam->FWRevision;				/* Software revision */
  PixelSizeN[0].value  = FLICam->x_pixel_size;				/* Pixel width (um) */
  PixelSizeN[1].value  = FLICam->y_pixel_size;				/* Pixel height (um) */
  TemperatureN[0].value = FLICam->temperature;				/* CCD chip temperatre (degrees C) */
  FrameN[0].value = 0;							/* X */
  FrameN[1].value = 0;							/* Y */
  FrameN[2].value = FLICam->Visible_Area[2] - FLICam->Visible_Area[0];	/* Frame Width */
  FrameN[3].value = FLICam->Visible_Area[3] - FLICam->Visible_Area[1];	/* Frame Height */
  
  FLICam->width  = FLIImg->width = FrameN[2].value;
  FLICam->height = FLIImg->width = FrameN[3].value;
  
  BinningN[0].value = BinningN[1].value = 1;
  
  IDLog("The Camera Width is %d ---- %d\n", (int) FLICam->width, (int) FrameN[2].value);
  IDLog("The Camera Height is %d ---- %d\n", (int) FLICam->height, (int) FrameN[3].value);
  

  IDSetNumber(&HWRevisionNP, NULL);
  IDSetNumber(&FWRevisionNP, NULL);
  IDSetNumber(&PixelSizeNP, NULL);
  IDSetNumber(&TemperatureNP, NULL);
  IDSetNumber(&FrameNP, NULL);
  IDSetNumber(&BinningNP, NULL);
  
  IDLog("Exiting getBasicData()\n");
}

int manageDefaults(char errmsg[])
{
  long err;
  int exposeTimeMS;
  
  exposeTimeMS = (int) (ExposeTimeN[0].value * 1000.);
  
  IDLog("Setting default exposure time of %d ms.\n", exposeTimeMS);
  if ( (err = FLISetExposureTime(fli_dev, exposeTimeMS) ))
  {
    sprintf(errmsg, "FLISetExposureTime() failed. %s.\n", strerror((int)-err));
    IDLog(errmsg, NULL);
    return -1;
  }
  
  /* Default frame type is NORMAL */
  if ( (err = FLISetFrameType(fli_dev, FLI_FRAME_TYPE_NORMAL) ))
  {
    sprintf(errmsg, "FLISetFrameType() failed. %s.\n", strerror((int)-err));
    IDLog(errmsg, NULL);
    return -1;
  }
  
  /* X horizontal binning */
  if ( (err = FLISetHBin(fli_dev, BinningN[0].value) ))
  {
    sprintf(errmsg, "FLISetBin() failed. %s.\n", strerror((int)-err));
    IDLog(errmsg, NULL);
    return -1;
  }
  
  /* Y vertical binning */
  if ( (err = FLISetVBin(fli_dev, BinningN[1].value) ))
  {
    sprintf(errmsg, "FLISetVBin() failed. %s.\n", strerror((int)-err));
    IDLog(errmsg, NULL);
    return -1;
  }
  
  IDLog("Setting default binning %f x %f.\n", BinningN[0].value, BinningN[1].value);
  
  FLISetNFlushes(fli_dev, NFLUSHES);
  
  /* Set image area */
  if (setImageArea(errmsg))
    return -1;
  
  /* Success */
  return 0;
    
}

int getOnSwitch(ISState * states, int n)
{
 int i;
 
 for (i=0; i < n ; i++)
     if (states[i] == ISS_ON)
      return i;

 return -1;
}


int checkPowerS(ISwitchVectorProperty *sp)
{
  if (PowerSP.s != IPS_OK)
  {
    IDMessage (mydev, "Cannot change a property while the CCD is offline.");
    sp->s = IPS_IDLE;
    IDSetSwitch(sp, NULL);
    return -1;
  }

  return 0;
}

int checkPowerN(INumberVectorProperty *np)
{
  if (PowerSP.s != IPS_OK)
  {
    IDMessage (mydev, "Cannot change a property while the CCD is offline");
    np->s = IPS_IDLE;
    IDSetNumber(np, NULL);
    return -1;
  }

  return 0;
}

int checkPowerT(ITextVectorProperty *tp)
{

  if (PowerSP.s != IPS_OK)
  {
    IDMessage (mydev, "Cannot change a property while the CCD is offline");
    tp->s = IPS_IDLE;
    IDSetText(tp, NULL);
    return -1;
  }

  return 0;

}

void connectCCD(ISState *s)
{
  int i=0;
  long err;
  char errmsg[1024];
 
  IDLog ("In ConnectCCD\n");
  
  for (i=0; i < NARRAY(PowerS); i++)
    PowerS[i].s = s[i];
    
  /* USB by default {USB, SERIAL, PARALLEL, INET} */
  switch (PowerS[0].s)
  {
    case ISS_ON:
      IDLog("Attempting to find the camera in domain %ld\n", Domains[portSwitchIndex]);
      if (findcam(Domains[portSwitchIndex]))
      {
	  PowerSP.s = IPS_IDLE;
	  PowerS[0].s = ISS_OFF;
	  PowerS[1].s = ISS_ON;
	  IDSetSwitch(&PowerSP, "Error: no cameras were detected.");
	  IDLog("Error: no cameras were detected.\n");
	  return;
      }
      
      if ((err = FLIOpen(&fli_dev, FLICam->name, FLIDEVICE_CAMERA | FLICam->domain)))
      {
          PowerSP.s = IPS_IDLE;
	  PowerS[0].s = ISS_OFF;
	  PowerS[1].s = ISS_ON;
	  IDSetSwitch(&PowerSP, "Error: FLIOpen() failed. %s.", strerror( (int) -err));
	  IDLog("Error: FLIOpen() failed. %s.\n", strerror( (int) -err));
	  return;
      }
      
      /* Sucess! */
      PowerS[0].s = ISS_ON;
      PowerS[1].s = ISS_OFF;
      PowerSP.s = IPS_OK;
      IDSetSwitch(&PowerSP, "CCD is online. Retrieving basic data.");
      IDLog("CCD is online. Retrieving basic data.\n");
      getBasicData();
      if (manageDefaults(errmsg))
      {
        IDMessage(mydev, errmsg, NULL);
	IDLog(errmsg);
	return;
      }
      
      break;
      
    case ISS_OFF:
      PowerS[0].s = ISS_OFF;
      PowerS[1].s = ISS_ON;
      PowerSP.s = IPS_IDLE;
      IDSetSwitch(&PowerSP, "CCD is offline.");
      break;
     }
}

/* isCCDConnected: return 1 if we have a connection, 0 otherwise */
int isCCDConnected(void)
{
  return ((PowerS[0].s == ISS_ON) ? 1 : 0);
}

int findcam(flidomain_t domain)
{
  char **tmplist;
  long err;
  
  IDLog("In find Camera, the domain is %ld\n", domain);

  if (( err = FLIList(domain | FLIDEVICE_CAMERA, &tmplist)))
  {
    IDLog("FLIList() failed. %s\n", strerror((int)-err));
    return -1;
  }
  
  if (tmplist != NULL && tmplist[0] != NULL)
  {
    int i;

    IDLog("Trying to allocate memory to FLICam\n");
    if ((FLICam = malloc (sizeof (cam_t))) == NULL)
    {
        IDLog("malloc() failed.\n");
	return -1;
    }

    for (i = 0; tmplist[i] != NULL; i++)
    {
      int j;

      for (j = 0; tmplist[i][j] != '\0'; j++)
	if (tmplist[i][j] == ';')
	{
	  tmplist[i][j] = '\0';
	  break;
	}
    }
    
     FLICam->domain = domain;
     
      /* Each driver handles _only_ one camera for now */
      switch (domain)
      {
      case FLIDOMAIN_PARALLEL_PORT:
	strcpy(FLICam->dname, "parallel port");
	break;

      case FLIDOMAIN_USB:
	strcpy(FLICam->dname , "USB");
	break;

      case FLIDOMAIN_SERIAL:
	strcpy(FLICam->dname , "serial");
	break;

      case FLIDOMAIN_INET:
	strcpy (FLICam->dname , "inet");
	break;

      default:
	strcpy(FLICam->dname , "Unknown domain");
      }
      
      FLICam->name = strdup(tmplist[0]);
      
     if ((err = FLIFreeList(tmplist)))
     {
       IDLog("FLIFreeList() failed. %s.\n", strerror((int)-err));
       return -1;
     }
      
   } /* end if */
   else
   {
     if ((err = FLIFreeList(tmplist)))
     {
       IDLog("FLIFreeList() failed. %s.\n", strerror((int)-err));
       return -1;
     }
     
     return -1;
   }

  IDLog("Findcam() finished successfully.\n");
  return 0;
}
