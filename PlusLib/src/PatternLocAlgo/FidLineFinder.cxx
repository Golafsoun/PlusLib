/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

#include "FidLineFinder.h"
#include "vtkMath.h"

#include <algorithm>
#include <float.h>

#include "PlusMath.h"

#include "vnl/vnl_vector.h"
#include "vnl/vnl_matrix.h"
#include "vnl/vnl_cross.h"
#include "vnl/algo/vnl_qr.h"
#include "vnl/algo/vnl_svd.h"

static const float DOT_STEPS  = 4.0;
static const float DOT_RADIUS = 6.0;

//-----------------------------------------------------------------------------

FidLineFinder::FidLineFinder()
{
	m_FrameSize[0] = -1;
  m_FrameSize[1] = -1;
	m_ApproximateSpacingMmPerPixel = -1.0;

  for(int i= 0 ; i<6 ; i++)
  {
	  m_ImageNormalVectorInPhantomFrameMaximumRotationAngleDeg[i] = -1.0;
  }

  for(int i= 0 ; i<16 ; i++)
  {
    m_ImageToPhantomTransform[i] = -1.0;
  }

	m_MaxLinePairDistanceErrorPercent = -1.0;

	m_CollinearPointsMaxDistanceFromLineMm = -1.0; 

	m_MinTheta = -1.0; 
	m_MaxTheta = -1.0;
}

//-----------------------------------------------------------------------------

FidLineFinder::~FidLineFinder()
{

}

//-----------------------------------------------------------------------------

void FidLineFinder::ComputeParameters()
{
	LOG_TRACE("FidLineFinder::ComputeParameters");

  //Computation of MinTheta and MaxTheta from the physical probe rotation range
  std::vector<NWire> nWires;

  for( int i=0 ; i< m_Patterns.size() ; i++)
  {
    if(typeid(m_Patterns.at(i)) == typeid(NWire))//if it is a NWire
    {
      NWire* tempNWire = (NWire*)(&(m_Patterns.at(i)));
      nWires.push_back(*tempNWire);
    }
  }

	std::vector<double> thetaX, thetaY, thetaZ;
	thetaX.push_back(GetImageNormalVectorInPhantomFrameMaximumRotationAngleDeg()[0]);
	thetaX.push_back(0);
	thetaX.push_back(GetImageNormalVectorInPhantomFrameMaximumRotationAngleDeg()[1]);
	thetaY.push_back(GetImageNormalVectorInPhantomFrameMaximumRotationAngleDeg()[2]);
	thetaY.push_back(0);
	thetaY.push_back(GetImageNormalVectorInPhantomFrameMaximumRotationAngleDeg()[3]);
	thetaZ.push_back(GetImageNormalVectorInPhantomFrameMaximumRotationAngleDeg()[4]);
	thetaZ.push_back(0);
	thetaZ.push_back(GetImageNormalVectorInPhantomFrameMaximumRotationAngleDeg()[5]);

	vnl_matrix<double> imageToPhantomTransform(4,4);

	for( int i = 0 ; i<4 ;i++)
	{
		for( int j = 0 ; j<4 ;j++)
		{
			imageToPhantomTransform.put(i,j,GetImageToPhantomTransform()[j+4*i]);
		}
	}
	
	vnl_vector<double> pointA(3), pointB(3), pointC(3);

  //3 points from the Nwires
	for( int i = 0; i<3 ;i++)
	{
		pointA.put(i,nWires[0].Wires[0].EndPointFront[i]);
		pointB.put(i,nWires[0].Wires[0].EndPointBack[i]);
		pointC.put(i,nWires[0].Wires[1].EndPointFront[i]);
	}

  //create 2 vectors out of them
	vnl_vector<double> AB(3);
	AB = pointB - pointA;
	vnl_vector<double> AC(3);
	AC = pointC - pointA;

	vnl_vector<double> normalVectorInPhantomCoord(3);
	normalVectorInPhantomCoord = vnl_cross_3d(AB,AC);//Get the vector normal to the wire plane

	vnl_vector<double> normalVectorInPhantomCoordExtended(4,0);

	for( int i = 0 ;i<normalVectorInPhantomCoord.size() ;i++)
	{
		normalVectorInPhantomCoordExtended.put(i,normalVectorInPhantomCoord.get(i));
	}

	vnl_vector<double> normalImagePlane(3,0);//vector normal to the image plane
	normalImagePlane.put(2,1);

	vnl_vector<double> imageYunitVector(3,0);
	imageYunitVector.put(1,1);//(0,1,0)

	std::vector<double> finalAngleTable;

	double tempThetaX, tempThetaY, tempThetaZ;

	for(int i = 0 ; i<3 ; i++)
	{
		tempThetaX = thetaX[i]*vtkMath::Pi()/180;//Convert angles to radiant
		for(int j = 0 ; j<3 ; j++)
		{
			tempThetaY = thetaY[j]*vtkMath::Pi()/180;//Convert angles to radiant
			for(int k = 0 ; k<3 ; k++)
			{
				tempThetaZ = thetaZ[k]*vtkMath::Pi()/180;//Convert angles to radiant
				vnl_matrix<double> totalRotation(4,4,0);//The overall rotation matrix (after applying rotation around X, Y and Z axis

				totalRotation.put(0,0,cos(tempThetaY)*cos(tempThetaZ));
				totalRotation.put(0,1,-cos(tempThetaX)*sin(tempThetaZ)+sin(tempThetaX)*sin(tempThetaY)*cos(tempThetaZ));
				totalRotation.put(0,2,sin(tempThetaX)*sin(tempThetaZ)+cos(tempThetaX)*sin(tempThetaY)*cos(tempThetaZ));
				totalRotation.put(1,0,cos(tempThetaY)*sin(tempThetaZ));
				totalRotation.put(1,1,cos(tempThetaX)*cos(tempThetaZ)+sin(tempThetaX)*sin(tempThetaY)*sin(tempThetaZ));
				totalRotation.put(1,2,-sin(tempThetaX)*cos(tempThetaZ)+cos(tempThetaX)*sin(tempThetaY)*sin(tempThetaZ));
				totalRotation.put(2,0,-sin(tempThetaY));
				totalRotation.put(2,1,sin(tempThetaX)*cos(tempThetaY));
				totalRotation.put(2,2,cos(tempThetaX)*cos(tempThetaY));
				totalRotation.put(3,3,1);

				vnl_matrix<double> totalTranform(4,4);//The overall tranform matrix: Rotation*ImageToPhantom
				totalTranform = totalRotation*imageToPhantomTransform;

				vnl_vector<double> normalVectorInImageCoordExtended(4);//extended because it is a 4 dimension vector (result from a 4x4 matrix)

        //Get the normal vector in image coordinates by applying the total transform to the normal vector in phantom coordinates
				normalVectorInImageCoordExtended = totalTranform*normalVectorInPhantomCoordExtended;

				vnl_vector<double> normalVectorInImageCoord(3);//Make it 3 dimensions, the 4th is a useless value only needed for 4x4 matrix operations
				
				for( int i = 0 ;i<normalVectorInImageCoord.size() ;i++)
				{
					normalVectorInImageCoord.put(i,normalVectorInImageCoordExtended.get(i));
				}

				vnl_vector<double> lineDirectionVector(3);
				lineDirectionVector = vnl_cross_3d(normalVectorInImageCoord,normalImagePlane);

				double dotProductValue = dot_product(lineDirectionVector,imageYunitVector);
				double normOfLineDirectionvector = lineDirectionVector.two_norm();
				double angle = acos(dotProductValue/normOfLineDirectionvector);//get the angle between the line direction vector and the (0,1,0) vector
				finalAngleTable.push_back(angle);
			}
		}
	}

  //Get the maximum and the minimum angle from the table
	m_MaxTheta = (*std::max_element(finalAngleTable.begin(),finalAngleTable.end()));
	m_MinTheta = (*std::min_element(finalAngleTable.begin(),finalAngleTable.end()));
}

//-----------------------------------------------------------------------------

PlusStatus FidLineFinder::ReadConfiguration( vtkXMLDataElement* configData )
{
	LOG_TRACE("FidLineFinder::ReadConfiguration");

	if ( configData == NULL) 
	{
		LOG_WARNING("Unable to read the SegmentationParameters XML data element!"); 
		return PLUS_FAIL; 
	}

  vtkXMLDataElement* segmentationParameters = configData->FindNestedElementWithName("Segmentation");
	if (segmentationParameters == NULL)
  {
    LOG_ERROR("Cannot find Segmentation element in XML tree!");
    return PLUS_FAIL;
	}

  double approximateSpacingMmPerPixel(0.0); 
	if ( segmentationParameters->GetScalarAttribute("ApproximateSpacingMmPerPixel", approximateSpacingMmPerPixel) )
	{
		m_ApproximateSpacingMmPerPixel = approximateSpacingMmPerPixel; 
	}
  else
  {
    LOG_WARNING("Could not read ApproximateSpacingMmPerPixel from configuration file.");
  }
	
	//if the tolerance parameters are computed automatically
	int computeSegmentationParametersFromPhantomDefinition(0);
	if(segmentationParameters->GetScalarAttribute("ComputeSegmentationParametersFromPhantomDefinition", computeSegmentationParametersFromPhantomDefinition)
		&& computeSegmentationParametersFromPhantomDefinition!=0 )
	{
		vtkXMLDataElement* phantomDefinition = segmentationParameters->GetRoot()->FindNestedElementWithName("PhantomDefinition");
		if (phantomDefinition == NULL)
		{
			LOG_ERROR("No phantom definition is found in the XML tree!");
		}
		vtkXMLDataElement* customTransforms = phantomDefinition->FindNestedElementWithName("CustomTransforms"); 
		if (customTransforms == NULL) 
		{
			LOG_ERROR("Custom transforms are not found in phantom model");
		}
		else
		{
			LOG_ERROR("Unable to read image to phantom transform!"); 
		}

		double* imageNormalVectorInPhantomFrameMaximumRotationAngleDeg = new double[6];
		if ( segmentationParameters->GetVectorAttribute("ImageNormalVectorInPhantomFrameMaximumRotationAngleDeg", 6, imageNormalVectorInPhantomFrameMaximumRotationAngleDeg) )
		{
			for( int i = 0; i<6 ; i++)
			{
				m_ImageNormalVectorInPhantomFrameMaximumRotationAngleDeg[i] = imageNormalVectorInPhantomFrameMaximumRotationAngleDeg[i];
			}
		}
    else
    {
      LOG_WARNING("Could not read ImageNormalVectorInPhantomFrameMaximumRotationAngleDeg from configuration file.");
    }
		delete [] imageNormalVectorInPhantomFrameMaximumRotationAngleDeg;

		double imageToPhantomTransformVector[16]={0}; 
		if (customTransforms->GetVectorAttribute("ImageToPhantomTransform", 16, imageToPhantomTransformVector)) 
		{
			for( int i = 0; i<16 ; i++)
			{
				m_ImageToPhantomTransform[i] = imageToPhantomTransformVector[i];
			}
		}
    else
    {
      LOG_WARNING("Could not read ImageToPhantomTranform from configuration file.");
    }

		double collinearPointsMaxDistanceFromLineMm(0.0); 
		if ( segmentationParameters->GetScalarAttribute("CollinearPointsMaxDistanceFromLineMm", collinearPointsMaxDistanceFromLineMm) )
		{
			m_CollinearPointsMaxDistanceFromLineMm = collinearPointsMaxDistanceFromLineMm; 
		}
    else
    {
      LOG_WARNING("Could not read CollinearPointsMaxDistanceFromLineMm from configuration file.");
    }

    ComputeParameters();
	}
	else
	{
		double minThetaDegrees(0.0); 
		if ( segmentationParameters->GetScalarAttribute("MinThetaDegrees", minThetaDegrees) )
		{
			m_MinTheta = minThetaDegrees * vtkMath::Pi() / 180.0;
		}
    else
    {
      LOG_WARNING("Could not read minThetaDegrees from configuration file.");
    }

		double maxThetaDegrees(0.0); 
		if ( segmentationParameters->GetScalarAttribute("MaxThetaDegrees", maxThetaDegrees) )
		{
			m_MaxTheta = maxThetaDegrees * vtkMath::Pi() / 180.0; 
		}
    else
    {
      LOG_WARNING("Could not read maxThetaDegrees from configuration file.");
    }

		double collinearPointsMaxDistanceFromLineMm(0.0); 
		if ( segmentationParameters->GetScalarAttribute("CollinearPointsMaxDistanceFromLineMm", collinearPointsMaxDistanceFromLineMm) )
		{
			m_CollinearPointsMaxDistanceFromLineMm = collinearPointsMaxDistanceFromLineMm; 
		}
    else
    {
      LOG_WARNING("Could not read CollinearPointsMaxDistanceFromLineMm from configuration file.");
    }
	}

  return PLUS_SUCCESS; 
}

//-----------------------------------------------------------------------------

std::vector<NWire> FidLineFinder::GetNWires()
{
  std::vector<NWire> nWires;

  for( int i=0 ; i< m_Patterns.size() ; i++)
  {
    NWire * tempNWire = (NWire*)((m_Patterns.at(i)));
    if(tempNWire)
    {
      nWires.push_back(*tempNWire);
    }
  }

  return nWires;
}

//-----------------------------------------------------------------------------

void FidLineFinder::SetFrameSize(int frameSize[2])
{
	LOG_TRACE("FidLineFinder::SetFrameSize(" << frameSize[0] << ", " << frameSize[1] << ")");

  if ((m_FrameSize[0] == frameSize[0]) && (m_FrameSize[1] == frameSize[1]))
  {
    return;
  }

  m_FrameSize[0] = frameSize[0];
  m_FrameSize[1] = frameSize[1];

  if (m_FrameSize[0] < 0 || m_FrameSize[1] < 0)
  {
    LOG_ERROR("Dimensions of the frame size are not positive!");
    return;
  }
}

//-----------------------------------------------------------------------------

float FidLineFinder::ComputeSlope( Dot *dot1, Dot *dot2 )
{
	//LOG_TRACE("FidLineFinder::ComputeSlope");

  float x1 = dot1->GetX();
	float y1 = dot1->GetY();

	float x2 = dot2->GetX();
	float y2 = dot2->GetY();

	float y = (y2 - y1);
	float x = (x2 - x1);

	float angle = atan2(y,x);

	return angle;
}

//-----------------------------------------------------------------------------

void FidLineFinder::ComputeLine( Line &line )
{
	//LOG_TRACE("FidLineFinder::ComputeLine");

  std::vector<int> pointNum;
  float lineIntensity = 0;

  for (int i=0; i<line.GetPoints()->size(); i++)
	{
    pointNum.push_back(line.GetPoint(i));
    lineIntensity += m_DotsVector[pointNum[i]].GetDotIntensity();
	}

  //Computing the line origin
  float currentDistance, minDistance = 0;
  int originIndex = line.GetOrigin();
  for(int i=0; i<line.GetPoints()->size(); i++)
  {
    if(line.GetPoint(i) != line.GetOrigin())
    {
      currentDistance = (m_DotsVector[line.GetPoint(i)].GetX()-m_DotsVector[line.GetOrigin()].GetX())+(m_DotsVector[line.GetPoint(i)].GetY()-m_DotsVector[line.GetOrigin()].GetY());
      if(currentDistance < minDistance)
      {
        minDistance = currentDistance;
        originIndex = line.GetPoint(i);
      }
    }
  }
  line.SetOrigin(originIndex);

  line.SetIntensity(lineIntensity);
	line.SetLength(LineLength( line ));//This actually computes the length (not return) and set the endpoint as well

  if(line.GetPoints()->size() > 2)//separating cases: 2-points lines and n-points lines, way simpler for 2-points lines
  {
    //computing the line equation c + n1*x + n2*y = 0
    std::vector<float> x;
    std::vector<float> y;

    float n1, n2, c;
    
    vnl_matrix<double> A(line.GetPoints()->size(),3,1);
  	
	  for (int i=0; i<line.GetPoints()->size(); i++)
	  {
		  x.push_back(m_DotsVector[pointNum[i]].GetX());
      y.push_back(m_DotsVector[pointNum[i]].GetY());
      A.put(i,1,x[i]);
      A.put(i,2,y[i]);
	  }

    //using QR matrix decomposition
    vnl_qr<double> QR(A);
    vnl_matrix<double> Q = QR.Q();
    vnl_matrix<double> R = QR.R();

    //the B matrix is a subset of the R matrix (because we are in 2D)
    vnl_matrix<double> B(2,2,0);
    B.put(0,0,R(1,1));
    B.put(0,1,R(1,2));
    B.put(1,1,R(2,2));

    //single Value decomposition of B
    vnl_svd<double> SVD(B);
    vnl_matrix<double> V = SVD.V();

    //We get the needed coefficients from V
    n1 = V(0,1);
    n2 = V(1,1);
    c = -(n1*R(0,1)+n2*R(0,2))/R(0,0);

    if(-n2 < 0 && n1 < 0)
    {
      line.SetDirectionVector(0,n2);//the vector (n1,n2) is orthogonal to the line, therefore (-n2,n1) is a direction vector
      line.SetDirectionVector(1,-n1);
    }
    else
    {
      line.SetDirectionVector(0,-n2);//the vector (n1,n2) is orthogonal to the line, therefore (-n2,n1) is a direction vector
      line.SetDirectionVector(1,n1);
    }
  }
  else//for 2-points lines, way simpler and faster
  {
    float xdif = m_DotsVector[line.GetEndPoint()].GetX()-m_DotsVector[line.GetOrigin()].GetX();
    float ydif = m_DotsVector[line.GetEndPoint()].GetY()-m_DotsVector[line.GetOrigin()].GetY();

    line.SetDirectionVector(0, xdif);
    line.SetDirectionVector(1, ydif);
  }
}

//-----------------------------------------------------------------------------

float FidLineFinder::SegmentLength( Dot *d1, Dot *d2 )
{
	//LOG_TRACE("FidLineFinder::SegmentLength");

	float xd = d2->GetX() - d1->GetX();
	float yd = d2->GetY() - d1->GetY();
	return sqrtf( xd*xd + yd*yd );
}

//-----------------------------------------------------------------------------

float FidLineFinder::LineLength( Line &line )
{
	//LOG_TRACE("FidLineFinder::LineLength");

  //Compute the length and set the endpoint (corresponding to the longest segment from origin)
  float maxLength = -1;
  float currentLength = -1;
  int endPointIndex;
  for( int i = 0; i<line.GetPoints()->size() ; i++)
  {
    if(line.GetOrigin() != line.GetPoint(i))
    {
      currentLength = SegmentLength( &m_DotsVector[line.GetOrigin()], &m_DotsVector[line.GetPoint(i)] );
    }
    if( currentLength > maxLength )
    {
      maxLength = currentLength;
      endPointIndex = line.GetPoint(i);
    }
  }
  line.SetEndPoint(endPointIndex);

	return maxLength;
}

//-----------------------------------------------------------------------------

float FidLineFinder::ComputeDistancePointLine(Dot dot, Line line)
{     
  double x[3], y[3], z[3];

  x[0] = m_DotsVector[line.GetOrigin()].GetX();
  x[1] = m_DotsVector[line.GetOrigin()].GetY();
  x[2] = 0;

  y[0] = m_DotsVector[line.GetEndPoint()].GetX();
  y[1] = m_DotsVector[line.GetEndPoint()].GetY();
  y[2] = 0;

  z[0] = dot.GetX();
  z[1] = dot.GetY();
  z[2] = 0;

  return PlusMath::ComputeDistanceLinePoint( x, y, z);
}

//-----------------------------------------------------------------------------

void FidLineFinder::FindLines2Points()
{
	LOG_TRACE("FidLineFinder::FindLines2Points");

  if (m_DotsVector.size() < 2)
  {
    return;
  }

  std::vector<Line> twoPointsLinesVector;

  for( int i=0 ; i<m_Patterns.size() ; i++)
  {
    //the expected length of the line
    int lineLenPx = floor(m_Patterns[i]->DistanceToOriginMm[m_Patterns[i]->Wires.size()-1] / m_ApproximateSpacingMmPerPixel + 0.5 );

	  for ( int b1 = 0; b1 < m_DotsVector.size()-1; b1++ ) 
    {
		  for ( int b2 = b1+1; b2 < m_DotsVector.size(); b2++ ) 
      {
        float length = SegmentLength(&m_DotsVector[b1],&m_DotsVector[b2]);
        bool acceptLength = fabs(length-lineLenPx) < floor(m_Patterns[i]->DistanceToOriginToleranceMm[m_Patterns[i]->Wires.size()-1] / m_ApproximateSpacingMmPerPixel + 0.5 );
        
        if(acceptLength)//to only add valid two point lines
        {
          float angle = ComputeSlope(&m_DotsVector[b1], &m_DotsVector[b2]);
          bool acceptAngle = AcceptAngle(angle);

          if(acceptAngle)
          {
			      Line twoPointsLine;
			      twoPointsLine.GetPoints()->resize(2);
            twoPointsLine.SetPoint(0, b1);
			      twoPointsLine.SetPoint(1, b2);

            bool duplicate = std::binary_search(twoPointsLinesVector.begin(), twoPointsLinesVector.end(),twoPointsLine, Line::compareLines);

            if(!duplicate)
            {
              int origin = b1;
              twoPointsLine.SetOrigin(origin);
              ComputeLine(twoPointsLine);
              
			        twoPointsLinesVector.push_back(twoPointsLine);
              std::sort(twoPointsLinesVector.begin(), twoPointsLinesVector.end(), Line::compareLines);//the lines need to be sorted that way each time for the binary search to be performed
            }
          }
        }
		  }
	  }
  }
  std::sort(twoPointsLinesVector.begin(), twoPointsLinesVector.end(), Line::lessThan);//sort the lines by intensity finally 

  m_LinesVector.push_back(twoPointsLinesVector);
}

//-----------------------------------------------------------------------------

void FidLineFinder::FindLinesNPoints()
{
	/* For each point, loop over each 2-point line and try to make a 3-point
	 * line. For the third point use the theta of the line and compute a value
	 * for p. Accept the line if the compute p is within some small distance
	 * of the 2-point line. */

  LOG_TRACE("FidLineFinder::FindLines3Points");
  
  float dist = m_CollinearPointsMaxDistanceFromLineMm / m_ApproximateSpacingMmPerPixel;
  int maxNumberOfPointsPerLine = -1;
  
  for( int i=0 ; i<m_Patterns.size() ; i++ )
  {
    if(int(m_Patterns[i]->Wires.size()) > maxNumberOfPointsPerLine)
    {
      maxNumberOfPointsPerLine = m_Patterns[i]->Wires.size();
    }
  }

  for( int i=0 ; i<m_Patterns.size() ; i++)
  {
    for( int linesVectorIndex = 3 ; linesVectorIndex <= maxNumberOfPointsPerLine ; linesVectorIndex++ )
    {
      if (linesVectorIndex > m_LinesVector.size())
      {
        continue;
      }

      for ( int l = 0; l < m_LinesVector[linesVectorIndex-1].size(); l++ ) 
	    {
        Line currentShorterPointsLine;
		    currentShorterPointsLine = m_LinesVector[linesVectorIndex-1][l];//the current max point line we want to expand

        for ( int b3 = 0; b3 < m_DotsVector.size(); b3++ ) 
        {
          std::vector<int> candidatesIndex;
          bool checkDuplicateFlag = false;//assume there is no duplicate
  			  
          for( int previousPoints=0 ; previousPoints < currentShorterPointsLine.GetPoints()->size() ; previousPoints++ )
          {
            candidatesIndex.push_back(currentShorterPointsLine.GetPoint(previousPoints));
            if(candidatesIndex[previousPoints] == b3)
            {
               checkDuplicateFlag = true;//the point we want to add is already a point of the line
            }
          }

          if(checkDuplicateFlag)
          {
            continue;
          }

          candidatesIndex.push_back(b3);                 
          float pointToLineDistance = ComputeDistancePointLine(m_DotsVector[b3], currentShorterPointsLine);

		      if ( pointToLineDistance <= dist ) 
          {
			      Line line;

			      // To find unique lines, each line must have a unique configuration of points.
            std::sort(candidatesIndex.begin(),candidatesIndex.end());

            for (int f=0; f<candidatesIndex.size(); f++)
			      {
              line.GetPoints()->push_back(candidatesIndex[f]);
			      }			
            line.SetOrigin(currentShorterPointsLine.GetOrigin());

            
            float length = SegmentLength(&m_DotsVector[currentShorterPointsLine.GetOrigin()],&m_DotsVector[b3]); //distance between the origin and the point we try to add

            int lineLenPx = floor(m_Patterns[i]->DistanceToOriginMm[linesVectorIndex-2] / m_ApproximateSpacingMmPerPixel + 0.5 );
            bool acceptLength = fabs(length-lineLenPx) < floor(m_Patterns[i]->DistanceToOriginToleranceMm[linesVectorIndex-2] / m_ApproximateSpacingMmPerPixel + 0.5 );

            if(!acceptLength)
            {
              continue;
            }

            // Create the vector between the origin point to the end point and between the origin point to the new point to check if the new point is between the origin and the end point
            double originToEndPointVector[3] = { m_DotsVector[currentShorterPointsLine.GetEndPoint()].GetX()-m_DotsVector[currentShorterPointsLine.GetOrigin()].GetX() ,
                                                m_DotsVector[currentShorterPointsLine.GetEndPoint()].GetY()-m_DotsVector[currentShorterPointsLine.GetOrigin()].GetY() ,
                                                0 };

            double originToNewPointVector[3] = {m_DotsVector[b3].GetX()-m_DotsVector[currentShorterPointsLine.GetOrigin()].GetX() ,
                                                m_DotsVector[b3].GetY()-m_DotsVector[currentShorterPointsLine.GetOrigin()].GetY() ,
                                                0 };

            double dot = vtkMath::Dot(originToEndPointVector,originToNewPointVector);

            // Reject the line if the middle point is outside the original line
            if( dot < 0 )
            {
              continue;
            }                                                
            
            if(m_LinesVector.size() <= linesVectorIndex)//in case the maxpoint lines has not found any yet (the binary search works on empty vector, not on NULL one obviously)
            { 
              std::vector<Line> emptyLine;
              m_LinesVector.push_back(emptyLine);
            }

			      if(!std::binary_search(m_LinesVector[linesVectorIndex].begin(), m_LinesVector[linesVectorIndex].end(),line, Line::compareLines)) 
			      {
				      ComputeLine(line);
				      if(AcceptLine(line))
				      {
					      m_LinesVector[linesVectorIndex].push_back(line);
					      // sort the lines so that lines that are already in the list can be quickly found by a binary search
					      std::sort (m_LinesVector[linesVectorIndex].begin(), m_LinesVector[linesVectorIndex].end(), Line::compareLines);
				      }
			      }
		      }
        }
      }
    }
	}
  if(m_LinesVector[m_LinesVector.size()-1].empty())
  {
    m_LinesVector.pop_back();
  }
}

//-----------------------------------------------------------------------------

void FidLineFinder::Clear()
{
	//LOG_TRACE("FidLineFinder::Clear");

  m_DotsVector.clear();
  m_LinesVector.clear();
  m_CandidateFidValues.clear();

  std::vector<Line> emptyLine;
  m_LinesVector.push_back(emptyLine);//initializing the 0 vector of lines (unused)
  m_LinesVector.push_back(emptyLine);//initializing the 1 vector of lines (unused)
}

//-----------------------------------------------------------------------------

bool FidLineFinder::AcceptAngle( float angle )
{
  //LOG_TRACE("FidLineFinder::AcceptLine");

  //then angle must be in a certain range that is given in half space (in config file) so it needs to check the whole space (if 20 is accepted, so should -160 as it is the same orientation)
  
  if(angle > m_MinTheta && angle < m_MaxTheta)
  {
    return true;
  }
  if(m_MaxTheta < vtkMath::Pi()/2 && m_MinTheta > -vtkMath::Pi()/2)
  {
    if(angle < m_MaxTheta-vtkMath::Pi() || angle > m_MinTheta+vtkMath::Pi())
    {
      return true;
    }
  }
  else if(m_MaxTheta > vtkMath::Pi()/2)
  {
    if(angle < m_MaxTheta-vtkMath::Pi() && angle > m_MinTheta-vtkMath::Pi())
    {
      return true;
    }
  }
  else if(m_MinTheta < -vtkMath::Pi()/2)
  {
    if(angle < m_MaxTheta+vtkMath::Pi() && angle > m_MinTheta+vtkMath::Pi())
    {
      return true;
    }
  }
  return false;
}

//-----------------------------------------------------------------------------

bool FidLineFinder::AcceptLine( Line &line )
{
	//LOG_TRACE("FidLineFinder::AcceptLine");

  float angle = Line::ComputeAngle(line);
  bool acceptAngle = AcceptAngle(angle);

	if ( acceptAngle )
  {
		return true;
  }

	return false;
}

//-----------------------------------------------------------------------------

void FidLineFinder::FindLines( )
{
	LOG_TRACE("FidLineFinder::FindLines");

	// Make pairs of dots into 2-point lines.
	FindLines2Points();

	// Make 2-point lines and dots into 3-point lines.
	FindLinesNPoints();

	// Sort by intensity.
  std::sort (m_LinesVector[m_LinesVector.size()-1].begin(), m_LinesVector[m_LinesVector.size()-1].end(), Line::lessThan);
}

//-----------------------------------------------------------------------------