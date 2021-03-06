/**************************************************************
*
*     Micron Tracker: Example C++ wrapper and Multi-platform demo
*   
*     Written by: 
*      Shi Sherebrin , Robarts Research Institute - London- Ontario , www.robarts.ca
*      Shahram Izadyar, Robarts Research Institute - London- Ontario , www.robarts.ca
*      Claudio Gatti, Ahmad Kolahi, Claron Technology - Toronto- Ontario, www.clarontech.com
*
*     Copyright Claron Technology 2000-2013
*
***************************************************************/

#include "MTC.h"

#include "Cameras.h"
#include "MCamera.h"
//#include "MTVideo.h"
#include "MicronTrackerLoggerMacros.h"

/****************************/
/** Constructor */
Cameras::Cameras()
{
  this->ownedByMe = TRUE;
  this->mCurrCam = NULL;
  this->mFailedCam = NULL;
  Cameras_HistogramEqualizeImagesSet(true);
  // error handling here
}

/****************************/
/** Destructor */
Cameras::~Cameras()
{
  // Clear all previously connected camera
  for (std::vector<MCamera *>::iterator camsIterator = m_vCameras.begin(); camsIterator != m_vCameras.end(); ++camsIterator)
  {
    free(*camsIterator);
  }
  m_vCameras.clear();

  if (mCurrCam != NULL)
  {
    free(mCurrCam);
    mCurrCam=NULL;
  }

  if (mFailedCam != NULL) 
  {
    free(mFailedCam);
    mFailedCam=NULL;
  }
}

/****************************/
/** */ 
bool Cameras::getHistogramEqualizeImages()
{
	bool R;
	Cameras_HistogramEqualizeImagesGet(&R);
	return R;
}

/****************************/
int Cameras::setHistogramEqualizeImages(bool on_off)
{
	int result = Cameras_HistogramEqualizeImagesSet(on_off);
	return result == mtOK ? result : -1;
}

/****************************/
/** Returns the camera with the index of /param index. */
MCamera* Cameras::getCamera(int index)
{
  if (index>=(int)this->m_vCameras.size() || index<0)
  {
    LOG_ERROR("Invalid camera index: "<<index);
    return NULL;
  }
  return this->m_vCameras[index];
}

int Cameras::grabFrame(MCamera *cam)
{
  int result = mtOK; // success
  if (cam == NULL)
  {
    // grab from all cameras
    std::vector<MCamera *>::iterator camsIterator;
    for (camsIterator = m_vCameras.begin(); camsIterator != m_vCameras.end(); camsIterator++)
    {
      if (!(*camsIterator)->grabFrame())
      {
        mFailedCam = *camsIterator;
        result = -1;
        break;
      }
    }
  }
  else
  {
    if (!cam->grabFrame())
    {
      mFailedCam = cam;
      result = -1;
    }
  }
  return result;
}


void Cameras::Detach()
{
  Cameras_Detach();
}


int Cameras::getMTHome(std::string &mtHomeDirectory)
{
  const char mfile[] = "MTHome";

#ifdef _WIN32
  HKEY topkey=HKEY_LOCAL_MACHINE;
  const char subkey[]="SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment";

  /* Check registry key to determine log file name: */
  HKEY key=NULL;
  if ( RegOpenKeyEx(topkey, subkey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS ) 
  {
    LOG_ERROR("Failed to opern registry key: "<<subkey);
    return (-1);
  }

  const int smtHomeDirectorySize=512;
  char smtHomeDirectory[smtHomeDirectorySize+1];
  smtHomeDirectory[smtHomeDirectorySize]=0;
  DWORD value_type=0;
  DWORD value_size = smtHomeDirectorySize;
  if ( RegQueryValueEx( key, mfile, 0,  /* reserved */ &value_type, (unsigned char*)smtHomeDirectory, &value_size ) != ERROR_SUCCESS || value_size <= 1 )
  {
    /* size always >1 if exists ('\0' terminator) ? */
    LOG_ERROR("Failed to get environment variable "<<mfile);
    return (-1);
  }
  mtHomeDirectory=smtHomeDirectory;
#else
  char *localNamePtr = getenv(mfile);
  if ( localNamePtr == NULL) 
  {
    return (-1);
  }
  mtHomeDirectory=localNamePtr;
#endif

  return(0);
}

int Cameras::AttachAvailableCameras()
{
  std::string calibrationDir;
  if ( getMTHome(calibrationDir) != 0 ) 
  {
    // No Environment
    LOG_ERROR("MT home directory was not found");
    return -1;
  } 
  calibrationDir+=std::string("/CalibrationFiles");

#if 0
  // Clear all previously connected camera
  vector<MCamera *>::iterator camsIterator;
  for (camsIterator = m_vCameras.begin(); camsIterator != m_vCameras.end(); ++camsIterator)
  {
    free (*camsIterator);
  }
  if (mCurrCam != NULL) 
  {
    free(mCurrCam);
    mCurrCam = NULL;
  }
  if (mFailedCam != NULL) 
  {
    free(mFailedCam);
    mFailedCam = NULL;
  }
#endif
  int result = Cameras_AttachAvailableCameras((char*)calibrationDir.c_str());
  if ( result != mtOK) 
  {
    LOG_ERROR("Failed to attach cameras using calibration data at: "<<calibrationDir.c_str());
    return -1;
  }

  // Number of the attached cameras
  this->m_attachedCamNums = Cameras_Count();
  if (this->m_attachedCamNums <=0) 
  {
    LOG_ERROR("No attached cameras were found");
    return -1;
  }

  // Populate the array of camera that are already attached
  for (int c=0; c < this->m_attachedCamNums; c++)
  {
    if ( c > MaxCameras) 
    {
      break;
    }
    mtHandle camHandle=0;
    if (Cameras_ItemGet( c , &camHandle)==mtOK)
    {
      m_vCameras.push_back( new MCamera(camHandle));
    }
  }

  return mtOK;
}

