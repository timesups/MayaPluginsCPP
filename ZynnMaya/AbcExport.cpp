#include "AbcExport.h"
#include "AbcWriteJob.h"
#include "MayaUtility.h"

#include <maya/MFileObject.h>
#include <maya/MItDependencyNodes.h>
#include <fstream>
#include <random>


// ##########################ABC Export New###########################



namespace AbcA = Alembic::AbcCoreAbstract;



std::string AbcExport::AbcExportNewCommandName = "AbcExportN";


AbcExport::AbcExport()
{
}

AbcExport::~AbcExport()
{
}



void* AbcExport::creator()
{
    return new AbcExport();
}



MStatus AbcExport::doIt(const MArgList& args)
{
    try
    {
        //从参数获取导出信息
        MStatus status;

        unsigned int index = 0;
        MStringArray jobsStringArray = args.asStringArray(index, &status);

        std::vector<MStringArray> jobsDagpath;

        for (int i = 0; i < jobsStringArray.length(); i++)
        {
            MStringArray dagPathsString;
            jobsStringArray[i].split(',', dagPathsString);
            jobsDagpath.push_back(dagPathsString);
        }


        if (!status)
        {
            MGlobal::displayError("Failed to get dags");
            return status;
        }

        index = 1;
        double startTime = args.asDouble(index, &status);
        if (!status)
        {
            MGlobal::displayError("Failed to get start time");
            return status;
        }

        index = 2;
        double endTime = args.asDouble(index, &status);
        if (!status)
        {
            MGlobal::displayError("Failed to get end time");
            return status;
        }


        index = 3;

        double step = args.asDouble(index, &status);
        if (!status)
        {
            MGlobal::displayError("Failed to get step");
            return status;
        }



        index = 4;

        double sExpend = args.asDouble(index, &status);
        if (!status)
        {
            MGlobal::displayError("Failed to get start expend");
            return status;
        }

        index = 5;
        double eExpend = args.asDouble(index, &status);
        if (!status)
        {
            MGlobal::displayError("Failed to get end expend");
            return status;
        }



        index = 6;
        MStringArray file_names = args.asStringArray(index, &status);
        if (!status)
        {
            MGlobal::displayError("Failed to get file name");
            return status;
        }


        index = 7;

        bool refresh = args.asBool(index, &status);
        if (!status)
        {
            refresh = false;
        }


        if (file_names.length() != jobsStringArray.length()) {
            MGlobal::displayError("path and jobs not matched");
            return status;
        }
        //~从参数获取导出信息完成


        std::vector< FrameRangeArgs > frameRanges(1);
        frameRanges.back().startTime = startTime;
        frameRanges.back().endTime = endTime + sExpend + eExpend;
        frameRanges.back().strideTime = step;



        
        std::vector<util::ShapeSet> dagPathsArray;
      
        for(int i = 0;i< jobsDagpath.size();i++)
        {
            util::ShapeSet dagPaths;
            for(int j=0;j<jobsDagpath[i].length();j++)
            {
                MSelectionList list;
                status = MGlobal::getSelectionListByName(jobsDagpath[i][j], list);
                if (!status)
                    continue;
                MDagPath dagPath;
                status = list.getDagPath(0, dagPath);
                if (!status)
                    continue;
                dagPaths.insert(dagPath);
            }
            dagPathsArray.push_back(dagPaths);
        }

        std::cout << dagPathsArray.size() << std::endl;


        std::set<double> allFrameRange;

        bool sampleGeo = true; // whether or not to subsample geometry

        bool hasRange = true;


        // make sure start frame is smaller or equal to endTime
        if (frameRanges.back().startTime > frameRanges.back().endTime)
        {
            std::swap(frameRanges.back().startTime,
                frameRanges.back().endTime);
        }


        for(int i=0;i<file_names.length();i++)
        {
            std::ofstream ofs(file_names[i].asChar());
            if (!ofs.is_open()) {
                MString error = MString("Can't write to file: ") + file_names[i].asChar();
                MGlobal::displayError(error);
                return MS::kFailure;
            }
            ofs.close();
        }


        // if -frameRelativeSample argument is not specified for a frame range,
        // we are assuming a -frameRelativeSample 0.0
        for (std::vector<FrameRangeArgs>::iterator range =
            frameRanges.begin(); range != frameRanges.end(); ++range)
        {
            if (range->shutterSamples.empty())
                range->shutterSamples.insert(0.0);
        }

        // the list of frame ranges for sampling
        std::vector<FrameRangeArgs> sampleRanges;
        std::vector<FrameRangeArgs> preRollRanges;
        for (std::vector<FrameRangeArgs>::const_iterator range =
            frameRanges.begin(); range != frameRanges.end(); ++range)
        {
            if (range->preRoll)
                preRollRanges.push_back(*range);
            else
                sampleRanges.push_back(*range);
        }

        // the list of frames written into the abc file
        std::set<double> geoSamples;
        std::set<double> transSamples;
        for (std::vector<FrameRangeArgs>::const_iterator range =
            sampleRanges.begin(); range != sampleRanges.end(); ++range)
        {
            for (double frame = range->startTime;
                frame <= range->endTime;
                frame += range->strideTime)
            {
                for (std::set<double>::const_iterator shutter =
                    range->shutterSamples.begin();
                    shutter != range->shutterSamples.end(); ++shutter)
                {
                    double curFrame = *shutter + frame;
                    if (!sampleGeo)
                    {
                        double intFrame = (double)(int)(
                            curFrame >= 0 ? curFrame + .5 : curFrame - .5);

                        // only insert samples that are close to being an integer
                        if (fabs(curFrame - intFrame) < 1e-4)
                        {
                            geoSamples.insert(curFrame);
                        }
                    }
                    else
                    {
                        geoSamples.insert(curFrame);
                    }
                    transSamples.insert(curFrame);
                }
            }

            if (geoSamples.empty())
            {
                geoSamples.insert(range->startTime);
            }

            if (transSamples.empty())
            {
                transSamples.insert(range->startTime);
            }
        }

        bool isAcyclic = false;
        if (sampleRanges.empty())
        {
            // no frame ranges or all frame ranges are pre-roll ranges
            hasRange = false;
            geoSamples.insert(frameRanges.back().startTime);
            transSamples.insert(frameRanges.back().startTime);
        }
        else
        {
            // check if the time range is even (cyclic)
            // otherwise, we will use acyclic
            // sub frames pattern
            std::vector<double> pattern(
                sampleRanges.begin()->shutterSamples.begin(),
                sampleRanges.begin()->shutterSamples.end());
            std::transform(pattern.begin(), pattern.end(), pattern.begin(),
                std::bind2nd(std::plus<double>(),
                    sampleRanges.begin()->startTime));

            // check the frames against the pattern
            std::vector<double> timeSamples(
                transSamples.begin(), transSamples.end());
            for (size_t i = 0; i < timeSamples.size(); i++)
            {
                // next pattern
                if (i % pattern.size() == 0 && i / pattern.size() > 0)
                {
                    std::transform(pattern.begin(), pattern.end(),
                        pattern.begin(), std::bind2nd(std::plus<double>(),
                            sampleRanges.begin()->strideTime));
                }

                // pattern mismatch, we use acyclic time sampling type
                if (timeSamples[i] != pattern[i % pattern.size()])
                {
                    isAcyclic = true;
                    break;
                }
            }
        }

        // the list of frames to pre-roll
        std::set<double> preRollSamples;
        for (std::vector<FrameRangeArgs>::const_iterator range =
            preRollRanges.begin(); range != preRollRanges.end(); ++range)
        {
            for (double frame = range->startTime;
                frame <= range->endTime;
                frame += range->strideTime)
            {
                for (std::set<double>::const_iterator shutter =
                    range->shutterSamples.begin();
                    shutter != range->shutterSamples.end(); ++shutter)
                {
                    double curFrame = *shutter + frame;
                    preRollSamples.insert(curFrame);
                }
            }

            if (preRollSamples.empty())
            {
                preRollSamples.insert(range->startTime);
            }
        }

        AbcA::TimeSamplingPtr transTime, geoTime;

        if (hasRange)
        {
            if (isAcyclic)
            {
                // acyclic, uneven time sampling
                // e.g. [0.8, 1, 1.2], [2.8, 3, 3.2], .. not continuous
                //      [0.8, 1, 1.2], [1.7, 2, 2.3], .. shutter different
                std::vector<double> samples(
                    transSamples.begin(), transSamples.end());
                std::transform(samples.begin(), samples.end(), samples.begin(),
                    std::bind2nd(std::multiplies<double>(), util::spf()));
                transTime.reset(new AbcA::TimeSampling(AbcA::TimeSamplingType(
                    AbcA::TimeSamplingType::kAcyclic), samples));
            }
            else
            {
                // cyclic, even time sampling between time periods
                // e.g. [0.8, 1, 1.2], [1.8, 2, 2.2], ...
                std::vector<double> samples;
                double startTime = sampleRanges[0].startTime;
                double strideTime = sampleRanges[0].strideTime;
                for (std::set<double>::const_iterator shutter =
                    sampleRanges[0].shutterSamples.begin();
                    shutter != sampleRanges[0].shutterSamples.end();
                    ++shutter)
                {
                    samples.push_back((startTime + *shutter) * util::spf());
                }

                if (samples.size() > 1)
                {
                    Alembic::Util::uint32_t numSamples =
                        static_cast<Alembic::Util::uint32_t>(samples.size());
                    transTime.reset(
                        new AbcA::TimeSampling(AbcA::TimeSamplingType(
                            numSamples, strideTime * util::spf()), samples));
                }
                // uniform sampling
                else
                {
                    transTime.reset(new AbcA::TimeSampling(
                        strideTime * util::spf(), samples[0]));
                }
            }
        }
        else
        {
            // time ranges are not specified
            transTime.reset(new AbcA::TimeSampling());
        }

        if (sampleGeo || !hasRange)
        {
            geoTime = transTime;
        }
        else
        {
            // sampling geo on whole frames
            if (isAcyclic)
            {
                // acyclic, uneven time sampling
                std::vector<double> samples(
                    geoSamples.begin(), geoSamples.end());
                // one more sample for setup()
                if (*transSamples.begin() != *geoSamples.begin())
                    samples.insert(samples.begin(), *transSamples.begin());
                std::transform(samples.begin(), samples.end(), samples.begin(),
                    std::bind2nd(std::multiplies<double>(), util::spf()));
                geoTime.reset(new AbcA::TimeSampling(AbcA::TimeSamplingType(
                    AbcA::TimeSamplingType::kAcyclic), samples));
            }
            else
            {
                double geoStride = sampleRanges[0].strideTime;
                if (geoStride < 1.0)
                    geoStride = 1.0;

                double geoStart = *geoSamples.begin() * util::spf();
                geoTime.reset(new AbcA::TimeSampling(
                    geoStride * util::spf(), geoStart));
            }
        }

        std::vector< AbcWriteJobPtr> job_list;

        for(int i=0;i < dagPathsArray.size();i++)
        {
            AbcWriteJobPtr job(new AbcWriteJob(file_names[i].asChar(), true,
                transSamples, transTime, geoSamples, geoTime, dagPathsArray[i]));

            job_list.push_back(job);
        }



        double localMin = *(transSamples.begin());
        std::set<double>::iterator last = transSamples.end();
        last--;
        double localMax = *last;

        double globalMin = *(allFrameRange.begin());
        last = allFrameRange.end();
        last--;
        double globalMax = *last;

        // if the min of our current frame range is beyond
        // what we know about, pad a few more frames
        if (localMin > globalMax)
        {
            for (double f = globalMax; f < localMin; f++)
            {
                allFrameRange.insert(f);
            }
        }

        // if the max of our current frame range is beyond
        // what we know about, pad a few more frames
        if (localMax < globalMin)
        {
            for (double f = localMax; f < globalMin; f++)
            {
                allFrameRange.insert(f);
            }
        }

        // right now we just copy over the translation samples since
        // they are guaranteed to contain all the geometry samples
        allFrameRange.insert(transSamples.begin(), transSamples.end());

        // copy over the pre-roll samples
        allFrameRange.insert(preRollSamples.begin(), preRollSamples.end());


        //遍历每一帧
        std::set<double>::iterator it = allFrameRange.begin();
        std::set<double>::iterator itEnd = allFrameRange.end();

        MComputation computation;
        computation.beginComputation();


        for (; it != itEnd; it++)
        {
            if(refresh)
            {
                //逐帧刷新
                MItDependencyNodes itnodes;

                for (; !itnodes.isDone(); itnodes.next())
                {
                    MFnDependencyNode node(itnodes.thisNode());
                    if (node.typeName() == "xgmCurveToSpline")
                    {
                        std::random_device rd;
                        std::uniform_int_distribution<int> dist(0, 9);

                        MPlug plug_speed = node.findPlug("speed");
                        plug_speed.setFloat(dist(rd));

                    }

                }
            }

            if (*it < (startTime + sExpend))
            {
                MGlobal::viewFrame(startTime);
            }
            else if(*it > endTime + sExpend)
            {
                MGlobal::viewFrame(endTime);
            }
            else
            {
                MGlobal::viewFrame(*it - sExpend);
            }

            if (computation.isInterruptRequested())
                return MS::kFailure;

            for(auto single_job : job_list)
            {
                single_job->eval(*it);
            }

        }
        computation.endComputation();

        return MS::kSuccess;
    }
    catch (Alembic::Util::Exception& e)
    {
        MString theError("Alembic Exception encountered: ");
        theError += e.what();
        MGlobal::displayError(theError);
        return MS::kFailure;
    }
    catch (std::exception& e)
    {
        MString theError("std::exception encountered: ");
        theError += e.what();
        MGlobal::displayError(theError);
        return MS::kFailure;
    }

}


// ~##########################ABC Export New###########################~


