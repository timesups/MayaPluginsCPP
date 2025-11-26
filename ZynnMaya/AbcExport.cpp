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
        MStringArray strDagpaths = args.asStringArray(index, &status);
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
        MString fName = args.asString(index, &status);
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


        std::string startExportMessage = "Export ";
        for (int i = 0; i < strDagpaths.length(); i++)
        {
            startExportMessage = startExportMessage + strDagpaths[i].asChar() + ",";
        }
        startExportMessage = 
            startExportMessage + "\n"
            + "  Start:" + std::to_string(startTime) + "\n"
            + "  End:" + std::to_string(endTime) + "\n"
            + "  FilePath:" + fName.asChar();
        MGlobal::displayInfo(startExportMessage.c_str());
        //~从参数获取导出信息完成





        std::vector< FrameRangeArgs > frameRanges(1);
        frameRanges.back().startTime = startTime;
        frameRanges.back().endTime = endTime + sExpend + eExpend;
        frameRanges.back().strideTime = step;

        std::string fileName = fName.asChar();



        util::ShapeSet dagPaths;

        for (int i = 0; i < strDagpaths.length(); i++)
        {
            MSelectionList list;
            status = MGlobal::getSelectionListByName(strDagpaths[i], list);
            if (!status)
                continue;
            MDagPath dagPath;
            status = list.getDagPath(0, dagPath);
            if (!status)
                continue;
            dagPaths.insert(dagPath);
        }

        std::set<double> allFrameRange;

        bool sampleGeo = true; // whether or not to subsample geometry

        bool hasRange = true;


        // make sure start frame is smaller or equal to endTime
        if (frameRanges.back().startTime > frameRanges.back().endTime)
        {
            std::swap(frameRanges.back().startTime,
                frameRanges.back().endTime);
        }

        {
            // 1. 设置 MFileObject 并将文件名解析为绝对路径
            MFileObject absoluteFile;
            absoluteFile.setRawFullName(fileName.c_str());
            // 如果 fileName 已经被确保是绝对路径，这句主要用于解析任何环境变量或别名
            fileName = absoluteFile.resolvedFullName().asChar();

            // 2. 检查父目录是否存在 (推荐保留，以确保文件可写)
            MFileObject absoluteFilePath;
            absoluteFilePath.setRawFullName(absoluteFile.path());
            if (!absoluteFilePath.exists()) {
                MString error;
                error.format("Path ^1s does not exist!", absoluteFilePath.resolvedFullName());
                MGlobal::displayError(error);
                return MS::kFailure;
            }

            // 3. 检查文件是否正在被场景中的 AlembicNode 使用 (必须保留)
            MItDependencyNodes dgIter(MFn::kPluginDependNode);
            for (; !dgIter.isDone(); dgIter.next()) {
                MFnDependencyNode alembicNode(dgIter.thisNode());
                if (alembicNode.typeName() != "AlembicNode") {
                    continue;
                }

                // 检查主文件属性
                MPlug abcFilePlug = alembicNode.findPlug("abc_File", true);
                if (!abcFilePlug.isNull())
                {
                    MFileObject alembicFile;
                    alembicFile.setRawFullName(abcFilePlug.asString());
                    if (alembicFile.exists())
                    {
                        if (alembicFile.resolvedFullName() == absoluteFile.resolvedFullName())
                        {
                            MString error = "Can't export to an Alembic file which is in use: ";
                            error += absoluteFile.resolvedFullName();
                            MGlobal::displayError(error);
                            return MS::kFailure;
                        }
                    }
                }

                // 检查分层文件属性
                MPlug abcLayerFilePlug = alembicNode.findPlug("abc_layerFiles", true);
                if (!abcLayerFilePlug.isNull())
                {
                    MFnStringArrayData fnSAD(abcLayerFilePlug.asMObject());
                    MStringArray layerFilenames = fnSAD.array();

                    for (unsigned int l = 0; l < layerFilenames.length(); l++)
                    {
                        MFileObject thisAlembicFile;
                        // ... path resolution logic retained ...
                        thisAlembicFile.setResolveMethod(MFileObject::MFileResolveMethod::kInputFile);
                        thisAlembicFile.setRawFullName(layerFilenames[l]);

                        if (!thisAlembicFile.exists())
                        {
                            continue;
                        }

                        if (thisAlembicFile.resolvedFullName() == absoluteFile.resolvedFullName())
                        {
                            MString error = "Can't export to an Alembic file which is in use: ";
                            error += absoluteFile.resolvedFullName();
                            MGlobal::displayError(error);
                            return MS::kFailure;
                        }
                    }
                }
            }

            // 4. 检查写入权限 (必须保留)
            std::ofstream ofs(fileName.c_str());
            if (!ofs.is_open()) {
                MString error = MString("Can't write to file: ") + fileName.c_str();
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

        if (dagPaths.size() > 1)
        {
            // check for validity of the DagPath relationships complexity : n^2

            util::ShapeSet::const_iterator m, n;
            util::ShapeSet::const_iterator end = dagPaths.end();
            for (m = dagPaths.begin(); m != end; )
            {
                MDagPath path1 = *m;
                m++;
                for (n = m; n != end; n++)
                {
                    MDagPath path2 = *n;
                    if (util::isAncestorDescendentRelationship(path1, path2))
                    {
                        MString errorMsg = path1.fullPathName();
                        errorMsg += " and ";
                        errorMsg += path2.fullPathName();
                        errorMsg += " have an ancestor relationship.";
                        MGlobal::displayError(errorMsg);
                        return MS::kFailure;
                    }
                }  // for n
            }  // for m
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

        AbcWriteJobPtr job(new AbcWriteJob(fileName.c_str(), true,
            transSamples, transTime, geoSamples, geoTime, dagPaths));


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


            //TIMER_START(Per_Frame);
            bool lastFrame = job->eval(*it);
            //TIMER_END(Per_Frame);
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


