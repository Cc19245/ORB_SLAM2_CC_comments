/**
 * @file ORBmatcher.cc
 * @author guoqing (1337841346@qq.com)
 * @brief 处理数据关联问题
 * @version 0.1
 * @date 2019-04-26
 * 
 * @copyright Copyright (c) 2019
 * 
 */

/**
* This file is part of ORB-SLAM2.
*
* Copyright (C) 2014-2016 Raúl Mur-Artal <raulmur at unizar dot es> (University of Zaragoza)
* For more information see <https://github.com/raulmur/ORB_SLAM2>
*
* ORB-SLAM2 is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM2 is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with ORB-SLAM2. If not, see <http://www.gnu.org/licenses/>.
*/

#include "ORBmatcher.h"

#include<limits.h>

#include<opencv2/core/core.hpp>
#include<opencv2/features2d/features2d.hpp>

#include "Thirdparty/DBoW2/DBoW2/FeatureVector.h"

#include<stdint.h>

using namespace std;

namespace ORB_SLAM2
{

// 要用到的一些阈值
const int ORBmatcher::TH_HIGH = 100;
const int ORBmatcher::TH_LOW = 50;
const int ORBmatcher::HISTO_LENGTH = 30;

// 构造函数,参数默认值为0.6,true
ORBmatcher::ORBmatcher(float nnratio, bool checkOri): mfNNratio(nnratio), mbCheckOrientation(checkOri)
{
    
}

/**
 * @brief 通过投影地图点到当前帧，对Local MapPoint进行跟踪
 * 步骤
 * Step 1 遍历有效的局部地图点
 * Step 2 设定搜索搜索窗口的大小。取决于视角, 若当前视角和平均视角夹角较小时, r取一个较小的值
 * Step 3 通过投影点以及搜索窗口和预测的尺度进行搜索, 找出搜索半径内的候选匹配点索引
 * Step 4 寻找候选匹配点中的最佳和次佳匹配点
 * Step 5 筛选最佳匹配点
 * @param[in] F                         当前帧
 * @param[in] vpMapPoints               局部地图点，来自局部关键帧
 * @param[in] th                        搜索范围
 * @return int                          成功匹配的数目
 */
//Done
int ORBmatcher::SearchByProjection(Frame &F, const vector<MapPoint*> &vpMapPoints, const float th)
{
    int nmatches=0;

    // 如果 th！=1 (RGBD 相机或者刚刚进行过重定位), 需要扩大范围搜索
    const bool bFactor = th!=1.0;

    // Step 1 遍历有效的局部地图点
    for(size_t iMP=0; iMP<vpMapPoints.size(); iMP++)
    {
        MapPoint* pMP = vpMapPoints[iMP];

        // 判断该点是否要投影
        if(!pMP->mbTrackInView)
            continue;

        if(pMP->isBad())
            continue;
            
        // 通过距离预测的金字塔层数，该层数相对于当前的帧
        const int &nPredictedLevel = pMP->mnTrackScaleLevel;

        // The size of the window will depend on the viewing direction
        // Step 2 设定搜索搜索窗口的大小。取决于视角, 若当前视角和平均视角夹角较小时, r取一个较小的值
        //; 但是这个函数感觉挺鸡肋的，要么2.5，要么4.0，而且判断的角度阈值是0.36度？？？这跟没有有啥区别？
        float r = RadiusByViewingCos(pMP->mTrackViewCos);   
        
        // 如果需要扩大范围搜索，则乘以阈值th
        if(bFactor)
            r*=th;

        // Step 3 通过投影点以及搜索窗口和预测的尺度进行搜索, 找出搜索半径内的候选匹配点索引
        //; 只选特定金字塔层级上的特征点，应该也是为了后面的加速匹配，让候选的特征点数量不要太多
        const vector<size_t> vIndices =
                F.GetFeaturesInArea(pMP->mTrackProjX,pMP->mTrackProjY,      // 该地图点投影到一帧上的坐标
                                    r*F.mvScaleFactors[nPredictedLevel],    // 认为搜索窗口的大小和该特征点被追踪到时所处的尺度也有关系
                                    nPredictedLevel-1,nPredictedLevel);     // 搜索的图层范围

        // 没找到候选的,就放弃对当前点的匹配
        if(vIndices.empty())
            continue;

        const cv::Mat MPdescriptor = pMP->GetDescriptor();

        // 最优的次优的描述子距离和index
        int bestDist=256;
        int bestLevel= -1;
        int bestDist2=256;
        int bestLevel2 = -1;
        int bestIdx =-1 ;

        // Get best and second matches with near keypoints
        // Step 4 寻找候选匹配点中的最佳和次佳匹配点
        for(vector<size_t>::const_iterator vit=vIndices.begin(), vend=vIndices.end(); vit!=vend; vit++)
        {
            const size_t idx = *vit;

            // 如果Frame中的该兴趣点已经有对应的MapPoint了,则退出该次循环
            if(F.mvpMapPoints[idx])
                //; ！为什么总要判断这个mnObs观测次数呢？
                if(F.mvpMapPoints[idx]->Observations()>0)
                    continue;

            //如果是双目数据
            if(F.mvuRight[idx]>0)
            {
                //计算在X轴上的投影误差
                const float er = fabs(pMP->mTrackProjXR-F.mvuRight[idx]);
                //超过阈值,说明这个点不行,丢掉.
                //这里的阈值定义是以给定的搜索范围r为参考,然后考虑到越近的点(nPredictedLevel越大), 相机运动时对其产生的影响也就越大,
                //因此需要扩大其搜索空间.
                //当给定缩放倍率为1.2的时候, mvScaleFactors 中的数据是: 1 1.2 1.2^2 1.2^3 ... 
                if(er>r*F.mvScaleFactors[nPredictedLevel])
                    continue;
            }

            const cv::Mat &d = F.mDescriptors.row(idx);

            // 计算地图点和候选投影点的描述子距离
            const int dist = DescriptorDistance(MPdescriptor,d);
            
            // 寻找描述子距离最小和次小的特征点和索引
            if(dist<bestDist)
            {
                bestDist2=bestDist;
                bestDist=dist;
                bestLevel2 = bestLevel;
                bestLevel = F.mvKeysUn[idx].octave;
                bestIdx=idx;
            }
            else if(dist<bestDist2)
            {
                bestLevel2 = F.mvKeysUn[idx].octave;
                bestDist2=dist;
            }
        }

        // Apply ratio to second match (only if best and second are in the same scale level)
        // Step 5 筛选最佳匹配点
        // 最佳匹配距离还需要满足在设定阈值内
        if(bestDist<=TH_HIGH)
        {
            // 条件1：bestLevel==bestLevel2 表示 最佳和次佳在同一金字塔层级。
            // CC: 玄学，这是什么要求？
            // 条件2：bestDist>mfNNratio*bestDist2 表示最佳和次佳距离不满足阈值比例。 理论来说 bestDist/bestDist2 越小越好
            //; 这个其实还是比较合理的，因为如果最佳和次佳的特征点不在同一金字塔层上，那么最佳匹配特征点的唯一性就满足了
            //; 如果在同一层上，那么就有点危险了，此时如果满足最佳的距离小于次佳的距离*ratio，那么最佳特征点的唯一性也能满足
            //; 唯一性不满足的就是在同一层上，而且不满足比例要求！
            if(bestLevel==bestLevel2 && bestDist>mfNNratio*bestDist2)  // mfNNratio 是最优和次优的比例，是为了保证特征点的唯一性
                continue;

            //保存结果: 为Frame中的特征点增加对应的MapPoint
            F.mvpMapPoints[bestIdx]=pMP;   // bestIdx来自index，就是从搜索函数中得到的，对应的就是图像中的特征点的索引。所以这里就赋值当前帧的地图点
            // 匹配关系中，特征点索引为bestIdex，对应的地图点就是最佳匹配的局部地图点。
            nmatches++;
        }
    }

    return nmatches;
}

// 根据观察的视角来计算匹配的时的搜索窗口大小
//Done
float ORBmatcher::RadiusByViewingCos(const float &viewCos)
{
    // 当视角相差小于3.6°，对应cos(3.6°)=0.998，搜索范围是2.5，否则是4
    if(viewCos>0.998)
        return 2.5;
    else
        return 4.0;
}

/**
 * @brief 用基础矩阵检查极线距离是否符合要求
 * 
 * @param[in] kp1               KF1中特征点
 * @param[in] kp2               KF2中特征点  
 * @param[in] F12               从KF2到KF1的基础矩阵
 * @param[in] pKF2              关键帧KF2
 * @return true 
 * @return false 
 */
bool ORBmatcher::CheckDistEpipolarLine(const cv::KeyPoint &kp1,const cv::KeyPoint &kp2,const cv::Mat &F12,const KeyFrame* pKF2)
{
    // Epipolar line in second image l2 = x1'F12 = [a b c]
    // Step 1 求出kp1在pKF2上对应的极线
    const float a = kp1.pt.x*F12.at<float>(0,0)+kp1.pt.y*F12.at<float>(1,0)+F12.at<float>(2,0);
    const float b = kp1.pt.x*F12.at<float>(0,1)+kp1.pt.y*F12.at<float>(1,1)+F12.at<float>(2,1);
    const float c = kp1.pt.x*F12.at<float>(0,2)+kp1.pt.y*F12.at<float>(1,2)+F12.at<float>(2,2);

    // Step 2 计算kp2特征点到极线l2的距离
    // 极线l2：ax + by + c = 0
    // (u,v)到l2的距离为： |au+bv+c| / sqrt(a^2+b^2)

    const float num = a*kp2.pt.x+b*kp2.pt.y+c;

    const float den = a*a+b*b;

    // 距离无穷大
    if(den==0)
        return false;

    // 距离的平方
    const float dsqr = num*num/den;

    // Step 3 判断误差是否满足条件。尺度越大，误差范围应该越大。
    // 金字塔最底层一个像素就占一个像素，在倒数第二层，一个像素等于最底层1.2个像素（假设金字塔尺度为1.2）
    // 3.84 是自由度为1时，服从高斯分布的一个平方项（也就是这里的误差）小于一个像素，这件事发生概率超过95%时的概率 （卡方分布）
    return dsqr < 3.84*pKF2->mvLevelSigma2[kp2.octave];
}

/*
 * @brief 通过词袋，对关键帧的特征点进行跟踪
 * 步骤
 * Step 1：分别取出属于同一node的ORB特征点(只有属于同一node，才有可能是匹配点)
 * Step 2：遍历KF中属于该node的特征点
 * Step 3：遍历F中属于该node的特征点，寻找最佳匹配点
 * Step 4：根据阈值 和 角度投票剔除误匹配
 * Step 5：根据方向剔除误匹配的点
 * @param  pKF               关键帧
 * @param  F                 当前普通帧
 * @param  vpMapPointMatches F中地图点对应的匹配，NULL表示未匹配
 * @return                   成功匹配的数量
 */
//Done
int ORBmatcher::SearchByBoW(KeyFrame* pKF,Frame &F, vector<MapPoint*> &vpMapPointMatches)
{
    // 获取该关键帧的地图点
    const vector<MapPoint*> vpMapPointsKF = pKF->GetMapPointMatches();

    // 和普通帧F特征点的索引一致
    //; 这种写法是什么意思，重新设置了大小吗？
    //; CC: 这就是赋初值的意思，就相当于同时定义了这个vector的长度和默认的元素值，和数组一样。但是注意vector的长度是可以变的，这里的长度相当于预留了一个长度
    vpMapPointMatches = vector<MapPoint*>(F.N, static_cast<MapPoint*>(NULL));  

    // 取出关键帧的词袋特征向量
    //; FeatureVector是一个map，键是这帧图像的特征点归类的word在哪个node下，值是这个node下还有哪些特征点
    //; 具体操作是：在计算词袋向量的时候，把所有特征点归类到某一层的node下，然后比如一共归类到1 2 3 三个node下，最后把所有的单词都归到这三个node下，
    //; 那么就能得到在node 1 2 3 下的特征点分别是哪些，从而加速后面的匹配
    const DBoW2::FeatureVector &vFeatVecKF = pKF->mFeatVec; 

    int nmatches=0;

    // 特征点角度旋转差统计用的直方图
    vector<int> rotHist[HISTO_LENGTH];
    for(int i=0;i<HISTO_LENGTH;i++)
        rotHist[i].reserve(500);

    // 将0~360的数转换到0~HISTO_LENGTH的系数
    // !原作者代码是 const float factor = 1.0f/HISTO_LENGTH; 是错误的，更改为下面代码  
    const float factor = HISTO_LENGTH/360.0f;

    // We perform the matching over ORB that belong to the same vocabulary node (at a certain level)
    // 将属于同一节点(特定层)的ORB特征进行匹配
    DBoW2::FeatureVector::const_iterator KFit = vFeatVecKF.begin();
    DBoW2::FeatureVector::const_iterator Fit = F.mFeatVec.begin();
    DBoW2::FeatureVector::const_iterator KFend = vFeatVecKF.end();
    DBoW2::FeatureVector::const_iterator Fend = F.mFeatVec.end();

    while(KFit != KFend && Fit != Fend)
    {
/**  node id 加速匹配的思想：
 *  1.如果归类node id 所在的层级别太高，比如是最高的根节点，那么所有的特征点都属于这个根节点，那么在进行匹配的时候，相当于下面的KFit->first == Fit->first一定成立
 *    因为只有这俩都是只有一个node id，都是根节点0。那么这个函数里面进行遍历的时候，要遍历关键帧的所有特征点，并且在当前帧中遍历所有特征点匹配，这样和暴力匹配相同。
 *    这样就相当于这个程序中的两个for循环次数过多
 * 
 *  2.如果归类node id 所在层级太低，比如是最底层的叶子节点，那么每个特征点可能都被分到一个叶子节点中，这样虽然每个叶子节点中包含的特征点很少，但是叶子节点太多了，
 *    这样导致虽然里面的for循环次数很少，但是外面的while循环遍历关键帧和当前帧中的node次数过多，而且如果是每个叶子节点都只有一个特征点的话，这样循环又和暴力匹配相同了。
 * 
 *  3.所以归类node id 所在的层级不能太低也不能太高，否则会导致两个极端，都等于暴力匹配。
 *    这个层级越高，那么while分的node id越少，外层while循环越少，但是每个node包含的特征点越多，内层的两个for循环越多；
 *    这个层级越低，那么while分的node id越多，外层while循环越多，但是每个node包含的特征点越少，内层的两个for循环越少。
 * 
 *  补充：好像不太对？如果把层级选的很低的话，那么匹配的时候就是使用最外层的while循环，而不用去匹配描述子了（假设每个描述子都别放入到不同的叶子节点中）
 *   但是这样带来的结果是不是匹配完全依赖于词袋的分类了？
 *   这样循环不需要计算描述子的距离，而是完全依靠词袋来进行分类。那这样和匹配描述子来进行分类有什么区别呢？结果会有很大的不同吗？
 *   感觉这样匹配好像过于依赖聚类得到的词袋？而没有充分利用现在的描述子信息？
 * 
 */

        // Step 1：分别取出属于同一node的ORB特征点(只有属于同一node，才有可能是匹配点)
        // first 元素就是node id，遍历
        if(KFit->first == Fit->first) 
        {
            // second 是该node内存储的feature index
            const vector<unsigned int> vIndicesKF = KFit->second;  // 在参考关键帧KF中属于当前node id的所有特征点的索引
            const vector<unsigned int> vIndicesF = Fit->second;    // 在当前帧F中属于当前node id的所有特征点的索引

            // Step 2：遍历KF中属于该node的特征点
            for(size_t iKF=0; iKF<vIndicesKF.size(); iKF++)
            {
                // 关键帧该节点中特征点的索引
                const unsigned int realIdxKF = vIndicesKF[iKF];

                // 取出KF中该特征点对应的地图点
                MapPoint* pMP = vpMapPointsKF[realIdxKF]; 

                if(!pMP)
                    continue;

                if(pMP->isBad())
                    continue;

                const cv::Mat &dKF= pKF->mDescriptors.row(realIdxKF); // 取出KF中该特征对应的描述子

                int bestDist1=256; // 最好的距离（最小距离）
                int bestIdxF =-1 ;
                int bestDist2=256; // 次好距离（倒数第二小距离）

                // Step 3：遍历F中属于该node的特征点，寻找最佳匹配点
                for(size_t iF=0; iF<vIndicesF.size(); iF++)
                {
                    // 和上面for循环重名了,这里的realIdxF是指普通帧该节点中特征点的索引
                    const unsigned int realIdxF = vIndicesF[iF];

                    // 如果地图点存在，说明这个点已经被匹配过了，不再匹配，加快速度
                    if(vpMapPointMatches[realIdxF])  // 普通帧中的这个特征点已经有匹配的地图点了，那么说明已经匹配上了之前循环中的地图点
                        continue;

                    const cv::Mat &dF = F.mDescriptors.row(realIdxF); // 取出F中该特征对应的描述子
                    // 计算描述子的距离
                    const int dist =  DescriptorDistance(dKF,dF); 

                    // 遍历，记录最佳距离、最佳距离对应的索引、次佳距离等
                    // 如果 dist < bestDist1 < bestDist2，更新bestDist1 bestDist2
                    if(dist<bestDist1)
                    {
                        bestDist2=bestDist1;
                        bestDist1=dist;
                        bestIdxF=realIdxF;
                    }
                    // 如果bestDist1 < dist < bestDist2，更新bestDist2
                    else if(dist<bestDist2) 
                    {
                        bestDist2=dist;
                    }
                }

                // Step 4：根据阈值 和 角度投票剔除误匹配
                // Step 4.1：第一关筛选：匹配距离必须小于设定阈值
                if(bestDist1<=TH_LOW) 
                {
                    // Step 4.2：第二关筛选：最佳匹配比次佳匹配明显要好，那么最佳匹配才真正靠谱
                    if(static_cast<float>(bestDist1)<mfNNratio*static_cast<float>(bestDist2))
                    {
                        // Step 4.3：记录成功匹配特征点的对应的地图点(来自关键帧)
                        // pMP是关键帧中的当前特征点对应的地图点
                        // bestIdxF是当前帧中和关键帧中的当期特征点最匹配的那个特征点，把这个特征点对应的地图点设置成关键帧中的当前特征点对应的地图点
                        vpMapPointMatches[bestIdxF]=pMP;  

                        // 这里的realIdxKF是当前遍历到的关键帧的特征点id
                        const cv::KeyPoint &kp = pKF->mvKeysUn[realIdxKF];

                        // Step 4.4：计算匹配点旋转角度差所在的直方图
                        if(mbCheckOrientation)
                        {
                            // angle：每个特征点在提取描述子时的旋转主方向角度，如果图像旋转了，这个角度将发生改变
                            // 所有的特征点的角度变化应该是一致的，通过直方图统计得到最准确的角度变化值
                            float rot = kp.angle-F.mvKeys[bestIdxF].angle;// 该特征点的角度变化值
                            if(rot<0.0)
                                rot+=360.0f;
                            int bin = round(rot*factor);// 将rot分配到bin组, 四舍五入, 其实就是离散到对应的直方图组中
                            if(bin==HISTO_LENGTH)
                                bin=0;
                            assert(bin>=0 && bin<HISTO_LENGTH);
                            rotHist[bin].push_back(bestIdxF);       // 直方图统计
                        }
                        nmatches++;
                    }
                }

            }
            KFit++;
            Fit++;
        }
        // 因为在生成特征点的词袋向量的时候就对node id进行排序了，所以这里可以直接对序号相加
        else if(KFit->first < Fit->first)
        {
            // 对齐
            KFit = vFeatVecKF.lower_bound(Fit->first);  // 找到>=Fit的node id的最小序号node
        }
        else
        {
            // 对齐
            Fit = F.mFeatVec.lower_bound(KFit->first);
        }
    }  // 执行到这里的时候，对关键帧中的所有特征点，都得到了在当前帧中的一个匹配特征点

    // Step 5 根据方向剔除误匹配的点
    if(mbCheckOrientation)
    {
        // index
        int ind1=-1;
        int ind2=-1;
        int ind3=-1;

        // 筛选出在旋转角度差落在在直方图区间内数量最多的前三个bin的索引
        ComputeThreeMaxima(rotHist,HISTO_LENGTH,ind1,ind2,ind3);

        for(int i=0; i<HISTO_LENGTH; i++)
        {
            // 如果特征点的旋转角度变化量属于这三个组，则保留
            if(i==ind1 || i==ind2 || i==ind3)
                continue;

            // 剔除掉不在前三的匹配对，因为他们不符合“主流旋转方向”  
            for(size_t j=0, jend=rotHist[i].size(); j<jend; j++)
            {
                // 擦除匹配关系这里就是把匹配的地图点去掉
                vpMapPointMatches[rotHist[i][j]]=static_cast<MapPoint*>(NULL);
                nmatches--;
            }
        }
    }

    return nmatches;
}


/**
 * @brief 根据Sim3变换，将闭环KF及其共视KF的所有地图点（不考虑当前KF已经匹配的地图点）投影到当前KF，生成新的匹配点对
 * 
 * @param[in] pKF               当前KF
 * @param[in] Scw               当前KF和闭环KF之间的Sim3变换
 * @param[in] vpPoints          闭环KF及其共视KF的地图点
 * @param[in] vpMatched         当前KF的已经匹配的地图点
 * @param[in] th                搜索范围
 * @return int                  返回新的成功匹配的点对的数目
 */
int ORBmatcher::SearchByProjection(KeyFrame* pKF, cv::Mat Scw, const vector<MapPoint*> &vpPoints, vector<MapPoint*> &vpMatched, int th)
{
    // Get Calibration Parameters for later projection
    const float &fx = pKF->fx;
    const float &fy = pKF->fy;
    const float &cx = pKF->cx;
    const float &cy = pKF->cy;

    // Decompose Scw
    // Step 1 分解Sim变换矩阵
    //; Scw是世界坐标系到当前帧的Sim3变换
    //; 也就是对于世界坐标系中的一个3D点X_w，如果将其变换到当前帧的相机坐标系下，不是一个简单的欧式变换（旋转+平移），
    //; 而是一个sim3变换，也就是先旋转，然后有一个尺度因子的缩放，最后再平移让两个坐标系的原点对齐
    //? 为什么要剥离尺度信息？
    //; CC解答：
    /* 要搞明白这个问题，需要时刻记住当前这个函数想干什么：就是在闭环候选帧的地图点中寻找更多的和当前关键帧匹配的地图点（不包括已经和当前关键帧匹配的地图点）！

       这个函数传入了一个Scw，也就是上一步计算出来的 世界坐标系 到 当前帧相机坐标系的变换关系，这个变换关系一个最重要的贡献就是指明了两个坐标系之间存在尺度
       漂移，也就是矩阵中的s项。如果这个s项为1，那么Scw就从sim3变换退化成了欧式变换，也就是只有旋转和平移，尺度保持不变。而这个s项的大小，就是说明c和w两个
       坐标系之间存在多大的尺度变化。

       1.首先分析这个s对于坐标变换有什么影响：世界坐标系中的点Xw变换到当前帧相机坐标系下结果，使用正常的sim3变换计算出来的相机坐标系下的坐标是 
          Xc = sR*Xw + t； 使用代码中剥离掉尺度因子s计算出来的坐标是 Xc'= R*Xw + t/s = 1/s * Xc。 从这结果可以看到，代码中计算出来的坐标和
          使用sim3计算出来的坐标就是差了一个尺度因子s。相当于两个坐标点是同向的，只不过离原点的距离差s倍。
       2.对于相机中心在世界坐标系下的坐标：使用正常的sim3变换，得Ow = -1/s * R.t * t； 使用代码中剥离掉尺度因子s计算出来的坐标：
          Ow = -1/s^2 * R.t * t。可见二者也是同向的，也只是距离世界坐标的原点差了s倍。

       3.回到函数的目的，寻找和当前帧匹配的地图点。看筛选条件：
        （1）3D地图点投影到当前相机的像素坐标要有效：由于1中已经论述两种情况得到的在相机坐标系下的坐标是同向的，也就是在相机的同一条光线上，
            所以他们投影的像素点是相同的
        （2）计算3D地图点到当前相机的向量，计算向量长度得到3D点离相机的距离，这个距离要在 “创建这个3D地图点时预测出来的能够在金字塔中检测到这个3D点
            对应的特征点的距离范围” 之内。那么选择那种方式计算出来的Ow作为相机中心在世界坐标系下的坐标呢？
    */
    /*
    //; 首先明确闭环候选帧及其共视关键帧的地图点是准确的，相对于世界坐标系是准确的
    //; 而当前帧的位姿和当前帧的地图点的坐标都是不准确的，因为有尺度漂移。
    //; 所以如果我想把闭环候选帧的地图点投影到当前帧中进行投影匹配，那么我该用的是当前帧的sim3变换，这样才是正确的。
    */
    cv::Mat sRcw = Scw.rowRange(0,3).colRange(0,3);
    const float scw = sqrt(sRcw.row(0).dot(sRcw.row(0)));   // 计算得到尺度s
    cv::Mat Rcw = sRcw/scw;                                 // 保证旋转矩阵行列式为1
    cv::Mat tcw = Scw.rowRange(0,3).col(3)/scw;             // 去掉尺度后的平移向量
    cv::Mat Ow = -Rcw.t()*tcw;                              // 世界坐标系下相机光心坐标

    // Set of MapPoints already found in the KeyFrame
    // 使用set类型，记录前面已经成功的匹配关系，避免重复匹配。并去除其中无效匹配关系（NULL）
    set<MapPoint*> spAlreadyFound(vpMatched.begin(), vpMatched.end());
    spAlreadyFound.erase(static_cast<MapPoint*>(NULL));

    int nmatches=0;

    // For each Candidate MapPoint Project and Match
    // Step 2 遍历闭环KF及其共视KF的所有地图点（不考虑当前KF已经匹配的地图点）投影到当前KF
    for(int iMP=0, iendMP=vpPoints.size(); iMP<iendMP; iMP++)
    {
        MapPoint* pMP = vpPoints[iMP];

        // Discard Bad MapPoints and already found
        // Step 2.1 丢弃坏点，跳过当前KF已经匹配上的地图点
        if(pMP->isBad() || spAlreadyFound.count(pMP))
            continue;

        // Get 3D Coords.
        // Step 2.2 投影到当前KF的图像坐标并判断是否有效
        cv::Mat p3Dw = pMP->GetWorldPos();

        // Transform into Camera Coords.  
        //; 变换到相机坐标系下，注意这里没有使用求出来的sim3变换，而是使用了去除尺度的欧式变换，为什么？
        //; CC：在sim3中尺度s只是改变了旋转后的缩放，对平移没有影响。所以对于sim3和去除s的sim3（欧式变换），
        //; 他们变换之后的坐标也只是差了一个尺度因子s，其他没有影响。相当于最后变换后的点的坐标是成倍数关系的，这个倍数关系就是s
        //; 疑问：注意看上面的代码，作者把平移向量也去掉了尺度s，所以最后使用单纯的sim3和代码中写的这种方式，坐标之间就是单纯差一个倍数关系
        cv::Mat p3Dc = Rcw*p3Dw+tcw;  //; 这个是相机坐标系下的地图点

        // Depth must be positive
        // 深度值必须为正
        if(p3Dc.at<float>(2)<0.0)
            continue;

        // Project into Image
        const float invz = 1/p3Dc.at<float>(2);
        const float x = p3Dc.at<float>(0)*invz;
        const float y = p3Dc.at<float>(1)*invz;

        const float u = fx*x+cx;
        const float v = fy*y+cy;

        // Point must be inside the image
        // 在图像范围内
        if(!pKF->IsInImage(u,v))
            continue;

        // Depth must be inside the scale invariance region of the point
        // 判断距离是否在有效距离内
        const float maxDistance = pMP->GetMaxDistanceInvariance();
        const float minDistance = pMP->GetMinDistanceInvariance();
        // 地图点到相机光心的向量
        //; 剥离尺度因子s造成的真正影响在于当前帧的相机中心在世界坐标系中的坐标Ow
        //; pMP是闭环候选帧的地图点，因为认为闭环候选帧是在刚开始的时候产生的，所以其尺度漂移不严重，故认为闭环候选帧的相机坐标系和
        //; 世界坐标系之间是欧式变换，或者说是s=1的sim3变换。那么上面的maxDistance和minDistance就是没有尺度漂移的距离。
        //; 下面一句计算这个地图点到当前帧相机中心的向量PO和距离，注意这个地图点还是没有漂移的，如果Ow是有漂移的，那么计算出来的这个距离就
        //; 没有参考价值了，因为最后就是要使用这个距离去评估这个地图点是否有可能出现在当前帧相机的深度范围内（maxDistance和minDistance就是
        //; 依据能在金字塔中检测到这个地图点来确定的），所以说这里需要舍弃s的影响，考虑纯欧式变换时这个地图点离相机的距离，从而判断这个地图点是否在
        //; 相机的可视深度范围内
        cv::Mat PO = p3Dw-Ow;
        const float dist = cv::norm(PO);

        if(dist<minDistance || dist>maxDistance)
            continue;

        // Viewing angle must be less than 60 deg
        // 观察角度小于60°
        cv::Mat Pn = pMP->GetNormal();

        if(PO.dot(Pn)<0.5*dist)
            continue;

        // 根据当前这个地图点距离当前KF光心的距离,预测该点在当前KF中的尺度(图层)
        int nPredictedLevel = pMP->PredictScale(dist,pKF);

        // Search in a radius
        // 根据尺度确定搜索半径
        const float radius = th*pKF->mvScaleFactors[nPredictedLevel];

        //  Step 2.3 搜索候选匹配点
        const vector<size_t> vIndices = pKF->GetFeaturesInArea(u,v,radius);

        if(vIndices.empty())
            continue;

        // Match to the most similar keypoint in the radius
        const cv::Mat dMP = pMP->GetDescriptor();

        int bestDist = 256;
        int bestIdx = -1;
        //  Step 2.4 遍历候选匹配点，找到最佳匹配点
        for(vector<size_t>::const_iterator vit=vIndices.begin(), vend=vIndices.end(); vit!=vend; vit++)
        {
            const size_t idx = *vit;
            if(vpMatched[idx])
                continue;

            const int &kpLevel= pKF->mvKeysUn[idx].octave;

            // 不在一个尺度也不行
            if(kpLevel<nPredictedLevel-1 || kpLevel>nPredictedLevel)
                continue;

            const cv::Mat &dKF = pKF->mDescriptors.row(idx);

            const int dist = DescriptorDistance(dMP,dKF);

            if(dist<bestDist)
            {
                bestDist = dist;
                bestIdx = idx;
            }
        }

        // 该MapPoint与bestIdx对应的特征点匹配成功
        if(bestDist<=TH_LOW)
        {
            vpMatched[bestIdx]=pMP;
            nmatches++;
        }

    }
    //  Step 3 返回新的成功匹配的点对的数目
    return nmatches;
}

/**
 * @brief 单目初始化中用于参考帧和当前帧的特征点匹配
 * 步骤
 * Step 1 构建旋转直方图
 * Step 2 在半径窗口内搜索当前帧F2中所有的候选匹配特征点 
 * Step 3 遍历搜索搜索窗口中的所有潜在的匹配候选点，找到最优的和次优的
 * Step 4 对最优次优结果进行检查，满足阈值、最优/次优比例，删除重复匹配
 * Step 5 计算匹配点旋转角度差所在的直方图
 * Step 6 筛除旋转直方图中“非主流”部分
 * Step 7 将最后通过筛选的匹配好的特征点保存
 * @param[in] F1                        初始化参考帧                  
 * @param[in] F2                        当前帧
 * @param[in & out] vbPrevMatched       本来存储的是参考帧的所有特征点坐标，该函数更新为匹配好的当前帧的特征点坐标
 * @param[in & out] vnMatches12         保存参考帧F1中特征点是否匹配上，index保存是F1对应特征点索引，值保存的是匹配好的F2特征点索引
 * @param[in] windowSize                搜索窗口
 * @return int                          返回成功匹配的特征点数目
 */
//Done
int ORBmatcher::SearchForInitialization(Frame &F1, Frame &F2, vector<cv::Point2f> &vbPrevMatched, vector<int> &vnMatches12, int windowSize)
{
    int nmatches=0;
    // F1中特征点和F2中匹配关系，注意是按照F1特征点数目分配空间，因为是找F1中的特征点在F2中的对应匹配点
    vnMatches12 = vector<int>(F1.mvKeysUn.size(),-1);

    // Step 1 构建旋转直方图，HISTO_LENGTH = 30
    vector<int> rotHist[HISTO_LENGTH];
    for(int i=0;i<HISTO_LENGTH;i++)
    // 每个bin里预分配500个，因为使用的是vector不够的话可以自动扩展容量
        rotHist[i].reserve(500);   

    //! 原作者代码是 const float factor = 1.0f/HISTO_LENGTH; 是错误的，更改为下面代码   
    const float factor = HISTO_LENGTH/360.0f;

    // 匹配点对距离，注意是按照F2特征点数目分配空间
    vector<int> vMatchedDistance(F2.mvKeysUn.size(),INT_MAX);
    // 从帧2到帧1的反向匹配，注意是按照F2特征点数目分配空间
    vector<int> vnMatches21(F2.mvKeysUn.size(),-1);

    // 遍历帧1中的所有特征点
    for(size_t i1=0, iend1=F1.mvKeysUn.size(); i1<iend1; i1++)
    {
        cv::KeyPoint kp1 = F1.mvKeysUn[i1];
        int level1 = kp1.octave;
        // 只使用原始图像上提取的特征点
        if(level1>0)
            continue;

        // Step 2 在半径窗口内搜索当前帧F2中所有的候选匹配特征点(实际进去看这个函数的话，并不是在一个圆形窗口内，而是在一个正方形窗口内)
        // vbPrevMatched 输入的是参考帧 F1的特征点
        // windowSize = 100，输入最大最小金字塔层级 均为0
        vector<size_t> vIndices2 = F2.GetFeaturesInArea(vbPrevMatched[i1].x,vbPrevMatched[i1].y, windowSize,level1,level1);

        // 没有候选特征点，跳过
        if(vIndices2.empty())
            continue;

        // 取出参考帧F1中当前遍历特征点对应的描述子
        cv::Mat d1 = F1.mDescriptors.row(i1);

        int bestDist = INT_MAX;     //最佳描述子匹配距离，越小越好
        int bestDist2 = INT_MAX;    //次佳描述子匹配距离
        int bestIdx2 = -1;          //最佳候选特征点在F2中的index

        // Step 3 遍历搜索搜索窗口中的所有潜在的匹配候选点，找到最优的和次优的
        for(vector<size_t>::iterator vit=vIndices2.begin(); vit!=vIndices2.end(); vit++)
        {
            size_t i2 = *vit;  // i2是帧2中特征点对应的索引
            // 取出候选特征点对应的描述子
            cv::Mat d2 = F2.mDescriptors.row(i2);
            // 计算两个特征点描述子距离
            int dist = DescriptorDistance(d1,d2);

            if(vMatchedDistance[i2]<=dist)
                continue;
            // 如果当前匹配距离更小，更新最佳次佳距离
            if(dist<bestDist)
            {
                bestDist2=bestDist;
                bestDist=dist;
                bestIdx2=i2;
            }
            // 更新次佳距离
            else if(dist<bestDist2)
            {
                bestDist2=dist;
            }
        }

        // Step 4 对最优次优结果进行检查，满足阈值、最优/次优比例，删除重复匹配
        // 即使算出了最佳描述子匹配距离，也不一定保证配对成功。要小于设定阈值
        if(bestDist<=TH_LOW)   // 最佳的距离要<50的阈值
        { 
            // 最佳距离比次佳距离要小于设定的比例，这样特征点辨识度更高
            if(bestDist<(float)bestDist2*mfNNratio)
            {
                // 如果找到的候选特征点对应F1中特征点已经匹配过了，说明发生了重复匹配，将原来的匹配也删掉
                if(vnMatches21[bestIdx2]>=0)
                {
                    vnMatches12[vnMatches21[bestIdx2]]=-1;
                    nmatches--;
                }
                // 次优的匹配关系，双向建立
                // vnMatches12保存参考帧F1和F2匹配关系，index保存是F1对应特征点索引，值保存的是匹配好的F2特征点索引
                vnMatches12[i1]=bestIdx2;
                vnMatches21[bestIdx2]=i1;
                vMatchedDistance[bestIdx2]=bestDist;
                nmatches++;

                // Step 5 计算匹配点旋转角度差所在的直方图
                if(mbCheckOrientation)
                {
                    // 计算匹配特征点的角度差，这里单位是角度°，不是弧度
                    float rot = F1.mvKeysUn[i1].angle-F2.mvKeysUn[bestIdx2].angle;
                    if(rot<0.0)
                        rot+=360.0f;
                    // 前面factor = HISTO_LENGTH/360.0f 
                    // bin = rot / 360.of * HISTO_LENGTH 表示当前rot被分配在第几个直方图bin  
                    int bin = round(rot*factor); // round四舍五入，最大到30
                    // 如果bin 满了又是一个轮回
                    if(bin==HISTO_LENGTH)  // 分类有30中，但是分类的索引只能是0-29
                        bin=0;
                    assert(bin>=0 && bin<HISTO_LENGTH);
                    rotHist[bin].push_back(i1);
                }
            }
        }
    }// 至此，对1中的每一个特征点，都在2中为其找了一个该特征点周围区域内的一个最佳匹配

    // Step 6 筛除旋转直方图中“非主流”部分
    if(mbCheckOrientation)
    {
        int ind1=-1;
        int ind2=-1;
        int ind3=-1;
        // 筛选出在旋转角度差落在在直方图区间内数量最多的前三个bin的索引
        ComputeThreeMaxima(rotHist,HISTO_LENGTH,ind1,ind2,ind3);

        for(int i=0; i<HISTO_LENGTH; i++)
        {
            if(i==ind1 || i==ind2 || i==ind3)
                continue;
            // 剔除掉不在前三的匹配对，因为他们不符合“主流旋转方向”    
            for(size_t j=0, jend=rotHist[i].size(); j<jend; j++)
            {
                int idx1 = rotHist[i][j];  // 选出这个直方图中的一个匹配关系的索引
                if(vnMatches12[idx1]>=0)   // 如果这个有匹配关系
                {
                    vnMatches12[idx1]=-1;   // 注意这里只删除F1中的特征匹配索引即可，因为前面定义的F2中的特征匹配索引只是为了防止和F1中的特征点重复匹配
                    nmatches--;
                }
            }
        }

    }

    //Update prev matched
    // Step 7 将最后通过筛选的匹配好的特征点保存到vbPrevMatched
    for(size_t i1=0, iend1=vnMatches12.size(); i1<iend1; i1++)
        if(vnMatches12[i1]>=0)  // 不知道这个赋值有什么意义，这样只是把1中特征点坐标改成了2中匹配的特征点坐标，变成了1.2坐标的混合
            vbPrevMatched[i1]=F2.mvKeysUn[vnMatches12[i1]].pt;

    return nmatches;
}

/*
 * @brief 通过词袋，对关键帧的特征点进行跟踪，该函数用于闭环检测时两个关键帧间的特征点匹配
 * @details 通过bow对pKF和F中的特征点进行快速匹配（不属于同一node的特征点直接跳过匹配） 
 * 对属于同一node的特征点通过描述子距离进行匹配 
 * 通过距离阈值、比例阈值和角度投票进行剔除误匹配
 * @param  pKF1               KeyFrame1
 * @param  pKF2               KeyFrame2
 * @param  vpMatches12        pKF2中与pKF1匹配的MapPoint，vpMatches12[i]表示匹配的地图点，null表示没有匹配，i表示匹配的pKF1 特征点索引
 * @return                    成功匹配的数量
 */
//Done
int ORBmatcher::SearchByBoW(KeyFrame *pKF1, KeyFrame *pKF2, vector<MapPoint *> &vpMatches12)
{
    // Step 1 分别取出两个关键帧的特征点、BoW 向量、地图点、描述子
    const vector<cv::KeyPoint> &vKeysUn1 = pKF1->mvKeysUn;
    const DBoW2::FeatureVector &vFeatVec1 = pKF1->mFeatVec;
    const vector<MapPoint*> vpMapPoints1 = pKF1->GetMapPointMatches();
    const cv::Mat &Descriptors1 = pKF1->mDescriptors;

    const vector<cv::KeyPoint> &vKeysUn2 = pKF2->mvKeysUn;
    const DBoW2::FeatureVector &vFeatVec2 = pKF2->mFeatVec;
    const vector<MapPoint*> vpMapPoints2 = pKF2->GetMapPointMatches();
    const cv::Mat &Descriptors2 = pKF2->mDescriptors;

    // 保存匹配结果
    vpMatches12 = vector<MapPoint*>(vpMapPoints1.size(),static_cast<MapPoint*>(NULL));
    vector<bool> vbMatched2(vpMapPoints2.size(),false);

    // Step 2 构建旋转直方图，HISTO_LENGTH = 30
    vector<int> rotHist[HISTO_LENGTH];
    for(int i=0;i<HISTO_LENGTH;i++)
        rotHist[i].reserve(500);

    //! 原作者代码是 const float factor = 1.0f/HISTO_LENGTH; 是错误的，更改为下面代码   
    const float factor = HISTO_LENGTH/360.0f;

    int nmatches = 0;

    DBoW2::FeatureVector::const_iterator f1it = vFeatVec1.begin();
    DBoW2::FeatureVector::const_iterator f2it = vFeatVec2.begin();
    DBoW2::FeatureVector::const_iterator f1end = vFeatVec1.end();
    DBoW2::FeatureVector::const_iterator f2end = vFeatVec2.end();

    while(f1it != f1end && f2it != f2end)
    {
        // Step 3 开始遍历，分别取出属于同一node的特征点(只有属于同一node，才有可能是匹配点)
        if(f1it->first == f2it->first)
        {
            // 遍历KF中属于该node的特征点
            for(size_t i1=0, iend1=f1it->second.size(); i1<iend1; i1++)
            {
                const size_t idx1 = f1it->second[i1];

                MapPoint* pMP1 = vpMapPoints1[idx1];
                if(!pMP1)
                    continue;
                if(pMP1->isBad())
                    continue;

                const cv::Mat &d1 = Descriptors1.row(idx1);

                int bestDist1=256;
                int bestIdx2 =-1 ;
                int bestDist2=256;

                // Step 4 遍历KF2中属于该node的特征点，找到了最优及次优匹配点
                for(size_t i2=0, iend2=f2it->second.size(); i2<iend2; i2++)
                {
                    const size_t idx2 = f2it->second[i2];

                    MapPoint* pMP2 = vpMapPoints2[idx2];

                    // 如果已经有匹配的点，或者遍历到的特征点对应的地图点无效
                    if(vbMatched2[idx2] || !pMP2)
                        continue;

                    if(pMP2->isBad())
                        continue;

                    const cv::Mat &d2 = Descriptors2.row(idx2);

                    int dist = DescriptorDistance(d1,d2);

                    if(dist<bestDist1)
                    {
                        bestDist2=bestDist1;
                        bestDist1=dist;
                        bestIdx2=idx2;
                    }
                    else if(dist<bestDist2)
                    {
                        bestDist2=dist;
                    }
                }

                // Step 5 对匹配结果进行检查，满足阈值、最优/次优比例，记录旋转直方图信息
                if(bestDist1<TH_LOW)
                {
                    if(static_cast<float>(bestDist1)<mfNNratio*static_cast<float>(bestDist2))
                    {
                        vpMatches12[idx1]=vpMapPoints2[bestIdx2];
                        vbMatched2[bestIdx2]=true;

                        if(mbCheckOrientation)
                        {
                            float rot = vKeysUn1[idx1].angle-vKeysUn2[bestIdx2].angle;
                            if(rot<0.0)
                                rot+=360.0f;
                            int bin = round(rot*factor);
                            if(bin==HISTO_LENGTH)
                                bin=0;
                            assert(bin>=0 && bin<HISTO_LENGTH);
                            rotHist[bin].push_back(idx1);
                        }
                        nmatches++;
                    }
                }
            }

            f1it++;
            f2it++;
        }
        else if(f1it->first < f2it->first)
        {
            f1it = vFeatVec1.lower_bound(f2it->first);
        }
        else
        {
            f2it = vFeatVec2.lower_bound(f1it->first);
        }
    }

    // Step 6 检查旋转直方图分布，剔除差异较大的匹配
    if(mbCheckOrientation)
    {
        int ind1=-1;
        int ind2=-1;
        int ind3=-1;

        ComputeThreeMaxima(rotHist,HISTO_LENGTH,ind1,ind2,ind3);

        for(int i=0; i<HISTO_LENGTH; i++)
        {
            if(i==ind1 || i==ind2 || i==ind3)
                continue;
            for(size_t j=0, jend=rotHist[i].size(); j<jend; j++)
            {
                vpMatches12[rotHist[i][j]]=static_cast<MapPoint*>(NULL);
                nmatches--;
            }
        }
    }

    return nmatches;
}

/*
 * @brief 利用基础矩阵F12极线约束，用BoW加速匹配两个关键帧的未匹配的特征点，产生新的匹配点对
 * 具体来说，pKF1图像的每个特征点与pKF2图像同一node节点的所有特征点依次匹配，判断是否满足对极几何约束，满足约束就是匹配的特征点
 * @param pKF1          关键帧1
 * @param pKF2          关键帧2
 * @param F12           从2到1的基础矩阵
 * @param vMatchedPairs 存储匹配特征点对，特征点用其在关键帧中的索引表示
 * @param bOnlyStereo   在双目和rgbd情况下，是否要求特征点在右图存在匹配
 * @return              成功匹配的数量
 */
int ORBmatcher::SearchForTriangulation(KeyFrame *pKF1, KeyFrame *pKF2, cv::Mat F12,
                                       vector<pair<size_t, size_t> > &vMatchedPairs, const bool bOnlyStereo)
{
    const DBoW2::FeatureVector &vFeatVec1 = pKF1->mFeatVec;
    const DBoW2::FeatureVector &vFeatVec2 = pKF2->mFeatVec;

    // Compute epipole in second image
    // Step 1 计算KF1的相机中心在KF2图像平面的二维像素坐标
    // KF1相机光心在世界坐标系坐标Cw
    cv::Mat Cw = pKF1->GetCameraCenter(); 
    // KF2相机位姿R2w,t2w,是世界坐标系到相机坐标系
    cv::Mat R2w = pKF2->GetRotation();    
    cv::Mat t2w = pKF2->GetTranslation(); 
    // KF1的相机光心转化到KF2坐标系中的坐标
    cv::Mat C2 = R2w*Cw+t2w; 
    const float invz = 1.0f/C2.at<float>(2);
    // 得到KF1的相机光心在KF2中的坐标，也叫极点，这里是像素坐标
    const float ex =pKF2->fx*C2.at<float>(0)*invz+pKF2->cx;
    const float ey =pKF2->fy*C2.at<float>(1)*invz+pKF2->cy;

    // Find matches between not tracked keypoints
    // Matching speed-up by ORB Vocabulary
    // Compare only ORB that share the same node

    int nmatches=0;
    // 记录匹配是否成功，避免重复匹配
    vector<bool> vbMatched2(pKF2->N,false);     //; 第二个关键帧中的特征点是否已经找到在第一个关键帧中的特征点匹配 
    vector<int> vMatches12(pKF1->N,-1);
    // 用于统计匹配点对旋转差的直方图
    vector<int> rotHist[HISTO_LENGTH];
    for(int i=0;i<HISTO_LENGTH;i++)
        rotHist[i].reserve(500);

     //! 原作者代码是 const float factor = 1.0f/HISTO_LENGTH; 是错误的，更改为下面代码   
    const float factor = HISTO_LENGTH/360.0f;

    // We perform the matching over ORB that belong to the same vocabulary node (at a certain level)
    // Step 2 利用BoW加速匹配：只对属于同一节点(特定层)的ORB特征进行匹配
    // FeatureVector其实就是一个map类，那就可以直接获取它的迭代器进行遍历
    // FeatureVector的数据结构类似于：{(node1,feature_vector1) (node2,feature_vector2)...}
    // f1it->first对应node编号，f1it->second对应属于该node的所有特特征点编号
    DBoW2::FeatureVector::const_iterator f1it = vFeatVec1.begin();
    DBoW2::FeatureVector::const_iterator f2it = vFeatVec2.begin();
    DBoW2::FeatureVector::const_iterator f1end = vFeatVec1.end();
    DBoW2::FeatureVector::const_iterator f2end = vFeatVec2.end();

    // Step 2.1：遍历pKF1和pKF2中的node节点
    while(f1it!=f1end && f2it!=f2end)
    {
        // 如果f1it和f2it属于同一个node节点才会进行匹配，这就是BoW加速匹配原理
        if(f1it->first == f2it->first)  // node相同
        {
            // Step 2.2：遍历属于同一node节点(id：f1it->first)下的所有特征点
            for(size_t i1=0, iend1=f1it->second.size(); i1<iend1; i1++) // 遍历这个node下KF1中的特征点
            {
                // 获取pKF1中属于该node节点的所有特征点索引
                const size_t idx1 = f1it->second[i1];
                
                // Step 2.3：通过特征点索引idx1在pKF1中取出对应的MapPoint
                MapPoint* pMP1 = pKF1->GetMapPoint(idx1);
                
                // If there is already a MapPoint skip
                // 由于寻找的是未匹配的特征点，所以pMP1应该为NULL
                if(pMP1)
                    continue;

                // 如果mvuRight中的值大于0，表示是双目，且该特征点有深度值
                //; 对于单目，是false
                const bool bStereo1 = pKF1->mvuRight[idx1]>=0;

                if(bOnlyStereo)
                    if(!bStereo1)
                        continue;
                
                // Step 2.4：通过特征点索引idx1在pKF1中取出对应的特征点
                const cv::KeyPoint &kp1 = pKF1->mvKeysUn[idx1];
                
                // 通过特征点索引idx1在pKF1中取出对应的特征点的描述子
                const cv::Mat &d1 = pKF1->mDescriptors.row(idx1);
                
                int bestDist = TH_LOW;
                int bestIdx2 = -1;
                
                // Step 2.5：遍历该node节点下(f2it->first)对应KF2中的所有特征点
                for(size_t i2=0, iend2=f2it->second.size(); i2<iend2; i2++)
                {
                    // 获取pKF2中属于该node节点的所有特征点索引
                    size_t idx2 = f2it->second[i2];
                    
                    // 通过特征点索引idx2在pKF2中取出对应的MapPoint
                    MapPoint* pMP2 = pKF2->GetMapPoint(idx2);
                    
                    // If we have already matched or there is a MapPoint skip
                    // 如果pKF2当前特征点索引idx2已经被匹配过或者对应的3d点非空，那么跳过这个索引idx2
                    if(vbMatched2[idx2] || pMP2)
                        continue;

                    //; 对于单目，是false
                    const bool bStereo2 = pKF2->mvuRight[idx2]>=0;

                    if(bOnlyStereo)
                        if(!bStereo2)
                            continue;
                    
                    // 通过特征点索引idx2在pKF2中取出对应的特征点的描述子
                    const cv::Mat &d2 = pKF2->mDescriptors.row(idx2);
                    
                    // Step 2.6 计算idx1与idx2在两个关键帧中对应特征点的描述子距离
                    const int dist = DescriptorDistance(d1,d2);
                    
                    if(dist>TH_LOW || dist>bestDist)  //; 描述子距离不满足，或者这个距离大于最好的距离
                        continue;

                    // 通过特征点索引idx2在pKF2中取出对应的特征点
                    const cv::KeyPoint &kp2 = pKF2->mvKeysUn[idx2];

                    //? 为什么双目就不需要判断像素点到极点的距离的判断？
                    // 因为双目模式下可以左右互匹配恢复三维点
                    //; 如果是单目图像
                    if(!bStereo1 && !bStereo2)  //; 这两个关键帧中的特征点都不是双目。这么麻烦，直接判断传感器是不是双目不就行了？
                    {
                        const float distex = ex-kp2.pt.x;
                        const float distey = ey-kp2.pt.y;
                        // Step 2.7 极点e2到kp2的像素距离如果小于阈值th,认为kp2对应的MapPoint距离pKF1相机太近，跳过该匹配点对
                        // 作者根据kp2金字塔尺度因子(scale^n，scale=1.2，n为层数)定义阈值th
                        // 金字塔层数从0到7，对应距离 sqrt(100*pKF2->mvScaleFactors[kp2.octave]) 是10-20个像素
                        //? 对这个阈值的有效性持怀疑态度
                        if(distex*distex+distey*distey<100*pKF2->mvScaleFactors[kp2.octave])
                            continue;
                    }

                    // Step 2.8 计算特征点kp2到kp1对应极线的距离是否小于阈值
                    if(CheckDistEpipolarLine(kp1,kp2,F12,pKF2))
                    {
                        // bestIdx2，bestDist 是 kp1 对应 KF2中的最佳匹配点 index及匹配距离
                        bestIdx2 = idx2;
                        bestDist = dist;  //; 两个匹配的特征点的描述子的距离
                    }
                }

                
                if(bestIdx2>=0)
                {
                    const cv::KeyPoint &kp2 = pKF2->mvKeysUn[bestIdx2];
                    // 记录匹配结果
                    vMatches12[idx1]=bestIdx2;      
                    
                    vbMatched2[bestIdx2]=true;  // !记录已经匹配，避免重复匹配。原作者漏掉！
                    nmatches++;

                    // 记录旋转差直方图信息
                    if(mbCheckOrientation)
                    {
                        // angle：角度，表示匹配点对的方向差。
                        float rot = kp1.angle-kp2.angle;
                        if(rot<0.0)
                            rot+=360.0f;
                        int bin = round(rot*factor);
                        if(bin==HISTO_LENGTH)
                            bin=0;
                        assert(bin>=0 && bin<HISTO_LENGTH);   
                        rotHist[bin].push_back(idx1);
                    }
                }
            }

            f1it++;
            f2it++;
        }
        else if(f1it->first < f2it->first)
        {
            f1it = vFeatVec1.lower_bound(f2it->first);
        }
        else
        {
            f2it = vFeatVec2.lower_bound(f1it->first);
        }
    }

    // Step 3 用旋转差直方图来筛掉错误匹配对
    if(mbCheckOrientation)
    {
        int ind1=-1;
        int ind2=-1;
        int ind3=-1;

        ComputeThreeMaxima(rotHist,HISTO_LENGTH,ind1,ind2,ind3);

        for(int i=0; i<HISTO_LENGTH; i++)
        {
            if(i==ind1 || i==ind2 || i==ind3)
                continue;
            for(size_t j=0, jend=rotHist[i].size(); j<jend; j++)
            {              
                //; 不过其实这里这个变量已经没用了，清除与否没影响
                vbMatched2[vMatches12[rotHist[i][j]]] = false;  // !清除匹配关系。原作者漏掉！
                vMatches12[rotHist[i][j]]=-1;
                nmatches--;
            }
        }

    }

    // Step 4 存储匹配关系，下标是关键帧1的特征点id，存储的是关键帧2的特征点id
    vMatchedPairs.clear();
    vMatchedPairs.reserve(nmatches);

    for(size_t i=0, iend=vMatches12.size(); i<iend; i++)
    {
        if(vMatches12[i]<0)
            continue;
        vMatchedPairs.push_back(make_pair(i,vMatches12[i]));   // KF1中的第i个特征点和KF2中的第vMatches12[i]个特征点相互匹配
    }

    return nmatches;
}

/**
 * @brief 将地图点投影到关键帧中进行匹配和融合；融合策略如下
 * 1.如果地图点能匹配关键帧的特征点，并且该点有对应的地图点，那么选择观测数目多的替换两个地图点
 * 2.如果地图点能匹配关键帧的特征点，并且该点没有对应的地图点，那么为该点添加该投影地图点

 * @param[in] pKF           关键帧
 * @param[in] vpMapPoints   待投影的地图点
 * @param[in] th            搜索窗口的阈值，默认为3
 * @return int              更新地图点的数量
 */
int ORBmatcher::Fuse(KeyFrame *pKF, const vector<MapPoint *> &vpMapPoints, const float th)
{
    // 取出当前帧位姿、内参、光心在世界坐标系下坐标
    cv::Mat Rcw = pKF->GetRotation();
    cv::Mat tcw = pKF->GetTranslation();

    const float &fx = pKF->fx;
    const float &fy = pKF->fy;
    const float &cx = pKF->cx;
    const float &cy = pKF->cy;
    const float &bf = pKF->mbf;

    cv::Mat Ow = pKF->GetCameraCenter();

    int nFused=0;

    const int nMPs = vpMapPoints.size();

    // 遍历所有的待投影地图点
    for(int i=0; i<nMPs; i++)
    {
        
        MapPoint* pMP = vpMapPoints[i];
        // Step 1 判断地图点的有效性 
        if(!pMP)
            continue;
        // 地图点无效 或 已经是该帧的地图点（无需融合），跳过
        if(pMP->isBad() || pMP->IsInKeyFrame(pKF))
            continue;

        // 将地图点变换到关键帧的相机坐标系下
        cv::Mat p3Dw = pMP->GetWorldPos();
        cv::Mat p3Dc = Rcw*p3Dw + tcw;

        // Depth must be positive
        // 深度值为负，跳过
        if(p3Dc.at<float>(2)<0.0f)
            continue;

        // Step 2 得到地图点投影到关键帧的图像坐标
        const float invz = 1/p3Dc.at<float>(2);
        const float x = p3Dc.at<float>(0)*invz;
        const float y = p3Dc.at<float>(1)*invz;

        const float u = fx*x+cx;
        const float v = fy*y+cy;

        // Point must be inside the image
        // 投影点需要在有效范围内
        if(!pKF->IsInImage(u,v))
            continue;

        const float ur = u-bf*invz;

        const float maxDistance = pMP->GetMaxDistanceInvariance();
        const float minDistance = pMP->GetMinDistanceInvariance();
        cv::Mat PO = p3Dw-Ow;
        const float dist3D = cv::norm(PO);

        // Depth must be inside the scale pyramid of the image
        // Step 3 地图点到关键帧相机光心距离需满足在有效范围内
        if(dist3D<minDistance || dist3D>maxDistance )
            continue;

        // Viewing angle must be less than 60 deg
        // Step 4 地图点到光心的连线与该地图点的平均观测向量之间夹角要小于60°
        cv::Mat Pn = pMP->GetNormal();
        if(PO.dot(Pn)<0.5*dist3D)
            continue;
        // 根据地图点到相机光心距离预测匹配点所在的金字塔尺度
        int nPredictedLevel = pMP->PredictScale(dist3D,pKF);

        // Search in a radius
        // 确定搜索范围
        const float radius = th*pKF->mvScaleFactors[nPredictedLevel];
        // Step 5 在投影点附近搜索窗口内找到候选匹配点的索引
        const vector<size_t> vIndices = pKF->GetFeaturesInArea(u,v,radius);

        if(vIndices.empty())
            continue;

        // Match to the most similar keypoint in the radius
         // Step 6 遍历寻找最佳匹配点
        const cv::Mat dMP = pMP->GetDescriptor();

        int bestDist = 256;
        int bestIdx = -1;
        for(vector<size_t>::const_iterator vit=vIndices.begin(), vend=vIndices.end(); vit!=vend; vit++)// 步骤3：遍历搜索范围内的features
        {
            const size_t idx = *vit;

            const cv::KeyPoint &kp = pKF->mvKeysUn[idx];

            const int &kpLevel= kp.octave;
            // 金字塔层级要接近（同一层或小一层），否则跳过
            if(kpLevel<nPredictedLevel-1 || kpLevel>nPredictedLevel)
                continue;

            // 计算投影点与候选匹配特征点的距离，如果偏差很大，直接跳过
            if(pKF->mvuRight[idx]>=0)
            {
                // Check reprojection error in stereo
                // 双目情况
                const float &kpx = kp.pt.x;
                const float &kpy = kp.pt.y;
                const float &kpr = pKF->mvuRight[idx];
                const float ex = u-kpx;
                const float ey = v-kpy;
                // 右目数据的偏差也要考虑进去
                const float er = ur-kpr;        
                const float e2 = ex*ex+ey*ey+er*er;

                //自由度为3, 误差小于1个像素,这种事情95%发生的概率对应卡方检验阈值为7.82
                if(e2*pKF->mvInvLevelSigma2[kpLevel]>7.8)   
                    continue;
            }
            else
            {
                // 计算投影点与候选匹配特征点的距离，如果偏差很大，直接跳过
                // 单目情况
                const float &kpx = kp.pt.x;
                const float &kpy = kp.pt.y;
                const float ex = u-kpx;
                const float ey = v-kpy;
                const float e2 = ex*ex+ey*ey;

                // 自由度为2的，卡方检验阈值5.99（假设测量有一个像素的偏差）
                if(e2*pKF->mvInvLevelSigma2[kpLevel]>5.99)
                    continue;
            }

            const cv::Mat &dKF = pKF->mDescriptors.row(idx);

            const int dist = DescriptorDistance(dMP,dKF);
            // 和投影点的描述子距离最小
            if(dist<bestDist)
            {
                bestDist = dist;
                bestIdx = idx;
            }
        }

        // If there is already a MapPoint replace otherwise add new measurement
        // Step 7 找到投影点对应的最佳匹配特征点，根据是否存在地图点来融合或新增
        // 最佳匹配距离要小于阈值
        if(bestDist<=TH_LOW)
        {
            MapPoint* pMPinKF = pKF->GetMapPoint(bestIdx);
            if(pMPinKF)
            {
                // 如果最佳匹配点有对应有效地图点，选择被观测次数最多的那个替换
                if(!pMPinKF->isBad())
                {
                    if(pMPinKF->Observations()>pMP->Observations())
                        pMP->Replace(pMPinKF);
                    else
                        pMPinKF->Replace(pMP);
                }
            }
            else
            {
                // 如果最佳匹配点没有对应地图点，添加观测信息
                pMP->AddObservation(pKF,bestIdx);
                pKF->AddMapPoint(pMP,bestIdx);
            }
            nFused++;
        }
    }

    return nFused;
}


/**
 * @brief 闭环矫正中使用。将当前关键帧闭环匹配上的关键帧及其共视关键帧组成的地图点投影到当前关键帧，融合地图点
 * 
 * @param[in] pKF                   当前关键帧
 * @param[in] Scw                   当前关键帧经过闭环Sim3 后的世界到相机坐标系的Sim变换
 * @param[in] vpPoints              与当前关键帧闭环匹配上的关键帧及其共视关键帧组成的地图点
 * @param[in] th                    搜索范围系数
 * @param[out] vpReplacePoint       替换的地图点
 * @return int                      融合（替换和新增）的地图点数目
 */
int ORBmatcher::Fuse(KeyFrame *pKF, cv::Mat Scw, const vector<MapPoint *> &vpPoints, float th, vector<MapPoint *> &vpReplacePoint)
{
    // Get Calibration Parameters for later projection
    const float &fx = pKF->fx;
    const float &fy = pKF->fy;
    const float &cx = pKF->cx;
    const float &cy = pKF->cy;

    // Decompose Scw
    // Step 1 将Sim3转化为SE3并分解
    cv::Mat sRcw = Scw.rowRange(0,3).colRange(0,3);
    const float scw = sqrt(sRcw.row(0).dot(sRcw.row(0)));// 计算得到尺度s
    cv::Mat Rcw = sRcw/scw;// 除掉s
    cv::Mat tcw = Scw.rowRange(0,3).col(3)/scw;// 除掉s
    cv::Mat Ow = -Rcw.t()*tcw;

    // Set of MapPoints already found in the KeyFrame
    // 当前帧已有的匹配地图点
    //; 当前帧的共视关键帧，创建的时候就有的地图点
    const set<MapPoint*> spAlreadyFound = pKF->GetMapPoints();

    int nFused=0;
    // 与当前帧闭环匹配上的关键帧及其共视关键帧组成的地图点
    const int nPoints = vpPoints.size();

    // For each candidate MapPoint project and match
    // 遍历所有的地图点
    for(int iMP=0; iMP<nPoints; iMP++)
    {
        MapPoint* pMP = vpPoints[iMP];

        // Discard Bad MapPoints and already found
        // 地图点无效 或 已经是该帧的地图点（无需融合），跳过
        if(pMP->isBad() || spAlreadyFound.count(pMP))
            continue;

        // Get 3D Coords.
        // Step 2 地图点变换到当前相机坐标系下
        cv::Mat p3Dw = pMP->GetWorldPos();

        // Transform into Camera Coords.
        //; 这一步比较重要，是把闭环候选帧及其共视关键帧的所有地图点（世界坐标是准确的，因为这些关键帧是在系统初始的时候创建的）
        //; 转换到校正后的当前帧的共视关键帧的相机坐标系下，注意最后这个3D坐标和使用sim3变换后的坐标差一个比例因子s。
        cv::Mat p3Dc = Rcw*p3Dw+tcw;

        // Depth must be positive
        if(p3Dc.at<float>(2)<0.0f)
            continue;

        // Project into Image
        // Step 3 得到地图点投影到当前帧的图像坐标
        const float invz = 1.0/p3Dc.at<float>(2);
        const float x = p3Dc.at<float>(0)*invz;
        const float y = p3Dc.at<float>(1)*invz;

        const float u = fx*x+cx;
        const float v = fy*y+cy;

        // Point must be inside the image
        // 投影点必须在图像范围内
        if(!pKF->IsInImage(u,v))
            continue;

        // Depth must be inside the scale pyramid of the image
        // Step 4 根据距离是否在图像合理金字塔尺度范围内和观测角度是否小于60度判断该地图点是否有效
        const float maxDistance = pMP->GetMaxDistanceInvariance();
        const float minDistance = pMP->GetMinDistanceInvariance();
        cv::Mat PO = p3Dw-Ow;
        const float dist3D = cv::norm(PO);

        if(dist3D<minDistance || dist3D>maxDistance)
            continue;

        // Viewing angle must be less than 60 deg
        cv::Mat Pn = pMP->GetNormal();

        if(PO.dot(Pn)<0.5*dist3D)
            continue;

        // Compute predicted scale level
        const int nPredictedLevel = pMP->PredictScale(dist3D,pKF);

        // Search in a radius
        // 计算搜索范围
        const float radius = th*pKF->mvScaleFactors[nPredictedLevel];

        // Step 5 在当前帧内搜索匹配候选点
        const vector<size_t> vIndices = pKF->GetFeaturesInArea(u,v,radius);

        if(vIndices.empty())
            continue;

        // Match to the most similar keypoint in the radius
        // Step 6 寻找最佳匹配点（没有用到次佳匹配的比例）
        const cv::Mat dMP = pMP->GetDescriptor();

        int bestDist = INT_MAX;
        int bestIdx = -1;
        for(vector<size_t>::const_iterator vit=vIndices.begin(); vit!=vIndices.end(); vit++)
        {
            const size_t idx = *vit;
            const int &kpLevel = pKF->mvKeysUn[idx].octave;

            if(kpLevel<nPredictedLevel-1 || kpLevel>nPredictedLevel)
                continue;

            const cv::Mat &dKF = pKF->mDescriptors.row(idx);

            int dist = DescriptorDistance(dMP,dKF);

            if(dist<bestDist)
            {
                bestDist = dist;
                bestIdx = idx;
            }
        }

        // If there is already a MapPoint replace otherwise add new measurement
        // Step 7 替换或新增地图点
        if(bestDist<=TH_LOW)
        {
            MapPoint* pMPinKF = pKF->GetMapPoint(bestIdx);
            if(pMPinKF)
            {
                // 如果这个地图点已经存在，则记录要替换信息
                // 这里不能直接替换，原因是需要对地图点加锁后才能替换，否则可能会crash。所以先记录，在加锁后替换
                if(!pMPinKF->isBad())
                    vpReplacePoint[iMP] = pMPinKF;
            }
            else
            {
                // 如果这个地图点不存在，直接添加
                pMP->AddObservation(pKF,bestIdx);
                pKF->AddMapPoint(pMP,bestIdx);
            }
            nFused++;
        }
    }
    // 融合（替换和新增）的地图点数目
    return nFused;
}

/**
 * @brief 通过Sim3变换，搜索两个关键帧中更多的匹配点对
 * （之前使用SearchByBoW进行特征点匹配时会有漏匹配）
 * @param[in] pKF1              当前帧
 * @param[in] pKF2              闭环候选帧
 * @param[in] vpMatches12       i表示匹配的pKF1 特征点索引，vpMatches12[i]表示匹配的地图点，null表示没有匹配
 * @param[in] s12               pKF2 到 pKF1 的Sim 变换中的尺度
 * @param[in] R12               pKF2 到 pKF1 的Sim 变换中的旋转矩阵
 * @param[in] t12               pKF2 到 pKF1 的Sim 变换中的平移向量
 * @param[in] th                搜索窗口的倍数
 * @return int                  新增的匹配点对数目
 */
int ORBmatcher::SearchBySim3(KeyFrame *pKF1, KeyFrame *pKF2, vector<MapPoint*> &vpMatches12,
                             const float &s12, const cv::Mat &R12, const cv::Mat &t12, const float th)
{
    // Step 1： 准备工作：内参，计算Sim3的逆
    const float &fx = pKF1->fx;
    const float &fy = pKF1->fy;
    const float &cx = pKF1->cx;
    const float &cy = pKF1->cy;

    // Camera 1 from world
    // 从world到camera1的变换
    cv::Mat R1w = pKF1->GetRotation();
    cv::Mat t1w = pKF1->GetTranslation();

    //Camera 2 from world
    // 从world到camera2的变换
    cv::Mat R2w = pKF2->GetRotation();
    cv::Mat t2w = pKF2->GetTranslation();

    //Transformation between cameras
    // Sim3 的逆
    cv::Mat sR12 = s12*R12;
    cv::Mat sR21 = (1.0/s12)*R12.t();
    cv::Mat t21 = -sR21*t12;

    // 取出关键帧中的地图点
    const vector<MapPoint*> vpMapPoints1 = pKF1->GetMapPointMatches();
    const int N1 = vpMapPoints1.size();

    const vector<MapPoint*> vpMapPoints2 = pKF2->GetMapPointMatches();
    const int N2 = vpMapPoints2.size();

    // 记录pKF1，pKF2中已经匹配的特征点，已经匹配记为true，否则false
    vector<bool> vbAlreadyMatched1(N1,false);
    vector<bool> vbAlreadyMatched2(N2,false);

    // Step 2：记录已经匹配的特征点
    for(int i=0; i<N1; i++)
    {
        MapPoint* pMP = vpMatches12[i];
        if(pMP)
        {
            // pKF1中第i个特征点已经匹配成功
            vbAlreadyMatched1[i]=true;
            // 得到该地图点在关键帧pkF2 中的id
            int idx2 = pMP->GetIndexInKeyFrame(pKF2);
            if(idx2>=0 && idx2<N2)
                // pKF2中第idx2个特征点在pKF1中有匹配
                vbAlreadyMatched2[idx2]=true;   
        }
    }

    vector<int> vnMatch1(N1,-1);
    vector<int> vnMatch2(N2,-1);

    // Transform from KF1 to KF2 and search
    // Step 3：通过Sim变换，寻找 pKF1 中特征点和 pKF2 中的新的匹配
    // 之前使用SearchByBoW进行特征点匹配时会有漏匹配
    for(int i1=0; i1<N1; i1++)
    {
        MapPoint* pMP = vpMapPoints1[i1];

        // 该特征点存在对应的地图点或者该特征点已经有匹配点了，跳过
        if(!pMP || vbAlreadyMatched1[i1])
            continue;
        // 地图点是要删掉的，跳过
        if(pMP->isBad())
            continue;

        // Step 3.1：通过Sim变换，将pKF1的地图点投影到pKF2中的图像坐标
        cv::Mat p3Dw = pMP->GetWorldPos();
        // 把pKF1的地图点从world坐标系变换到camera1坐标系
        cv::Mat p3Dc1 = R1w*p3Dw + t1w;
        // 再通过Sim3将该地图点从camera1变换到camera2坐标系
        cv::Mat p3Dc2 = sR21*p3Dc1 + t21;

        // 深度值为负，跳过
        if(p3Dc2.at<float>(2)<0.0)
            continue;

        // 投影到camera2图像坐标 (u,v)
        const float invz = 1.0/p3Dc2.at<float>(2);
        const float x = p3Dc2.at<float>(0)*invz;
        const float y = p3Dc2.at<float>(1)*invz;

        const float u = fx*x+cx;
        const float v = fy*y+cy;

        // Point must be inside the image
        // 投影点必须在图像范围内，否则跳过
        if(!pKF2->IsInImage(u,v))
            continue;

        const float maxDistance = pMP->GetMaxDistanceInvariance();
        const float minDistance = pMP->GetMinDistanceInvariance();
        const float dist3D = cv::norm(p3Dc2);

        // Depth must be inside the scale invariance region
        // 深度值在有效范围内
        if(dist3D<minDistance || dist3D>maxDistance )
            continue;

        // Compute predicted octave
        // Step 3.2：预测投影的点在图像金字塔哪一层
        const int nPredictedLevel = pMP->PredictScale(dist3D,pKF2);

        // Search in a radius
        // 计算特征点搜索半径
        const float radius = th*pKF2->mvScaleFactors[nPredictedLevel];

        // Step 3.3：搜索该区域内的所有候选匹配特征点
        const vector<size_t> vIndices = pKF2->GetFeaturesInArea(u,v,radius);

        if(vIndices.empty())
            continue;

        // Match to the most similar keypoint in the radius
        const cv::Mat dMP = pMP->GetDescriptor();

        int bestDist = INT_MAX;
        int bestIdx = -1;
        // Step 3.4：遍历所有候选特征点，寻找最佳匹配点（并未使用次佳最佳比例约束）
        for(vector<size_t>::const_iterator vit=vIndices.begin(), vend=vIndices.end(); vit!=vend; vit++)
        {
            const size_t idx = *vit;

            const cv::KeyPoint &kp = pKF2->mvKeysUn[idx];

            if(kp.octave<nPredictedLevel-1 || kp.octave>nPredictedLevel)
                continue;

            const cv::Mat &dKF = pKF2->mDescriptors.row(idx);

            const int dist = DescriptorDistance(dMP,dKF);

            if(dist<bestDist)
            {
                bestDist = dist;
                bestIdx = idx;
            }
        }

        if(bestDist<=TH_HIGH)
        {
            vnMatch1[i1]=bestIdx;
        }
    }

    // Transform from KF2 to KF1 and search
    // Step 4：通过Sim变换，寻找 pKF2 中特征点和 pKF1 中的新的匹配
    // 具体步骤同上
    for(int i2=0; i2<N2; i2++)
    {
        MapPoint* pMP = vpMapPoints2[i2];

        if(!pMP || vbAlreadyMatched2[i2])
            continue;

        if(pMP->isBad())
            continue;

        cv::Mat p3Dw = pMP->GetWorldPos();
        cv::Mat p3Dc2 = R2w*p3Dw + t2w;
        cv::Mat p3Dc1 = sR12*p3Dc2 + t12;

        // Depth must be positive
        if(p3Dc1.at<float>(2)<0.0)
            continue;

        const float invz = 1.0/p3Dc1.at<float>(2);
        const float x = p3Dc1.at<float>(0)*invz;
        const float y = p3Dc1.at<float>(1)*invz;

        const float u = fx*x+cx;
        const float v = fy*y+cy;

        // Point must be inside the image
        if(!pKF1->IsInImage(u,v))
            continue;

        const float maxDistance = pMP->GetMaxDistanceInvariance();
        const float minDistance = pMP->GetMinDistanceInvariance();
        const float dist3D = cv::norm(p3Dc1);

        // Depth must be inside the scale pyramid of the image
        if(dist3D<minDistance || dist3D>maxDistance)
            continue;

        // Compute predicted octave
        const int nPredictedLevel = pMP->PredictScale(dist3D,pKF1);

        // Search in a radius of 2.5*sigma(ScaleLevel)
        const float radius = th*pKF1->mvScaleFactors[nPredictedLevel];

        const vector<size_t> vIndices = pKF1->GetFeaturesInArea(u,v,radius);

        if(vIndices.empty())
            continue;

        // Match to the most similar keypoint in the radius
        const cv::Mat dMP = pMP->GetDescriptor();

        int bestDist = INT_MAX;
        int bestIdx = -1;
        for(vector<size_t>::const_iterator vit=vIndices.begin(), vend=vIndices.end(); vit!=vend; vit++)
        {
            const size_t idx = *vit;

            const cv::KeyPoint &kp = pKF1->mvKeysUn[idx];

            if(kp.octave<nPredictedLevel-1 || kp.octave>nPredictedLevel)
                continue;

            const cv::Mat &dKF = pKF1->mDescriptors.row(idx);

            const int dist = DescriptorDistance(dMP,dKF);

            if(dist<bestDist)
            {
                bestDist = dist;
                bestIdx = idx;
            }
        }

        if(bestDist<=TH_HIGH)
        {
            vnMatch2[i2]=bestIdx;
        }
    }

    // Check agreement
    // Step 5： 一致性检查,只有在两次互相匹配中都出现才能够认为是可靠的匹配
    int nFound = 0;

    for(int i1=0; i1<N1; i1++)
    {
        int idx2 = vnMatch1[i1];

        if(idx2>=0)
        {
            int idx1 = vnMatch2[idx2];
            if(idx1==i1)
            {
                // 更新匹配的地图点
                vpMatches12[i1] = vpMapPoints2[idx2];
                nFound++;
            }
        }
    }

    return nFound;
}

/**
 * @brief 将上一帧跟踪的地图点投影到当前帧，并且搜索匹配点。用于跟踪前一帧
 * 步骤
 * Step 1 建立旋转直方图，用于检测旋转一致性
 * Step 2 计算当前帧和前一帧的平移向量
 * Step 3 对于前一帧的每一个地图点，通过相机投影模型，得到投影到当前帧的像素坐标
 * Step 4 根据相机的前后前进方向来判断搜索尺度范围
 * Step 5 遍历候选匹配点，寻找距离最小的最佳匹配点 
 * Step 6 计算匹配点旋转角度差所在的直方图
 * Step 7 进行旋转一致检测，剔除不一致的匹配
 * @param[in] CurrentFrame          当前帧
 * @param[in] LastFrame             上一帧
 * @param[in] th                    搜索范围阈值，默认单目为7，双目15
 * @param[in] bMono                 是否为单目
 * @return int                      成功匹配的数量
 */
//Done  注意这里是给恒速模型跟踪的时候，从上一帧的地图点中投影到当前帧的图像中
int ORBmatcher::SearchByProjection(Frame &CurrentFrame, const Frame &LastFrame, const float th, const bool bMono)
{
    int nmatches = 0;

    // Rotation Histogram (to check rotation consistency)
    // Step 1 建立旋转直方图，用于检测旋转一致性
    vector<int> rotHist[HISTO_LENGTH];
    for(int i=0;i<HISTO_LENGTH;i++)
        rotHist[i].reserve(500);

    //! 原作者代码是 const float factor = 1.0f/HISTO_LENGTH; 是错误的，更改为下面代码
    const float factor = HISTO_LENGTH/360.0f;

    // Step 2 计算当前帧和前一帧的平移向量
    //当前帧的相机位姿
    const cv::Mat Rcw = CurrentFrame.mTcw.rowRange(0,3).colRange(0,3);
    const cv::Mat tcw = CurrentFrame.mTcw.rowRange(0,3).col(3);

    //当前相机坐标系在世界坐标系下的平移向量
    const cv::Mat twc = -Rcw.t()*tcw; 

    //上一帧的相机位姿
    const cv::Mat Rlw = LastFrame.mTcw.rowRange(0,3).colRange(0,3);
    const cv::Mat tlw = LastFrame.mTcw.rowRange(0,3).col(3); // tlw(l)

    // vector from LastFrame to CurrentFrame expressed in LastFrame
    // 当前帧相对于上一帧相机的平移向量，主要就是为了计算相机实在前进还是后退
    const cv::Mat tlc = Rlw*twc+tlw; 

    // 判断前进还是后退。感觉这个判断前进后退的阈值比较玄学，为什么就选了基线距离？
    // 对单目来说，这两个变量都是false，也就是没有判断前进还是后退
    const bool bForward = tlc.at<float>(2) > CurrentFrame.mb && !bMono;     // 非单目情况，如果Z大于基线，则表示相机明显前进
    const bool bBackward = -tlc.at<float>(2) > CurrentFrame.mb && !bMono;   // 非单目情况，如果-Z小于基线，则表示相机明显后退

    //  Step 3 对于前一帧的每一个地图点，通过相机投影模型，得到投影到当前帧的像素坐标
    for(int i=0; i<LastFrame.N; i++)
    {
        MapPoint* pMP = LastFrame.mvpMapPoints[i];

        if(pMP)  // 如果上一帧的特征点对应有地图点
        {
            if(!LastFrame.mvbOutlier[i]) // 并且这个地图点不是外点
            {
                // 对上一帧有效的MapPoints投影到当前帧坐标系
                cv::Mat x3Dw = pMP->GetWorldPos();  // 地图点的世界坐标
                cv::Mat x3Dc = Rcw*x3Dw+tcw;  // 把地图点转换到当前帧的坐标系下

                const float xc = x3Dc.at<float>(0);
                const float yc = x3Dc.at<float>(1);
                const float invzc = 1.0/x3Dc.at<float>(2);

                if(invzc<0)   // 转换之后深度<0，说明这个地图点在当前帧坐标系下的相机成像平面后边，不合法
                    continue;

                // 投影到当前帧中
                float u = CurrentFrame.fx*xc*invzc+CurrentFrame.cx;
                float v = CurrentFrame.fy*yc*invzc+CurrentFrame.cy;

                // 投影之后像素坐标超过了图像范围
                if(u<CurrentFrame.mnMinX || u>CurrentFrame.mnMaxX)
                    continue;
                if(v<CurrentFrame.mnMinY || v>CurrentFrame.mnMaxY)
                    continue;

                // 上一帧中地图点对应二维特征点所在的金字塔层级
                int nLastOctave = LastFrame.mvKeys[i].octave;

                // Search in a window. Size depends on scale
                // 单目：th = 7，双目：th = 15
                // 金字塔层级越高，那么搜索的原型区域半径就越小
                float radius = th*CurrentFrame.mvScaleFactors[nLastOctave]; // 尺度越大，搜索范围越大

                // 记录候选匹配点的id
                vector<size_t> vIndices2;         

                // Step 4 根据相机的前后前进方向来判断搜索尺度范围。
                // 以下可以这么理解，例如一个有一定面积的圆点，在某个尺度n下它是一个特征点
                // 当相机前进时，圆点的面积增大，在某个尺度m下它是一个特征点，由于面积增大，则需要在更高的尺度下才能检测出来
                // 当相机后退时，圆点的面积减小，在某个尺度m下它是一个特征点，由于面积减小，则需要在更低的尺度下才能检测出来
                if(bForward) // 前进,则上一帧兴趣点在所在的尺度nLastOctave<=nCurOctave
                    vIndices2 = CurrentFrame.GetFeaturesInArea(u,v, radius, nLastOctave);
                else if(bBackward) // 后退,则上一帧兴趣点在所在的尺度0<=nCurOctave<=nLastOctave
                    vIndices2 = CurrentFrame.GetFeaturesInArea(u,v, radius, 0, nLastOctave);
                else // 在[nLastOctave-1, nLastOctave+1]中搜索
                // 在当前金字塔上下各一层，共三层的圆形区域内搜索，返回的就是特征点的索引
                    vIndices2 = CurrentFrame.GetFeaturesInArea(u,v, radius, nLastOctave-1, nLastOctave+1);

                if(vIndices2.empty())
                    continue;

                const cv::Mat dMP = pMP->GetDescriptor();   // 得到这个地图点对应的描述子

                int bestDist = 256;
                int bestIdx2 = -1;

                // Step 5 遍历候选匹配点，寻找距离最小的最佳匹配点 
                for(vector<size_t>::const_iterator vit=vIndices2.begin(), vend=vIndices2.end(); vit!=vend; vit++)
                {
                    const size_t i2 = *vit;

                    // 如果该特征点已经有对应的MapPoint了,则退出该次循环
                    if(CurrentFrame.mvpMapPoints[i2])  // 这个特征点有地图点了
                        if(CurrentFrame.mvpMapPoints[i2]->Observations()>0)  // 并且这个地图点被观测的次数>0
                            continue;

                    if(CurrentFrame.mvuRight[i2]>0)
                    {
                        // 双目和rgbd的情况，需要保证右图的点也在搜索半径以内
                        const float ur = u - CurrentFrame.mbf*invzc;
                        const float er = fabs(ur - CurrentFrame.mvuRight[i2]);
                        if(er>radius)
                            continue;
                    }

                    const cv::Mat &d = CurrentFrame.mDescriptors.row(i2);  // 得到这个特征点的索引

                    const int dist = DescriptorDistance(dMP,d);  // 计算这个特征点的描述子和地图点的描述子之间的距离

                    if(dist<bestDist)
                    {
                        bestDist=dist;
                        bestIdx2=i2;
                    }
                }

                // 最佳匹配距离要小于设定阈值
                if(bestDist<=TH_HIGH)
                {
                    CurrentFrame.mvpMapPoints[bestIdx2]=pMP; 
                    nmatches++;

                    // Step 6 计算匹配点旋转角度差所在的直方图
                    if(mbCheckOrientation)
                    {
                        float rot = LastFrame.mvKeysUn[i].angle-CurrentFrame.mvKeysUn[bestIdx2].angle;
                        if(rot<0.0)
                            rot+=360.0f;
                        int bin = round(rot*factor);
                        if(bin==HISTO_LENGTH)
                            bin=0;
                        assert(bin>=0 && bin<HISTO_LENGTH);
                        rotHist[bin].push_back(bestIdx2);
                    }
                }
            }
        }
    }

    //Apply rotation consistency
    //  Step 7 进行旋转一致检测，剔除不一致的匹配
    if(mbCheckOrientation)
    {
        int ind1=-1;
        int ind2=-1;
        int ind3=-1;

        ComputeThreeMaxima(rotHist,HISTO_LENGTH,ind1,ind2,ind3);

        for(int i=0; i<HISTO_LENGTH; i++)
        {
            // 对于数量不是前3个的点对，剔除
            if(i!=ind1 && i!=ind2 && i!=ind3)
            {
                for(size_t j=0, jend=rotHist[i].size(); j<jend; j++)
                {
                    CurrentFrame.mvpMapPoints[rotHist[i][j]]=static_cast<MapPoint*>(NULL);
                    nmatches--;
                }
            }
        }
    }

    return nmatches;
}

/**
 * @brief 通过投影的方式将关键帧中未匹配的地图点投影到当前帧中,进行匹配，并通过旋转直方图进行筛选
 * 
 * @param[in] CurrentFrame          当前帧
 * @param[in] pKF                   参考关键帧
 * @param[in] sAlreadyFound         已经找到的地图点集合，不会用于PNP
 * @param[in] th                    匹配时搜索范围，会乘以金字塔尺度
 * @param[in] ORBdist               匹配的ORB描述子距离应该小于这个阈值    
 * @return int                      成功匹配的数量
 */
//Done
int ORBmatcher::SearchByProjection(Frame &CurrentFrame, KeyFrame *pKF, const set<MapPoint*> &sAlreadyFound, const float th , const int ORBdist)
{
    int nmatches = 0;

    const cv::Mat Rcw = CurrentFrame.mTcw.rowRange(0,3).colRange(0,3);
    const cv::Mat tcw = CurrentFrame.mTcw.rowRange(0,3).col(3);
    const cv::Mat Ow = -Rcw.t()*tcw;

    // Rotation Histogram (to check rotation consistency)
    // Step 1 建立旋转直方图，用于检测旋转一致性
    vector<int> rotHist[HISTO_LENGTH];
    for(int i=0;i<HISTO_LENGTH;i++)
        rotHist[i].reserve(500);
    const float factor = HISTO_LENGTH/360.0f;

    const vector<MapPoint*> vpMPs = pKF->GetMapPointMatches();

    // Step 2 遍历关键帧中的每个地图点，通过相机投影模型，得到投影到当前帧的像素坐标
    for(size_t i=0, iend=vpMPs.size(); i<iend; i++)
    {
        MapPoint* pMP = vpMPs[i];

        if(pMP)
        {
            // 地图点存在 并且 不在已有地图点集合里
            if(!pMP->isBad() && !sAlreadyFound.count(pMP))
            {
                //Project
                cv::Mat x3Dw = pMP->GetWorldPos();
                cv::Mat x3Dc = Rcw*x3Dw+tcw;

                const float xc = x3Dc.at<float>(0);
                const float yc = x3Dc.at<float>(1);
                const float invzc = 1.0/x3Dc.at<float>(2);

                const float u = CurrentFrame.fx*xc*invzc+CurrentFrame.cx;
                const float v = CurrentFrame.fy*yc*invzc+CurrentFrame.cy;

                if(u<CurrentFrame.mnMinX || u>CurrentFrame.mnMaxX)
                    continue;
                if(v<CurrentFrame.mnMinY || v>CurrentFrame.mnMaxY)
                    continue;

                // Compute predicted scale level
                cv::Mat PO = x3Dw-Ow;
                float dist3D = cv::norm(PO);

                const float maxDistance = pMP->GetMaxDistanceInvariance();
                const float minDistance = pMP->GetMinDistanceInvariance();

                // Depth must be inside the scale pyramid of the image
                if(dist3D<minDistance || dist3D>maxDistance)
                    continue;

                //预测尺度
                int nPredictedLevel = pMP->PredictScale(dist3D,&CurrentFrame);
                // Search in a window
                // 搜索半径和尺度相关
                const float radius = th*CurrentFrame.mvScaleFactors[nPredictedLevel];

                //  Step 3 搜索候选匹配点
                const vector<size_t> vIndices2 = CurrentFrame.GetFeaturesInArea(u, v, radius, nPredictedLevel-1, nPredictedLevel+1);

                if(vIndices2.empty())
                    continue;

                const cv::Mat dMP = pMP->GetDescriptor();

                int bestDist = 256;
                int bestIdx2 = -1;
                // Step 4 遍历候选匹配点，寻找距离最小的最佳匹配点 
                for(vector<size_t>::const_iterator vit=vIndices2.begin(); vit!=vIndices2.end(); vit++)
                {
                    const size_t i2 = *vit;
                    if(CurrentFrame.mvpMapPoints[i2])
                        continue;

                    const cv::Mat &d = CurrentFrame.mDescriptors.row(i2);

                    const int dist = DescriptorDistance(dMP,d);

                    if(dist<bestDist)
                    {
                        bestDist=dist;
                        bestIdx2=i2;
                    }
                }

                if(bestDist<=ORBdist)
                {
                    CurrentFrame.mvpMapPoints[bestIdx2]=pMP;
                    nmatches++;
                     // Step 5 计算匹配点旋转角度差所在的直方图
                    if(mbCheckOrientation)
                    {
                        float rot = pKF->mvKeysUn[i].angle-CurrentFrame.mvKeysUn[bestIdx2].angle;
                        if(rot<0.0)
                            rot+=360.0f;
                        int bin = round(rot*factor);
                        if(bin==HISTO_LENGTH)
                            bin=0;
                        assert(bin>=0 && bin<HISTO_LENGTH);
                        rotHist[bin].push_back(bestIdx2);
                    }
                }

            }
        }
    }

    //  Step 6 进行旋转一致检测，剔除不一致的匹配
    if(mbCheckOrientation)
    {
        int ind1=-1;
        int ind2=-1;
        int ind3=-1;

        ComputeThreeMaxima(rotHist,HISTO_LENGTH,ind1,ind2,ind3);

        for(int i=0; i<HISTO_LENGTH; i++)
        {
            if(i!=ind1 && i!=ind2 && i!=ind3)
            {
                for(size_t j=0, jend=rotHist[i].size(); j<jend; j++)
                {
                    CurrentFrame.mvpMapPoints[rotHist[i][j]]=NULL;
                    nmatches--;
                }
            }
        }
    }

    return nmatches;
}

/**
 * @brief 筛选出在旋转角度差落在在直方图区间内数量最多的前三个bin的索引
 * 
 * @param[in] histo         匹配特征点对旋转方向差直方图
 * @param[in] L             直方图尺寸
 * @param[in & out] ind1          bin值第一大对应的索引
 * @param[in & out] ind2          bin值第二大对应的索引
 * @param[in & out] ind3          bin值第三大对应的索引
 */
//Done
void ORBmatcher::ComputeThreeMaxima(vector<int>* histo, const int L, int &ind1, int &ind2, int &ind3)
{
    int max1=0;
    int max2=0;
    int max3=0;

    for(int i=0; i<L; i++)
    {
        const int s = histo[i].size();
        if(s>max1)  // 这里就是排序直方图中的数据大小
        {
            max3=max2;
            max2=max1;
            max1=s;
            ind3=ind2;
            ind2=ind1;
            ind1=i;
        }
        else if(s>max2)
        {
            max3=max2;
            max2=s;
            ind3=ind2;
            ind2=i;
        }
        else if(s>max3)
        {
            max3=s;
            ind3=i;
        }
    }

    // 如果差距太大了,说明次优的非常不好,这里就索性放弃了,都置为-1
    if(max2<0.1f*(float)max1)
    {
        ind2=-1;
        ind3=-1;
    }
    else if(max3<0.1f*(float)max1)
    {
        ind3=-1;
    }
}


// Bit set count operation from
// Hamming distance：两个二进制串之间的汉明距离，指的是其不同位数的个数
// http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetParallel
int ORBmatcher::DescriptorDistance(const cv::Mat &a, const cv::Mat &b)
{
    const int *pa = a.ptr<int32_t>();
    const int *pb = b.ptr<int32_t>();

    int dist=0;

    // 8*32=256bit

    for(int i=0; i<8; i++, pa++, pb++)
    {
        unsigned  int v = *pa ^ *pb;        // 相等为0,不等为1
        // 下面的操作就是计算其中bit为1的个数了,这个操作看上面的链接就好
        // 其实我觉得也还阔以直接使用8bit的查找表,然后做32次寻址操作就完成了;不过缺点是没有利用好CPU的字长
        v = v - ((v >> 1) & 0x55555555);
        v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
        dist += (((v + (v >> 4)) & 0xF0F0F0F) * 0x1010101) >> 24;
    }

    return dist;
}

} //namespace ORB_SLAM
