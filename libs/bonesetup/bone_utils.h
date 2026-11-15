


#define BONE_PROFILE_LOOP(ID,COUNT)
#define BONE_PROFILE_FUNC()  



void CalcDecompressedAnimation( const mstudiocompressedikerror_t *pCompressed, int iFrame, float fraq, BoneVector &pos, BoneQuaternion &q );
void QuaternionAccumulate( const Quaternion &p, float s, const Quaternion &q, Quaternion &qt );

bool CalcPoseSingle( const CStudioHdr *pStudioHdr, BoneVector pos[], BoneQuaternionAligned q[], mstudioseqdesc_t &seqdesc, int sequence, float cycle, const float poseParameter[], BoneFlags boneMask, float flTime );

void CalcBoneAdj( const CStudioHdr *pStudioHdr, BoneVector pos[], BoneQuaternion q[], const float controllers[], BoneFlags boneMask );

void BuildBoneChainPartial(
	const CStudioHdr *pStudioHdr,
	const matrix3x4_t &rootxform,
	const BoneVector pos[], 
	const BoneQuaternion q[], 
	int	iBone,
	matrix3x4_t *pBoneToWorld,
	CBoneBitList &boneComputed,
	int iRoot );


class CBoneSetup
{
public:
	CBoneSetup( const CStudioHdr *pStudioHdr, BoneFlags boneMask, const float poseParameter[]);
	void InitPose( BoneVector pos[], BoneQuaternionAligned q[] );
	void AccumulatePose( BoneVector pos[], BoneQuaternion q[], int sequence, float cycle, float flWeight, float flTime, CIKContext *pIKContext );
	void CalcAutoplaySequences(	BoneVector pos[], BoneQuaternion q[], float flRealTime, CIKContext *pIKContext );
private:
public:
	const CStudioHdr *m_pStudioHdr;
	BoneFlags m_boneMask;
	const float *m_flPoseParameter;
};



