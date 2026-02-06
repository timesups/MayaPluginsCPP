#pragma once

#include "Foundation.h"
#include "MayaTransformWriter.h"
#include <Alembic/AbcGeom/OCurves.h>


class MayaSplineWriter
{
public:

    MayaSplineWriter(MDagPath& iDag, Alembic::Abc::OObject& iParent,
        Alembic::Util::uint32_t iTimeIndex);
    void write();
    bool isAnimated() const;
    unsigned int getNumCVs();
    unsigned int getNumCurves();
public:
    void WriteGroupName();
    void WriteGroupId(int group_id);
    MStatus GetGuideDagPath(MDagPath& outDag);
    MStatus BakeUV();
    std::vector<MPoint> rootList;
public:
    //attr in abc file
    std::string groomGroupNameAttrName = "groom_group_name";
    std::string groomGuideAttrName = "groom_guide";
    std::string groomGroupIdAttrName = "groom_group_id";
    std::string groomRootUVAttrName = "groom_root_uv";

    //attr in maya
    MString attrGroupName = "GroupName";
    MString attrGuideGroupName = "GuideGroupName";
    MString attrMeshUVName = "MeshUVName";
    MString attrUVSetIndexName = "MeshUVSetIndex";
    MString attrIsExport = "IsExport";
    MString attrCharacterName = "CharachterName";
    MString attrExportGuideAnim = "GuideAnimation";
    MString attrExportSplineAnim = "SplineAnimaiton";

public:
    std::string groupName;
    int groupID;
    MDagPath mRootDagPath;

    bool GuideAnimation = false;
private:

    bool mIsAnimated;


    Alembic::AbcGeom::OCurvesSchema mSchema;

    unsigned int mCVCount;
    unsigned int mCurves;
};
