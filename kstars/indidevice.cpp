/*  INDI frontend for KStars
    Copyright (C) 2003 Jasem Mutlaq (mutlaqja@ikarustech.com)
    		       Elwood C. Downey.

    This application is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    JM Changelog:
    2003-04-28 Used indimenu.c as a template. C --> C++, Xm --> KDE/Qt
    2003-05-01 Added tab for devices and a group feature
    2003-05-02 Added scrolling area. Most things are rewritten
    2003-05-05 Device/Group seperation
    2003-05-29 Replaced raw INDI time with KStars's timedialog
    2003-08-02 Upgrading to INDI v 1.11
    2003-08-09 Initial support for non-sidereal tracking
    2004-01-15 redesigning the GUI to support INDI v1.2 and fix previous GUI bugs
               and problems. The new GUI can easily incoperate extensions to the INDI
	       protocol as required. 

 */

#include "indiproperty.h"
#include "indigroup.h"
#include "indidevice.h"
#include "devicemanager.h"
#include "indimenu.h"
#include "indidriver.h"
#include "indistd.h"
#include "indi/indicom.h"
#include "kstars.h"
#include "skyobject.h"
#include "timedialog.h"
#include "geolocation.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>

#include <qlineedit.h>
#include <qtextedit.h>
#include <qframe.h>
#include <qtabwidget.h>
#include <qcheckbox.h>
#include <qlabel.h>
#include <qpushbutton.h>
#include <qlayout.h>
#include <qtooltip.h>
#include <qwhatsthis.h>
#include <qbuttongroup.h>
#include <qscrollview.h>
#include <qsocketnotifier.h>
#include <qvbox.h>
#include <qdatetime.h>
#include <qtable.h>
#include <qstring.h>
#include <qptrlist.h>

#include <kled.h>
#include <klineedit.h>
#include <kpushbutton.h>
#include <kapplication.h>
#include <klocale.h>
#include <kmessagebox.h>
#include <klistview.h>
#include <kdebug.h>
#include <kcombobox.h>
#include <knuminput.h>
#include <kdialogbase.h>
#include <kstatusbar.h>
#include <kpopupmenu.h>

#define NINDI_STD	18
/* INDI standard property used across all clients to enable interoperability. */
const char * indi_std[NINDI_STD] = 
  {"CONNECTION", "EQUATORIAL_COORD", "ON_COORD_SET", "ABORT_MOTION", "SOLAR_SYSTEM",
   "GEOGRAPHIC_COORD", "HORIZONTAL_COORD", "TIME", "EXPOSE_DURATION", "DEVICE_PORT", "PARK", "MOVEMENT", "SDTIME", "DATA_CHANNEL", "VIDEO_STREAM", "IMAGE_SIZE", "IMAGE_TYPE", "FILE_NAME"};

/*******************************************************************
** INDI Device: The work-horse. Responsible for handling its
** child properties and managing signal and changes.
*******************************************************************/
INDI_D::INDI_D(INDIMenu *menuParent, DeviceManager *parentManager, QString inName, QString inLabel) 
{
  name      = inName;
  label     = inLabel;
  parent    = menuParent;
  parentMgr = parentManager;
  
  gl.setAutoDelete(true);

 deviceVBox     = menuParent->addVBoxPage(inLabel);
 groupContainer = new QTabWidget(deviceVBox);
 
 msgST_w        = new QTextEdit(deviceVBox);
 msgST_w->setReadOnly(true);
 msgST_w->setMaximumHeight(100);
 
 stdDev 	= new INDIStdDevice(this, parent->ksw);

 curGroup       = NULL;

 INDIStdSupport = false;

}

INDI_D::~INDI_D()
{
   gl.clear();
   delete(deviceVBox);
   delete (stdDev);
}

void INDI_D::registerProperty(INDI_P *pp)
{
   
   if (isINDIStd(pp))
    pp->pg->dp->INDIStdSupport = true;
    
    stdDev->registerProperty(pp);
     
}

bool INDI_D::isINDIStd(INDI_P *pp)
{
  for (uint i=0; i < NINDI_STD; i++)
    if (!strcmp(pp->name.ascii(), indi_std[i]))
    {
      pp->stdID = i;
      return true;
    }
      
  return false;
}

/* Remove a property from a group, if there are no more properties 
 * left in the group, then delete the group as well */
int INDI_D::removeProperty(INDI_P *pp)
{
    for (unsigned int i=0; i < gl.count(); i++)
       if (gl.at(i)->removeProperty(pp))
       {
         if (gl.at(i)->pl.count() == 0)
	    gl.remove(i);
         return 0;
	}


    kdDebug() << "INDI: Device " << name << " has no property named " << pp->name << endl;
    return (-1);
}

/* implement any <set???> received from the device.
 * return 0 if ok, else -1 with reason in errmsg[]
 */
int INDI_D::setAnyCmd (XMLEle *root, char errmsg[])
{
	XMLAtt *ap;
	INDI_P *pp;
	
	ap = findAtt (root, "name", errmsg);
	if (!ap)
	    return (-1);

	pp = findProp (valuXMLAtt(ap));
	if (!pp)
	{
	    sprintf (errmsg,"INDI: <%s> device %s has no property named %s",
						tagXMLEle(root), name.ascii(), valuXMLAtt(ap));
	    return (-1);
	}

	parentMgr->checkMsg (root, this);

	return (setValue (pp, root, errmsg));
}

/* set the given GUI property according to the XML command.
 * return 0 if ok else -1 with reason in errmsg
 */
int INDI_D::setValue (INDI_P *pp, XMLEle *root, char errmsg[])
{
	XMLAtt *ap;

	/* set overall property state, if any */
	ap = findXMLAtt (root, "state");
	if (ap)
	{
	    if (crackLightState (valuXMLAtt(ap), &pp->state) == 0)
	      pp->drawLt (pp->state);
	    else
	    {
		sprintf (errmsg, "INDI: <%s> bogus state %s for %s %s",
						tagXMLEle(root), valuXMLAtt(ap), name.ascii(), pp->name.ascii());
		return (-1);
	    }
	}
	
	/* allow changing the timeout */
	ap = findXMLAtt (root, "timeout");
	if (ap)
	    pp->timeout = atof(valuXMLAtt(ap));

	/* process specific GUI features */
	switch (pp->guitype)
	{
	case PG_NONE:
	    break;

	case PG_NUMERIC:	/* FALLTHRU */
	case PG_TEXT:
	    return (setTextValue (pp, root, errmsg));
	    break;

	case PG_BUTTONS:	
	case PG_LIGHTS:
	case PG_RADIO:		
	case PG_MENU:
	    return (setLabelState (pp, root, errmsg));
	    break;

	default:
	    break;
	}

	return (0);
}


/* set the given TEXT or NUMERIC property from the given element.
 * root should have <text> or <number> child.
 * return 0 if ok else -1 with reason in errmsg
 */
int INDI_D::setTextValue (INDI_P *pp, XMLEle *root, char errmsg[])
{
	XMLEle *ep;
	XMLAtt *ap;
	INDI_E *lp;
	QString elementName;
	char iNumber[32];
	int min, max;
	
	for (ep = nextXMLEle (root, 1); ep != NULL; ep = nextXMLEle (root, 0))
	{
	    if (strcmp (tagXMLEle(ep), "oneText") && strcmp(tagXMLEle(ep), "oneNumber"))
		continue;

	    ap = findXMLAtt(ep, "name");
	    if (!ap)
	    {
	        kdDebug() << "Error: unable to find attribute 'name' for property " << pp->name << endl;
	        return (-1);
	    }

	    elementName = valuXMLAtt(ap);
	    
	    lp = pp->findElement(elementName);
	    
	    if (!lp)
	    {
	      sprintf(errmsg, "Error: unable to find element '%s' in property '%s'", elementName.ascii(), pp->name.ascii());
	      return (-1);
	    }
	    
	    //fprintf(stderr, "tag okay, getting perm\n");
	   switch (pp->perm)
	   {
	   case PP_RW:	// FALLTHRU
	   case PP_RO:
	     if (pp->guitype == PG_TEXT)
	     {
	     	lp->text = QString(pcdataXMLEle(ep));
		lp->read_w->setText(lp->text);
             }
	     else if (pp->guitype == PG_NUMERIC)
	     {
	       lp->value = atof(pcdataXMLEle(ep));
	       numberFormat(iNumber, lp->format.ascii(), lp->value);
	       lp->text = iNumber;
	       lp->read_w->setText(lp->text);
	       ap = findXMLAtt (ep, "min");
	       if (ap) { min = atof(valuXMLAtt(ap)); lp->setMin(min); }
	       ap = findXMLAtt (ep, "max");
	       if (ap) { max = atof(valuXMLAtt(ap)); lp->setMax(max); }
	     }
	     break;

	case PP_WO:
	    if (pp->guitype == PG_TEXT)
	      lp->write_w->setText(QString(pcdataXMLEle(ep)));
	    else if (pp->guitype == PG_NUMERIC)
	    {
	      lp->value = atof(pcdataXMLEle(ep));
	      numberFormat(iNumber, lp->format.ascii(), lp->value);
	      lp->text = iNumber;

	      if (lp->spin_w)
                lp->spin_w->setValue(lp->value);
	      else
 	        lp->write_w->setText(lp->text);
		
	       ap = findXMLAtt (ep, "min");
	       if (ap) { min = atof(valuXMLAtt(ap)); lp->setMin(min); }
	       ap = findXMLAtt (ep, "max");
	       if (ap) { max = atof(valuXMLAtt(ap)); lp->setMax(max); }
	    }
	    break;

	  }
       }

       /* handle standard cases if needed */
       stdDev->setTextValue(pp);
        
	// suppress warning
	errmsg = errmsg;

	return (0);
}

/* set the given BUTTONS or LIGHTS property from the given element.
 * root should have some <switch> or <light> children.
 * return 0 if ok else -1 with reason in errmsg
 */
int INDI_D::setLabelState (INDI_P *pp, XMLEle *root, char errmsg[])
{
	int menuChoice=0;
	unsigned i=0;
	XMLEle *ep;
	XMLAtt *ap;
        INDI_E *lp = NULL;
	int islight;
	PState state;

	/* for each child element */
	for (ep = nextXMLEle (root, 1), i=0; ep != NULL; ep = nextXMLEle (root, 0), i++)
	{
	    
	    /* only using light and switch */
	    islight = !strcmp (tagXMLEle(ep), "oneLight");
	    if (!islight && strcmp (tagXMLEle(ep), "oneSwitch"))
		continue;

	    ap =  findXMLAtt (ep, "name");
	    /* no name */
	    if (!ap)
	    {
		sprintf (errmsg, "INDI: <%s> %s %s %s requires name",
						    tagXMLEle(root), name.ascii(), pp->name.ascii(), tagXMLEle(ep));
		return (-1);
	    }

	    if ((islight && crackLightState (pcdataXMLEle(ep), &state) < 0)
		    || (!islight && crackSwitchState (pcdataXMLEle(ep), &state) < 0))
	    {
		sprintf (errmsg, "INDI: <%s> unknown state %s for %s %s %s",
					    tagXMLEle(root), pcdataXMLEle(ep), name.ascii(), pp->name.ascii(), tagXMLEle(ep));
		return (-1);
	    }

	    /* find matching label */
	    //fprintf(stderr, "Find matching label. Name from XML is %s\n", valuXMLAtt(ap));
	    lp = pp->findElement(QString(valuXMLAtt(ap)));

	    if (!lp)
	    {
		sprintf (errmsg,"INDI: <%s> %s %s has no choice named %s",
						    tagXMLEle(root), name.ascii(), pp->name.ascii(), valuXMLAtt(ap));
		return (-1);
	    }

	    QFont buttonFont;
	    /* engage new state */
	    lp->state = state;

	    switch (pp->guitype)
	    {
	     case PG_BUTTONS:
	      if (islight)
	       break;

                lp->push_w->setDown(state == PS_ON ? true : false);
		buttonFont = lp->push_w->font();
		buttonFont.setBold(state == PS_ON ? TRUE : FALSE);
		lp->push_w->setFont(buttonFont);

		break;

	     case PG_RADIO:
                 lp->check_w->setChecked(state == PS_ON ? true : false);
		 break;
	     case PG_MENU:
	       if (state == PS_ON)
	       {
	      	if (menuChoice)
	      	{
	        	sprintf(errmsg, "INDI: <%s> %s %s has multiple ON states", tagXMLEle(root), name.ascii(), pp->name.ascii());
			return (-1);
              	}
	      	menuChoice = 1;
	        pp->om_w->setCurrentItem(i);
	       }
	       break;
	       
	     case PG_LIGHTS:
	      lp->drawLt();
	      break;

	     default:
	      break;
	   }

	}
	
	stdDev->setLabelState(pp);

	return (0);
}

bool INDI_D::isOn()
{

  INDI_P *prop;

  prop = findProp(QString("CONNECTION"));
  if (!prop)
   return false;

  return (prop->isOn(QString("CONNECT")));
}

INDI_P * INDI_D::addProperty (XMLEle *root, char errmsg[])
{
	INDI_P *pp = NULL;
	INDI_G *pg = NULL;
	XMLAtt *ap = NULL;

        // Search for group tag
	ap = findAtt (root, "group", errmsg);
        if (!ap)
        {
                kdDebug() << QString(errmsg) << endl;
                return NULL;
        }
	// Find an existing group, if none found, create one
        pg = findGroup(QString(valuXMLAtt(ap)), 1, errmsg);

	if (!pg)
	 return NULL;
	 
	 /* Remove Vertical spacer from group layout, this is done everytime
	  * a new property arrives. The spacer is then appended to the end of the 
	  * properties */
	pg->propertyLayout->removeItem(pg->VerticalSpacer);

        /* get property name and add new property to dp */
	ap = findAtt (root, "name", errmsg);
	if (ap == NULL)
	    return NULL;

	if (findProp (valuXMLAtt(ap)))
	{
	    sprintf (errmsg, "INDI: <%s %s %s> already exists", tagXMLEle(root),
							name.ascii(), valuXMLAtt(ap));
	    return NULL;
	}

	pp = new INDI_P(pg, QString(valuXMLAtt(ap)));

	/* init state */
	ap = findAtt (root, "state", errmsg);
	if (!ap)
	{
	    delete(pp);
	    return (NULL);
	}

	if (crackLightState (valuXMLAtt(ap), &pp->state) < 0)
	{
	    sprintf (errmsg, "INDI: <%s> bogus state %s for %s %s",
				tagXMLEle(root), valuXMLAtt(ap), pp->pg->dp->name.ascii(), pp->name.ascii());
	    delete(pp);
	    return (NULL);
	}

	/* init timeout */
	ap = findAtt (root, "timeout", NULL);
	/* default */
	pp->timeout = ap ? atof(valuXMLAtt(ap)) : 0;

	/* log any messages */
	parentMgr->checkMsg (root, this);

	pp->addGUI(root);

	/* ok! */
	return (pp);
}

INDI_P * INDI_D::findProp (QString name)
{
       for (unsigned int i = 0; i < gl.count(); i++)
	for (unsigned int j = 0; j < gl.at(i)->pl.count(); j++)
	    if (name == gl.at(i)->pl.at(j)->name)
		return (gl.at(i)->pl.at(j));

	return NULL;
}

INDI_G *  INDI_D::findGroup (QString grouptag, int create, char errmsg[])
{
  INDI_G *ig;
  
  for (ig = gl.first(); ig != NULL; ig = gl.next() )
    if (ig->name == grouptag)
    {
      curGroup = ig;
      return ig;
    }

  /* couldn't find an existing group, create a new one if create is 1*/
  if (create)
  {
  	if (grouptag.isEmpty())
	  grouptag = "Group_1";
	  
	curGroup = new INDI_G(this, grouptag);
  	gl.append(curGroup);
        return curGroup;
  }

  sprintf (errmsg, "INDI: group %s not found in %s", grouptag.ascii(), name.ascii());
  return NULL;
}


/* find "perm" attribute in root, crack and set *pp.
 * return 0 if ok else -1 with excuse in errmsg[]
 */

 int INDI_D::findPerm (INDI_P *pp, XMLEle *root, PPerm *permp, char errmsg[])
{
	XMLAtt *ap;

	ap = findXMLAtt(root, "perm");
	if (!ap) {
	    sprintf (errmsg, "INDI: <%s %s %s> missing attribute 'perm'",
					tagXMLEle(root), pp->pg->dp->name.ascii(), pp->name.ascii());
	    return (-1);
	}
	if (!strcmp(valuXMLAtt(ap), "ro") || !strcmp(valuXMLAtt(ap), "r"))
	    *permp = PP_RO;
	else if (!strcmp(valuXMLAtt(ap), "wo"))
	    *permp = PP_WO;
	else if (!strcmp(valuXMLAtt(ap), "rw") || !strcmp(valuXMLAtt(ap), "w"))
	    *permp = PP_RW;
	else {
	    sprintf (errmsg, "INDI: <%s> unknown perm %s for %s %s",
				tagXMLEle(root), valuXMLAtt(ap), pp->pg->dp->name.ascii(), pp->name.ascii());
	    return (-1);
	}

	return (0);
}

/* convert the given light/property state string to the PState at psp.
 * return 0 if successful, else -1 and leave *psp unchanged.
 */
int INDI_D::crackLightState (char *name, PState *psp)
{
	typedef struct
	{
	    PState s;
	    const char *name;
	} PSMap;

	PSMap psmap[] =
	{
	    {PS_IDLE,  "Idle"},
	    {PS_OK,    "Ok"},
	    {PS_BUSY,  "Busy"},
	    {PS_ALERT, "Alert"},
	};

	for (int i = 0; i < 4; i++)
	    if (!strcmp (psmap[i].name, name)) {
		*psp = psmap[i].s;
		return (0);
	    }

	return (-1);
}

/* convert the given switch state string to the PState at psp.
 * return 0 if successful, else -1 and leave *psp unchanged.
 */
int INDI_D::crackSwitchState (char *name, PState *psp)
{
	typedef struct
	{
	    PState s;
	    const char *name;
	} PSMap;

	PSMap psmap[] =
	{
	    {PS_ON,  "On"},
	    {PS_OFF, "Off"},
	};


	for (int i = 0; i < 2; i++)
	    if (!strcmp (psmap[i].name, name))
	    {
		*psp = psmap[i].s;
		return (0);
	    }

	return (-1);
}

int INDI_D::buildTextGUI(XMLEle *root, char errmsg[])
{
       	INDI_P *pp = NULL;
	PPerm p;

	/* build a new property */
	pp = addProperty (root, errmsg);

	if (pp == NULL)
	    return (-1);

	/* get the permission, it will determine layout issues */
	if (findPerm (pp, root, &p, errmsg))
	{
	    delete(pp);
	    return (-1);
	}

	/* we know it will be a general text GUI */
	pp->guitype = PG_TEXT;
	pp->perm = p;
	
	if (pp->buildTextGUI(root, errmsg) < 0)
	{
	  delete (pp);
	  return (-1);
	}
	
	pp->pg->addProperty(pp);
	
	return (0);
}

/* build GUI for a number property.
 * return 0 if ok, else -1 with reason in errmsg[]
 */
int INDI_D::buildNumberGUI (XMLEle *root, char *errmsg)
{
        INDI_P *pp = NULL;
        PPerm p;

        /* build a new property */
	pp = addProperty (root, errmsg);

	if (pp == NULL)
	    return (-1);

	/* get the permission, it will determine layout issues */
	if (findPerm (pp, root, &p, errmsg))
	{
	    delete(pp);
	    return (-1);
	}
	 
	/* we know it will be a number GUI */
	pp->guitype = PG_NUMERIC;
	pp->perm = p;
	
	if (pp->buildNumberGUI(root, errmsg) < 0)
	{
	  delete (pp);
	  return (-1);
	}
	
	pp->pg->addProperty(pp);
	
	return (0);
}
	
/* build GUI for switches property.
 * rule and number of will determine exactly how the GUI is built.
 * return 0 if ok, else -1 with reason in errmsg[]
 */
int INDI_D::buildSwitchesGUI (XMLEle *root, char errmsg[])
{
	INDI_P *pp;
	XMLAtt *ap;
	XMLEle *ep;
	int n, err;

	/* build a new property */
	pp = addProperty (root, errmsg);
	if (!pp)
	    return (-1);

	ap = findAtt (root, "rule", errmsg);
	if (!ap)
	{
	    delete(pp);
	    return (-1);
	}

	/* decide GUI. might use MENU if OneOf but too many for button array */
	if (!strcmp (valuXMLAtt(ap), "OneOfMany"))
	{
	    /* count number of switches -- make menu if too many */
	    for ( ep = nextXMLEle(root, 1) , n = 0 ; ep != NULL; ep = nextXMLEle(root, 0))
		if (!strcmp (tagXMLEle(ep), "defSwitch"))
		    n++;
		    
	    if (n > MAXRADIO)
	    {
	        pp->guitype = PG_MENU;
		err = pp->buildMenuGUI (root, errmsg);
		if (err < 0)
		    delete(pp);
		    
		pp->pg->addProperty(pp);
		return (err);
	    }

	    /* otherwise, build 1-4 button layout */
	    pp->guitype = PG_BUTTONS;
	    
	    err = pp->buildSwitchesGUI(root, errmsg);
	    if (err < 0)
	      delete (pp);
	      
	    pp->pg->addProperty(pp);
	    return (err);

	}
	else if (!strcmp (valuXMLAtt(ap), "AnyOfMany"))
	{
	    /* 1-4 checkboxes layout */
	    pp->guitype = PG_RADIO;
	    
	    err = pp->buildSwitchesGUI(root, errmsg);
	    if (err < 0)
	      delete (pp);
	      
	    pp->pg->addProperty(pp);
	    return (err);
	}
	
	sprintf (errmsg, "INDI: <%s> unknown rule %s for %s %s",
				tagXMLEle(root), valuXMLAtt(ap), name.ascii(), pp->name.ascii());
	    
	delete(pp);
	return (-1);
}
	


/* build GUI for a lights GUI.
 * return 0 if ok, else -1 with reason in errmsg[] */
int INDI_D::buildLightsGUI (XMLEle *root, char errmsg[])
{
	INDI_P *pp;

	// build a new property
	pp = addProperty (root, errmsg);
	if (!pp)
	    return (-1);

	pp->guitype = PG_LIGHTS;
	
	if (pp->buildLightsGUI(root, errmsg) < 0)
	{
	  delete (pp);
	  return (-1);
	}
	
	pp->pg->addProperty(pp);
	return (0);
}

#include "indidevice.moc"
