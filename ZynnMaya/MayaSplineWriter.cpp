#include "MayaSplineWriter.h"
#include <maya/MFnPluginData.h>
#include <maya/MPxData.h>
#include "MayaTransformWriter.h"


template <typename T>
T GetAttribute(MObject node,MString attrributeName,T defaultValue) 
{
    MStatus status;
    T value = defaultValue;
    MFnDependencyNode depNode(node);

    MPlug plug = depNode.findPlug(attrributeName, false,&status);
    if(status)
        plug.getValue(value);
    return value;
}


MayaSplineWriter::MayaSplineWriter(MDagPath& iDag, Alembic::Abc::OObject& iParent, Alembic::Util::uint32_t iTimeIndex, const JobArgs& iArgs):
    mRootDagPath(iDag)
    {
    MStatus stat;
    MFnDependencyNode fnDepNode(iDag.node(), &stat);
    MString name = fnDepNode.name();


    //int meshUVSetIndex = GetAttribute<int>(iDag.transform(), "GroupName",0);
    //bool bGuideAnimation = GetAttribute<bool>(iDag.transform(), "GroupName", false);
    //bool bSplineAnimation = GetAttribute<bool>(iDag.transform(), "GroupName",false);

    MObject spline = iDag.node();
    
    if (iTimeIndex != 0 && util::isAnimated(spline))
    {
        mIsAnimated = true;
    }
    else
    {
        iTimeIndex = 0;
    }

    name = util::stripNamespaces(name, iArgs.stripNamespace);

    Alembic::AbcGeom::OCurves obj(iParent, name.asChar(), iTimeIndex);
    mSchema = obj.getSchema();


    if (!mIsAnimated || iArgs.setFirstAnimShape) 
    {
        write();
    }
    

}

void MayaSplineWriter::write()
{

    MFnDependencyNode splineDepNode(mRootDagPath.node());
    std::vector<std::vector<uint64_t>> PrimitiveInfosList;
    std::vector<std::vector<float>> PositionsList;
    std::vector<std::vector<float>> WidthDataList;
    //从spline节点中获取必要的信息
    util::GetSplineData(splineDepNode, PrimitiveInfosList, PositionsList, WidthDataList, false);


    Alembic::AbcGeom::OCurvesSchema::Sample samp;
    samp.setBasis(Alembic::AbcGeom::kBsplineBasis);

    samp.setWrap(Alembic::AbcGeom::CurvePeriodicity::kNonPeriodic);
    samp.setType(Alembic::AbcGeom::kCubic);
    

    MStatus status;
    mCVCount = 0;

    mCurves = 0;

    for (int i = 0; i < PrimitiveInfosList.size(); i++)
    {
        mCurves += (PrimitiveInfosList[i].size() / 2);

        for (int j = 0; j < PrimitiveInfosList[i].size(); j += 2)
        {
            mCVCount += PrimitiveInfosList[i][j];
        }
    }

    std::vector<Alembic::Util::int32_t> nVertices(mCurves);
    std::vector<float> points;
    std::vector<float> width;
    std::vector<float> knots;
    std::vector<Alembic::Util::uint8_t> orders(mCurves);


    int degree = 3;


    int curveIndex = 0;
    int cvIndex = 0;


    for (int i = 0; i < PrimitiveInfosList.size(); i++)
    {
        auto primitiveInfos = PrimitiveInfosList[i];
        auto positionData = PositionsList[i];
        auto widthData = WidthDataList[i];

        for (int j = 0; j < primitiveInfos.size(); j += 2)
        {
            uint64_t offset = primitiveInfos[j];
            int length = primitiveInfos[j + 1];

            if (length < 2)
                continue;

            uint64_t startAddr = offset * 3;
            for (int k = 0; k < length; k++)
            {

                points.push_back(positionData[startAddr]);
                points.push_back(positionData[startAddr+1]);
                points.push_back(positionData[startAddr+2]);
                width.push_back(widthData[offset + k]);
                startAddr += 3;
                cvIndex += 1;
            }

            orders[curveIndex] = degree + 1;
            nVertices[curveIndex] = length;

            int knotsInsideNum = length - degree + 1;

            int knotsLitSize = 2 * degree + knotsInsideNum;
            std::vector<int> knotsList(knotsLitSize);
            std::fill_n(knotsList.begin(), degree, 0);

            for (int n = 0; n < knotsInsideNum; n++)
            {
                knotsList[degree + n] = n;
            }
            std::fill_n(knotsList.begin() + degree + knotsInsideNum, degree, knotsInsideNum - 1);
            for (auto value : knotsList)
                knots.push_back(static_cast<float>(value));
            curveIndex += 1;
        }

    }

    samp.setCurvesNumVertices(Alembic::Abc::Int32ArraySample(nVertices));
    samp.setPositions(Alembic::Abc::V3fArraySample(
        (const Imath::V3f*)&points.front(), points.size() / 3
    ));
    samp.setWidths(Alembic::AbcGeom::OFloatGeomParam::Sample(
        Alembic::Abc::FloatArraySample(width), Alembic::AbcGeom::kVertexScope
    ));
    samp.setKnots(Alembic::Abc::FloatArraySample(knots));
    mSchema.set(samp);
}

bool MayaSplineWriter::isAnimated() const
{
    return mIsAnimated;
}

unsigned int MayaSplineWriter::getNumCVs()
{
    return mCVCount;
}

unsigned int MayaSplineWriter::getNumCurves()
{
    return mCurves;
}

void MayaSplineWriter::WriteGroupName()
{
    auto cp = mSchema.getArbGeomParams();
    std::string value = GetAttribute<MString>(mRootDagPath.transform(), attrGroupName, "").asChar();
    Alembic::Abc::OStringArrayProperty groupName = Alembic::Abc::OStringArrayProperty(cp, groomGroupNameAttrName);
    std::vector<std::string> values;
    values.push_back(value);
    groupName.set(Alembic::Abc::StringArraySample(values));
}

void MayaSplineWriter::WriteGroupId(int group_id)
{

}


