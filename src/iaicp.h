#ifndef IAICP_H_
#define IAICP_H_

#include <pcl/point_types.h>
#include <pcl/tracking/tracking.h>
#include <pcl/features/normal_3d_omp.h>

#include <pcl/common/common.h>
#include <pcl/common/transforms.h>
#include <pcl/common/eigen.h>
#include <pcl/filters/filter.h>
#include <functional>

#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_real.hpp>
#include <boost/random/variate_generator.hpp>
#include <algorithm>
#include <numeric>

#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h>

#include "common.h"

#include <Eigen/Dense>
#include <pcl/filters/voxel_grid.h>
#include <pcl/registration/transformation_estimation_svd.h>
#include <pcl/registration/transformation_estimation_dual_quaternion.h>
#include <pcl/registration/transformation_estimation_lm.h>
#include <pcl/common/transformation_from_correspondences.h>
#include <pcl/registration/icp_nl.h>
#include <pcl/registration/correspondence_rejection_sample_consensus.h>

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

using namespace pcl;
using namespace pcl::registration;
using namespace Eigen;
using namespace std;
using namespace cv;


class Iaicp
{
public:
    Iaicp();
    ~Iaicp();
    void setupSource(CloudPtr &source);
    void setupTarget(CloudPtr &target);
    void setupPredict(Affine3f pred); //set up prediction of transformation
    void run();  //performs the iterative registration
    Affine3f getTransResult(){return m_trans;} //returns the estimated transformation result
    CloudPtr getSalientSource(){return m_salientSrc;}

    void checkAngles(Vector6f &vec);
    Affine3f toEigen(Vector6f pose);
    Vector6f toVector(Affine3f pose);

private:
    /*performs one level of iterations of the IaICP method
    maxDist: max. distance allowed between correspondences.
    offset: skipping pixel number,   refer l in the paper
    maxiter: iteration number in this level
    */
    void iterateLevel(float maxDist, int offset, int maxiter);  //performs ICP iterations with specification
    CloudPtr src_, tgt_; //used for iterateLevel(), selected correspondences for each iteration
    void sampleSource();    //sample salient points in the source frame

    float fx, fy, cx, cy; //camera parameters
    float intMedian, geoMedian, intMad, geoMad;

    int height, width; //the organzied cloud's height/width
    CloudPtr m_src, m_tgt; //source and target frame
    CloudPtr m_salientSrc; //salient points of the source frame
    Affine3f m_trans; //the transformation that transforms source frame to target frame.
    Affine3f m_predict; //the prediction of source2target transformaiton.

};



#endif // Iaicp_H
