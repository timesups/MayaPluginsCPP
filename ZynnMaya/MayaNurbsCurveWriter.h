#pragma once

#include "Foundation.h"
#include "MayaTransformWriter.h"

#include <Alembic/AbcGeom/OCurves.h>

class MayaNurbsCurveWriter
{
  public:

    MayaNurbsCurveWriter(MDagPath & iDag, Alembic::Abc::OObject & iParent,
        Alembic::Util::uint32_t iTimeIndex, bool iIsCurveGrp,
        const JobArgs & iArgs,std::string grpName,bool is_guide,int group_id);
    void write();
    bool isAnimated() const;
    unsigned int getNumCVs();
    unsigned int getNumCurves();
public:
    void WriteGroupName(const std::string& group_name);
    void WriteIsGuide(bool is_guide = true);
    void WriteGroupId(int group_id);
public:
    //attr in abc file
    std::string groomGroupNameAttrName = "groom_group_name";
    std::string groomGuideAttrName = "groom_guide";
    std::string groomGroupIdAttrName = "groom_group_id";

    //attr in maya
    MString attrGroupName = "GroupName";
    MString attrGuideGroupName = "GuideGroupName";
    MString attrMeshUVName = "MeshUVName";
    MString attrUVSetIndexName = "MeshUVSetIndex";
    MString attrIsExport = "IsExport";
    MString attrCharacterName = "CharachterName";
    MString attrExportGuideAnim = "GuideAnimation";
    MString attrExportSplineAnim = "SplineAnimaiton";

    std::string groupName;
    bool isGuide;
    int groupID;
protected:
    bool mIsAnimated;
    MDagPath mRootDagPath;
    MDagPathArray mNurbsCurves;

    Alembic::AbcGeom::OCurvesSchema mSchema;

    bool mIsCurveGrp;
    unsigned int mCVCount;
};
