//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#pragma once



#include "studio.h"
#include "bitvec.h"


class CBoneToWorld;
class CIKContext;
class CBoneAccessor;
class IPoseDebugger;




class CBoneBitList : public CBitVec<MAXSTUDIOBONES>
{
public:
	inline void MarkBone(int iBone)
	{
		Set(iBone);
	}
	inline bool IsBoneMarked(int iBone)
	{
		return Get(iBone) != 0 ? true : false;
	}
};


//-----------------------------------------------------------------------------
// Purpose: blends together all the bones from two p:q lists
//
// p1 = p1 * (1 - s) + p2 * s
// q1 = q1 * (1 - s) + q2 * s
//-----------------------------------------------------------------------------
void SlerpBones( 
	const CStudioHdr *pStudioHdr,
	BoneQuaternion q1[MAXSTUDIOBONES], 
	BoneVector pos1[MAXSTUDIOBONES], 
	mstudioseqdesc_t &seqdesc, // source of q2 and pos2
	int sequence, 
	const BoneQuaternionAligned q2[MAXSTUDIOBONES], 
	const BoneVector pos2[MAXSTUDIOBONES], 
	float s,
	BoneFlags boneMask
	);


class CBoneSetup;
class IBoneSetup
{
public:
	IBoneSetup( const CStudioHdr *pStudioHdr, BoneFlags boneMask, const float poseParameter[]);
	~IBoneSetup( void );
	void InitPose( BoneVector pos[], BoneQuaternionAligned q[] );
	void AccumulatePose( BoneVector pos[], BoneQuaternion q[], int sequence, float cycle, float flWeight, float flTime, CIKContext *pIKContext );
	void CalcAutoplaySequences(	BoneVector pos[], BoneQuaternion q[], float flRealTime, CIKContext *pIKContext );
	void CalcBoneAdj( BoneVector pos[], BoneQuaternion q[], const float controllers[] );

	CStudioHdr *GetStudioHdr();

private:
	CBoneSetup *m_pBoneSetup;
};

// Given two samples of a bone separated in time by dt, 
// compute the velocity and angular velocity of that bone
// Give a derivative of a bone, compute the velocity & angular velocity of that bone


// Purpose: build boneToWorld transforms for a specific bone
void BuildBoneChain(
	const CStudioHdr *pStudioHdr,
	const matrix3x4a_t &rootxform,
	const BoneVector pos[], 
	const BoneQuaternion q[], 
	int	iBone,
	matrix3x4a_t *pBoneToWorld );

void BuildBoneChain(
	const CStudioHdr *pStudioHdr,
	const matrix3x4a_t &rootxform,
	const BoneVector pos[], 
	const BoneQuaternion q[], 
	int	iBone,
	matrix3x4a_t *pBoneToWorld,
	CBoneBitList &boneComputed );


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

// ik info
class CIKTarget
{
public:
	int GetOwner( void );

	int chain;
	IkRuleType type;
	// accumulated offset from ideal footplant location
public:
	struct x2 {
		char		*pAttachmentName;
		Vector		pos;
		Quaternion	q;
	} offset;
private:
	struct x3 {
		Vector		pos;
		Quaternion	q;
	} ideal;
public:
	struct x4 {
		float		latched;
		float		release;
		float		height;
		float		floor;
		float		radius;
		float		flTime;
		float		flWeight;
		Vector		pos;
		Quaternion	q;
		bool		onWorld;
	} est; // estimate contact position
	struct x5 {
		float		hipToFoot;	// distance from hip
		float		hipToKnee;	// distance from hip to knee
		float		kneeToFoot;	// distance from knee to foot
		Vector		hip;		// location of hip
		Vector		closest;	// closest valid location from hip to foot that the foot can move to
		Vector		knee;		// pre-ik location of knee
		Vector		farthest;	// farthest valid location from hip to foot that the foot can move to
		Vector		lowest;		// lowest position directly below hip that the foot can drop to
	} trace;
private:
	// internally latched footset, position
	struct x1 {
		// matrix3x4a_t		worldTarget;
		bool		bNeedsLatch;
		bool		bHasLatch;
		float		influence;
		int			iFramecounter;
		int			owner;
		Vector		absOrigin;
		QAngle		absAngles;
		Vector		pos;
		Quaternion	q;
		Vector		deltaPos;	// acculated error
		Quaternion	deltaQ;
		Vector		debouncePos;
		Quaternion	debounceQ;
	} latched;
	struct x6 {
		float		flTime; // time last error was detected
		float		flErrorTime;
		float		ramp;
		bool		bInError;
	} error;

	friend class CIKContext;
};


struct ikchainresult_t
{
	// accumulated offset from ideal footplant location
	int			target;
	Vector		pos;
	Quaternion	q;
	float		flWeight;
};



struct ikcontextikrule_t
{
	int			index;

	IkRuleType	type;
	int			chain;

	int			bone;

	int			slot;	// iktarget slot.  Usually same as chain.
	float		height;
	float		radius;
	float		floor;
	Vector		pos;
	float		pad1;
	Quaternion	q;

	float		start;	// beginning of influence
	float		peak;	// start of full influence
	float		tail;	// end of full influence
	float		end;	// end of all influence

	float		top;
	float		drop;

	float		commit;		// frame footstep target should be committed
	float		release;	// frame ankle should end rotation from latched orientation

	float		flWeight;		// processed version of start-end cycle
	float		flRuleWeight;	// blending weight
	float		latched;		// does the IK rule use a latched value?
	char		*szLabel;

	Vector		kneeDir;
	float		pad2;
	Vector		kneePos;
	float		pad3;

	ikcontextikrule_t() {}

	ikcontextikrule_t(const ikcontextikrule_t&) = delete;
};


void Studio_AlignIKMatrix( matrix3x4a_t &mMat, const Vector &vAlignTo );

bool Studio_SolveIK( int iThigh, int iKnee, int iFoot, Vector &targetFoot, matrix3x4a_t* pBoneToWorld );

bool Studio_SolveIK( int iThigh, int iKnee, int iFoot, Vector &targetFoot, Vector &targetKneePos, Vector &targetKneeDir, matrix3x4a_t* pBoneToWorld );



class CIKContext 
{
public:
	CIKContext( );
	void Init( const CStudioHdr *pStudioHdr, const QAngle &angles, const Vector &pos, float flTime, int iFramecounter, BoneFlags boneMask );


	void ClearTargets( void );
	void SolveDependencies( BoneVector pos[], BoneQuaternion q[], matrix3x4a_t boneToWorld[], CBoneBitList &boneComputed );


	

	void SolveLock( const mstudioiklock_t *plock, int i, BoneVector pos[], BoneQuaternion q[], matrix3x4a_t boneToWorld[], CBoneBitList &boneComputed );

	CUtlVectorFixed< CIKTarget, 12 >	m_target;

	void BuildBoneChain( const BoneVector pos[], const BoneQuaternion q[], int iBone, matrix3x4a_t *pBoneToWorld, CBoneBitList &boneComputed );

	BoneFlags GetBoneMask( void ) { return m_boneMask; }
private:

	CStudioHdr const *m_pStudioHdr;

	bool Estimate( int iSequence, float flCycle, int iTarget, const float poseParameter[], float flWeight = 1.0f ); 
	

	// virtual IK rules, filtered and combined from each sequence
	CUtlVector< CUtlVector< ikcontextikrule_t > > m_ikChainRule;
	CUtlVector< ikcontextikrule_t > m_ikLock;
	matrix3x4a_t m_rootxform;

	int m_iFramecounter;
	float m_flTime;
	BoneFlags m_boneMask;
};


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------





// converts a global 0..1 pose parameter into the local sequences blending value
int Studio_LocalPoseParameter( const CStudioHdr *pStudioHdr, const float poseParameter[], mstudioseqdesc_t &seqdesc, int iSequence, int iLocalIndex, float &flSetting );

void Studio_SeqAnims( const CStudioHdr *pStudioHdr, mstudioseqdesc_t &seqdesc, int iSequence, const float poseParameter[], mstudioanimdesc_t *panim[4], float *weight );
float Studio_CPS( const CStudioHdr *pStudioHdr, mstudioseqdesc_t &seqdesc, int iSequence, const float poseParameter[] );

// void Studio_Movement( const CStudioHdr *pStudioHdr, int iSequence, const float poseParameter[], Vector *pVec );


void QuaternionSM( float s, const Quaternion &p, const Quaternion &q, Quaternion &qt );
void QuaternionMA( const Quaternion &p, float s, const Quaternion &q, Quaternion &qt );

