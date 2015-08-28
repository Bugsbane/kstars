/*  Ekos Scheduler Job
    Copyright (C) Jasem Mutlaq <mutlaqja@ikarustech.com>

    This application is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.
 */

#include <KLocalizedString>

#include "schedulerjob.h"

SchedulerJob::SchedulerJob()
{
    startupCondition    = START_NOW;
    completionCondition = FINISH_SEQUENCE;
    moduleUsage         = USE_NONE;
    state               = JOB_IDLE;
    fitsState           = FITS_IDLE;

    statusCell          = NULL;
    minAltitude         = -1;
    minMoonSeparation   = -1;

    culminationOffset   = 0;



    #if 0
    NowCheck=false;
    specificTime=false;
    specificAlt=false;
    moonSeparationCheck=false;
    meridianFlip=false;
    isFITSSelected=false;
    whenSeqCompCheck=false;
    loopCheck=false;
    onTimeCheck=false;
    score=0;
    alt=-1;
    isOk = 0;
    #endif
}

SchedulerJob::~SchedulerJob()
{

}

QString SchedulerJob::getName() const
{
return name;
}

void SchedulerJob::setName(const QString &value)
{
name = value;
}

SkyPoint & SchedulerJob::getTargetCoords()
{
    return targetCoords;
}

SchedulerJob::StartupCondition SchedulerJob::getStartingCondition() const
{
    return startupCondition;
}

void SchedulerJob::setStartupCondition(const StartupCondition &value)
{
    startupCondition = value;
}

QDateTime SchedulerJob::getStartupTime() const
{
    return startupTime;
}

void SchedulerJob::setStartupTime(const QDateTime &value)
{
    startupTime = value;
}
QUrl SchedulerJob::getSequenceFile() const
{
    return sequenceFile;
}

void SchedulerJob::setSequenceFile(const QUrl &value)
{
    sequenceFile = value;
}
QUrl SchedulerJob::getFITSFile() const
{
    return fitsFile;
}
void SchedulerJob::setFITSFile(const QUrl &value)
{
    fitsFile = value;
}
double SchedulerJob::getMinAltitude() const
{
    return minAltitude;
}

void SchedulerJob::setMinAltitude(const double &value)
{
    minAltitude = value;
}
double SchedulerJob::getMinMoonSeparation() const
{
    return minMoonSeparation;
}

void SchedulerJob::setMinMoonSeparation(const double &value)
{
    minMoonSeparation = value;
}
bool SchedulerJob::getEnforceWeather() const
{
    return enforceWeather;
}

void SchedulerJob::setEnforceWeather(bool value)
{
    enforceWeather = value;
}
bool SchedulerJob::getNoMeridianFlip() const
{
    return noMeridianFlip;
}

void SchedulerJob::setNoMeridianFlip(bool value)
{
    noMeridianFlip = value;
}
QDateTime SchedulerJob::getcompletionTimeEdit() const
{
    return completionTimeEdit;
}

void SchedulerJob::setcompletionTimeEdit(const QDateTime &value)
{
    completionTimeEdit = value;
}

SchedulerJob::CompletionCondition SchedulerJob::getCompletionCondition() const
{
    return completionCondition;
}

void SchedulerJob::setCompletionCondition(const CompletionCondition &value)
{
    completionCondition = value;
}

SchedulerJob::ModuleUsage SchedulerJob::getModuleUsage() const
{
    return moduleUsage;
}

void SchedulerJob::setModuleUsage(const ModuleUsage &value)
{
    moduleUsage = value;
}

SchedulerJob::JOBStatus SchedulerJob::getState() const
{
    return state;
}

void SchedulerJob::setState(const JOBStatus &value)
{
    state = value;

    if (statusCell == NULL)
        return;

    switch (state)
    {
        case JOB_IDLE:
            statusCell->setText(xi18n("Idle"));
            break;

        case JOB_BUSY:
            statusCell->setText(xi18n("Busy"));
            break;


        default:
            statusCell->setText(xi18n("Unknown"));
            break;


    }
}

SchedulerJob::FITSStatus SchedulerJob::getFITSState() const
{
    return fitsState;
}

void SchedulerJob::setFITSState(const FITSStatus &value)
{
    fitsState = value;

    switch (fitsState)
    {
        case FITS_SOLVING:
            statusCell->setText(xi18n("Solving FITS"));
            break;

        case FITS_ERROR:
            statusCell->setText(xi18n("Solver failed"));
            break;

        case FITS_COMPLETE:
            statusCell->setText(xi18n("Solver completed"));
            break;

        default:
        break;
    }
}
int SchedulerJob::getScore() const
{
    return score;
}

void SchedulerJob::setScore(int value)
{
    score = value;
}
uint16_t SchedulerJob::getCulminationOffset() const
{
    return culminationOffset;
}

void SchedulerJob::setCulminationOffset(const uint16_t &value)
{
    culminationOffset = value;
}






void SchedulerJob::setTargetCoords(dms ra, dms dec)
{
    targetCoords.setRA0(ra);
    targetCoords.setDec0(dec);
}

#if 0
QString SchedulerJob::getRA() const
{
    return RA;
}

void SchedulerJob::setRA(const QString &value)
{
RA = value;
}
QString SchedulerJob::getDEC() const
{
return DEC;
}

void SchedulerJob::setDEC(const QString &value)
{
DEC = value;
}
QString SchedulerJob::getStartTime() const
{
return startTime;
}

void SchedulerJob::setStartTime(const QString &value)
{
startTime = value;
}
QString SchedulerJob::getFinTime() const
{
return finTime;
}

void SchedulerJob::setFinTime(const QString &value)
{
finTime = value;
}
QString SchedulerJob::getFileName() const
{
return fileName;
}

void SchedulerJob::setFileName(const QString &value)
{
fileName = value;
}
SkyObject *SchedulerJob::getOb() const
{
return ob;
}

void SchedulerJob::setOb(SkyObject *value)
{
ob = value;
}
float SchedulerJob::getAlt() const
{
return alt;
}

void SchedulerJob::setAlt(float value)
{
alt = value;
}
float SchedulerJob::getMoonSeparation() const
{
return moonSeparation;
}

void SchedulerJob::setMoonSeparation(float value)
{
moonSeparation = value;
}
int SchedulerJob::getHours() const
{
return hours;
}

void SchedulerJob::setHours(int value)
{
hours = value;
}
int SchedulerJob::getMinutes() const
{
return minutes;
}

void SchedulerJob::setMinutes(int value)
{
minutes = value;
}
bool SchedulerJob::getNowCheck() const
{
return NowCheck;
}

void SchedulerJob::setNowCheck(bool value)
{
NowCheck = value;
}
bool SchedulerJob::getSpecificTime() const
{
return specificTime;
}

void SchedulerJob::setSpecificTime(bool value)
{
specificTime = value;
}
bool SchedulerJob::getSpecificAlt() const
{
return specificAlt;
}

void SchedulerJob::setSpecificAlt(bool value)
{
specificAlt = value;
}
bool SchedulerJob::getMoonSeparationCheck() const
{
return moonSeparationCheck;
}

void SchedulerJob::setMoonSeparationCheck(bool value)
{
moonSeparationCheck = value;
}
bool SchedulerJob::getMeridianFlip() const
{
return meridianFlip;
}

void SchedulerJob::setMeridianFlip(bool value)
{
meridianFlip = value;
}
bool SchedulerJob::getWhenSeqCompCheck() const
{
return whenSeqCompCheck;
}

void SchedulerJob::setWhenSeqCompCheck(bool value)
{
whenSeqCompCheck = value;
}
bool SchedulerJob::getLoopCheck() const
{
return loopCheck;
}

void SchedulerJob::setLoopCheck(bool value)
{
loopCheck = value;
}
bool SchedulerJob::getOnTimeCheck() const
{
return onTimeCheck;
}

void SchedulerJob::setOnTimeCheck(bool value)
{
onTimeCheck = value;
}

int SchedulerJob::getScore() const
{
return score;
}

void SchedulerJob::setScore(int value)
{
score = value;
}
int SchedulerJob::getFinishingHour() const
{
    return finishingHour;
}

void SchedulerJob::setFinishingHour(int value)
{
    finishingHour = value;
}
int SchedulerJob::getFinishingMinute() const
{
    return finishingMinute;
}

void SchedulerJob::setFinishingMinute(int value)
{
    finishingMinute = value;
}
bool SchedulerJob::getFocusCheck() const
{
    return focusCheck;
}

void SchedulerJob::setFocusCheck(bool value)
{
    focusCheck = value;
}
bool SchedulerJob::getAlignCheck() const
{
    return alignCheck;
}

void SchedulerJob::setAlignCheck(bool value)
{
    alignCheck = value;
}
SchedulerJob::JobState SchedulerJob::getState() const
{
    return state;
}

void SchedulerJob::setState(const JobState &value)
{
    state = value;
}
int SchedulerJob::getRowNumber() const
{
    return rowNumber;
}

void SchedulerJob::setRowNumber(int value)
{
    rowNumber = value;
}
bool SchedulerJob::getGuideCheck() const
{
    return guideCheck;
}

void SchedulerJob::setGuideCheck(bool value)
{
    guideCheck = value;
}
int SchedulerJob::getIsOk() const
{
    return isOk;
}

void SchedulerJob::setIsOk(int value)
{
    isOk = value;
}
bool SchedulerJob::getParkTelescopeCheck() const
{
    return parkTelescopeCheck;
}

void SchedulerJob::setParkTelescopeCheck(bool value)
{
    parkTelescopeCheck = value;
}
bool SchedulerJob::getWarmCCDCheck() const
{
    return warmCCDCheck;
}

void SchedulerJob::setWarmCCDCheck(bool value)
{
    warmCCDCheck = value;
}
bool SchedulerJob::getCloseDomeCheck() const
{
    return closeDomeCheck;
}

void SchedulerJob::setCloseDomeCheck(bool value)
{
    closeDomeCheck = value;
}
QString SchedulerJob::getFITSPath() const
{
    return FITSPath;
}

void SchedulerJob::setFITSPath(const QString &value)
{
    FITSPath = value;
}
double SchedulerJob::getFitsRA() const
{
    return fitsRA;
}

void SchedulerJob::setFitsRA(double value)
{
    fitsRA = value;
}
double SchedulerJob::getFitsDEC() const
{
    return fitsDEC;
}

void SchedulerJob::setFitsDEC(double value)
{
    fitsDEC = value;
}
bool SchedulerJob::getIsFITSSelected() const
{
    return isFITSSelected;
}

void SchedulerJob::setIsFITSSelected(bool value)
{
    isFITSSelected = value;
}
SchedulerJob::SolverState SchedulerJob::getSolverState() const
{
    return solverState;
}

void SchedulerJob::setSolverState(const SolverState &value)
{
    solverState = value;
}
int SchedulerJob::getMonth() const
{
    return month;
}

void SchedulerJob::setMonth(int value)
{
    month = value;
}
int SchedulerJob::getDay() const
{
    return day;
}

void SchedulerJob::setDay(int value)
{
    day = value;
}
int SchedulerJob::getFinishingMonth() const
{
    return finishingMonth;
}

void SchedulerJob::setFinishingMonth(int value)
{
    finishingMonth = value;
}
int SchedulerJob::getFinishingDay() const
{
    return finishingDay;
}

void SchedulerJob::setFinishingDay(int value)
{
    finishingDay = value;
}
double SchedulerJob::getNormalRA() const
{
    return normalRA;
}

void SchedulerJob::setNormalRA(double value)
{
    normalRA = value;
}
double SchedulerJob::getNormalDEC() const
{
    return normalDEC;
}

void SchedulerJob::setNormalDEC(double value)
{
    normalDEC = value;
}
#endif