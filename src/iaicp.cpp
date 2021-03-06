#include "iaicp.h"
#include <Eigen/Geometry>
using namespace pcl;


Iaicp::Iaicp()
{
    ros::param::get("/fx", fx);
    ros::param::get("/fy", fy);
    ros::param::get("/cx", cx);
    ros::param::get("/cy", cy);
    ros::param::get("/height", height);
    ros::param::get("/width", width);
    m_trans=Affine3f::Identity();
    m_predict=Affine3f::Identity();
}

Iaicp::~Iaicp(){}

void Iaicp::setupSource(CloudPtr &source)
{
    m_src.reset(new Cloud);
    m_src=source;
    intMedian=0.f;  intMad=45.f;
}

void Iaicp::setupTarget(CloudPtr &target)
{
    m_tgt.reset(new Cloud);
    m_tgt=target;
    geoMedian =0.f; geoMad=0.02f;
    intMedian=0.f;  intMad=45.f;
}
void Iaicp::setupPredict(Affine3f pred)
{
    m_predict = pred;
    m_trans = pred;

}


void Iaicp::sampleSource()
{
    cout<<m_src->points.size()<<"  "<<m_tgt->points.size()<<endl;
    m_salientSrc.reset(new Cloud());
    int cnt=0;
    int begin = 2+rand()%2;
    cout<<width<<"  "<<height<<endl;
    for(size_t i=begin; i<width-begin-4; i+=2){
        for(size_t j=begin; j<height-begin-4; j+=2){
            PointT pt=m_src->points[j*width+i];
            if (pt.z!=pt.z || pt.z>8.f) {continue;}  //continue if no depth value available

            //warp to target image
            PointT ptwarp=pt;
            ptwarp= pcl::transformPoint(ptwarp, m_predict);
            int xpos = int(floor(fx/ptwarp.z * ptwarp.x + cx));
            int ypos = int(floor(fy/ptwarp.z * ptwarp.y + cy));
            if (xpos>=width-3 || ypos>=height-3 || xpos<3 || ypos<3) {continue;} //continue if out of image border


            //check whether backgfloor point
            float z_ = m_src->points[j*width+i].z;
            float diff1=z_ - m_src->points[j*width+i+2].z;
            float diff2=z_ - m_src->points[j*width+i-2].z;
            float diff3=z_ - m_src->points[(j-2)*width+i].z;
            float diff4=z_ - m_src->points[(j+2)*width+i].z;
            if ( diff1!=diff1 || diff2!=diff2 || diff3!=diff3 || diff4!=diff4){
                                continue;
            }
            float thres= 0.021*z_;
            if ( diff1>thres || diff2>thres || diff3>thres || diff4>thres ){
                                continue;
            }

//            //image gradient
            float sim1=colorsimGray(m_src->points[j*width+i-4], m_src->points[j*width+i+4] );
            float sim2=colorsimGray(m_src->points[(j-4)*width+i], m_src->points[(j+4)*width+i] );
            if((sim1==sim1 && sim1<=0.85f)||(sim2==sim2 && sim2 <=0.85f)){
                m_salientSrc->points.push_back(m_src->points[j*width+i]);
                cnt++;
                continue;
            }

            //intensity residual
            float residual= fabs(getresidual(m_tgt->points[ypos*width+xpos], pt));
            if (fabs(residual)>100.f){
                m_salientSrc->points.push_back(m_src->points[j*width+i]);
                cnt++;
                continue;
            }

            //depth gradient
            if ( fabs(diff1-diff2) >0.03f*z_ || fabs(diff3-diff4) >0.03f*z_){
                m_salientSrc->points.push_back(m_src->points[j*width+i]);
                cnt++;
                continue;
            }

//            //large depth change
//            float zdiff = pt.z - m_tgt->points[ypos*width+xpos].z;
//            if (fabs(zdiff)>0.09f*z_){
//                m_salientSrc->points.push_back(m_src->points[j*width+i]);
//                cnt++;
//                continue;
//            }
        }
    }
    if(cnt<200){
        for(size_t i=0; i<1000; i++){
            m_salientSrc->points.push_back(m_src->points[rand()%m_src->points.size()]);
        }
    }
    vector<int> indices;
    pcl::removeNaNFromPointCloud(*m_salientSrc, *m_salientSrc, indices);
    cout<<"sampled "<< cnt<<" salient points"<<endl;
}

void Iaicp::run()
{
    sampleSource();
    int iterPerLevel = 7;
    int offset=7, maxDist=0.15f;
    iterateLevel(maxDist, offset, iterPerLevel);
    offset=3; maxDist=0.06f;
    iterateLevel(maxDist, offset, iterPerLevel);
    offset=1; maxDist=0.02f;
    iterateLevel(maxDist, offset,15);
}

void Iaicp::iterateLevel(float maxDist, int offset, int maxiter) //performs one iteration of the IaICP method
{
    for (size_t iteration=0; iteration<maxiter; iteration++){
        tgt_.reset(new Cloud());
        src_.reset(new Cloud());
        std::vector<float> geoResiduals;
        std::vector<float> intResiduals;

        int counter=0;   //counter for how many number of correspondences have been already used
        for(size_t i=0; i<m_salientSrc->points.size(); i++){
            if (counter>=150) break;    //only use  limited number of pairs of correspondences.
            int thisIndex =  rand()%m_salientSrc->points.size();  //randomly select one salient point
            PointT temp = m_salientSrc->points[thisIndex];   //selected source ponint
            PointT pt=transformPoint(temp, m_trans);   //warped source point
            PointT tgtpt;                              //for the selected correponding point from the target cloud.
            int xpos = int(floor(fx/pt.z * pt.x + cx)); //warped image coordinate x
            int ypos = int(floor(fy/pt.z * pt.y + cy)); //warped image coordinate y

            if (xpos>=(width) || ypos>=(height)|| xpos<0 || ypos<0) { continue;}
            float maxWeight = 1e-10;
            int searchRange=4;
            float intResidual, geoResidual;
            for(int xx=-searchRange; xx<searchRange+1; xx++){
                for(int yy=-searchRange; yy<searchRange+1; yy++){
                    float gridDist = sqrt(pow(float(xx),2) + pow(float(yy),2));
                    if ( gridDist > (float)searchRange ){continue;}  //get a circle shaped search area
                    int xpos_ = xpos+xx*(float)offset;  //searched target point's image coordinate
                    int ypos_ = ypos+yy*(float)offset;
                    if (xpos_>=(width-2) || ypos_>=(height-2) || xpos_<2 || ypos_<2) { continue;}
                    PointT pt2 = m_tgt->points[ypos_*width+xpos_];
                    float dist = (pt.getVector3fMap()-pt2.getVector3fMap()).norm();  //geo. distance
                    if(dist==dist){           //check for NAN
//                        if (dist>maxDist) {continue;}
                        float residual = getresidual(pt2, pt);
                        if(residual==residual){  //check for NAN
                            float geoWeight = 1e2f*(6.f/(5.f+ pow((dist)/(geoMad), 2)));
                            float colWeight = 1e2f*(6.f/(5.f+ pow((residual-intMedian)/intMad, 2)));
                            float thisweight = geoWeight * colWeight;
                            if(thisweight==thisweight && thisweight>maxWeight){
                                tgtpt=pt2;
                                maxWeight=thisweight;
                                intResidual= residual; geoResidual = dist;
                            }
                        }
                    }
                }
            }

            if(maxWeight>0 ){
                if ((m_salientSrc->points[thisIndex].getVector3fMap()-tgtpt.getVector3fMap()).norm()<1000.f){
                     src_->points.push_back(pt);
                     tgt_->points.push_back(tgtpt);

                     intResidual=getresidual(tgtpt, pt);
                     geoResidual = (pt.getVector3fMap()-tgtpt.getVector3fMap()).norm();
                     intResiduals.push_back(intResidual);
                     geoResiduals.push_back(geoResidual);
                     counter++;
                }
            }
        }

        //Estimate median and deviation for both intensity and geometry residuals
        vector<float> temp = geoResiduals;
        sort(temp.begin(), temp.end());
        geoMedian = temp[temp.size()-temp.size()/2];
        for(size_t i=0; i<temp.size(); i++){
            temp[i] = fabs(temp[i]-geoMedian);
        }
        sort(temp.begin(), temp.end());
        geoMad = 1.f*1.4826 * temp[temp.size()/2]+1e-11;
        for(size_t i=0; i<geoResiduals.size(); i++){
            geoResiduals[i] =  (6.f/(5.f+ pow((geoResiduals[i])/geoMad, 2)));
        }
        temp.clear();
        temp = intResiduals;
        sort(temp.begin(), temp.end());
        intMedian = temp[temp.size()-temp.size()/2];
        for(size_t i=0; i<temp.size(); i++){
            temp[i] = fabs(temp[i]-intMedian);
        }
        sort(temp.begin(), temp.end());
        intMad = 1.f*1.4826 * temp[temp.size()/2]+1e-11;
        for(size_t i=0; i<intResiduals.size(); i++){
            intResiduals[i] = (6.f/(5.f+ pow((intResiduals[i]-intMedian)/intMad, 2)));
        }

        pcl::TransformationFromCorrespondences transFromCorr;
        for (size_t i =0;i<src_->points.size();i++)
        {
                        Eigen::Vector3f from(src_->points.at(i).x, src_->points.at(i).y, src_->points.at(i).z);
                        Eigen::Vector3f to(tgt_->points.at(i).x, tgt_->points.at(i).y, tgt_->points.at(i).z);
                        float sensorRel = 1.f/(0.0012+0.0019*pow(src_->points.at(i).z-0.4, 2));
                        transFromCorr.add(from, to, geoResiduals[i] * intResiduals[i]*sensorRel);

        }
        Affine3f increTrans= transFromCorr.getTransformation();
        m_trans = toEigen(toVector(increTrans *m_trans) ) ;

    }
}



void Iaicp::checkAngles(Vector6f &vec)
{
   for(size_t i=3; i<6; i++){
       while (vec(i)>M_PI)  {vec(i) -= 2*M_PI;}
       while (vec(i)<-M_PI) {vec(i) += 2*M_PI;}
   }
}

Affine3f Iaicp::toEigen(Vector6f pose)
{
    return pcl::getTransformation(pose(0),pose(1),pose(2),pose(3),pose(4),pose(5));
}

Vector6f Iaicp::toVector(Affine3f pose)
{
    Vector6f temp;
    pcl::getTranslationAndEulerAngles(pose, temp(0),temp(1),temp(2),temp(3),temp(4),temp(5));
    checkAngles(temp);
    return temp;
}



