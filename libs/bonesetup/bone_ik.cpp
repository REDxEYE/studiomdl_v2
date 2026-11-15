//===== Copyright � 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//


#include "mathlib/mathlib.h"
#include "studiomdl/bone_setup.h"

#include <cstring>




#include "bone_utils.h"



class CIKSolver
{
public:
//-------- SOLVE TWO LINK INVERSE KINEMATICS -------------
// Author: Ken Perlin
//
// Given a two link joint from [0,0,0] to end effector position P,
// let link lengths be a and b, and let norm |P| = c.  Clearly a+b <= c.
//
// Problem: find a "knee" position Q such that |Q| = a and |P-Q| = b.
//
// In the case of a point on the x axis R = [c,0,0], there is a
// closed form solution S = [d,e,0], where |S| = a and |R-S| = b:
//
//    d2+e2 = a2                  -- because |S| = a
//    (c-d)2+e2 = b2              -- because |R-S| = b
//
//    c2-2cd+d2+e2 = b2           -- combine the two equations
//    c2-2cd = b2 - a2
//    c-2d = (b2-a2)/c
//    d - c/2 = (a2-b2)/c / 2
//
//    d = (c + (a2-b2/c) / 2      -- to solve for d and e.
//    e = sqrt(a2-d2)

   static float findD(float a, float b, float c) {
      return (c + (a*a-b*b)/c) / 2;
   }
   static float findE(float a, float d) { return sqrt(a*a-d*d); }

// This leads to a solution to the more general problem:
//
//   (1) R = Mfwd(P)         -- rotate P onto the x axis
//   (2) Solve for S
//   (3) Q = Minv(S)         -- rotate back again

   float Mfwd[3][3];
   float Minv[3][3];

   bool solve(float A, float B, float const P[], float const D[], float Q[]) {
      float R[3];
      defineM(P,D);
      rot(Minv,P,R);
	  float r = length(R);
      float d = findD(A,B,r);
      float e = findE(A,d);
      float S[3] = {d,e,0};
      rot(Mfwd,S,Q);
      return d > (r - B) && d < A;
   }

// If "knee" position Q needs to be as close as possible to some point D,
// then choose M such that M(D) is in the y>0 half of the z=0 plane.
//
// Given that constraint, define the forward and inverse of M as follows:

   void defineM(float const P[], float const D[]) {
      float *X = Minv[0], *Y = Minv[1], *Z = Minv[2];

// Minv defines a coordinate system whose x axis contains P, so X = unit(P).
	  int i;
      for (i = 0 ; i < 3 ; i++)
         X[i] = P[i];
      normalize(X);

// Its y axis is perpendicular to P, so Y = unit( E - X(E�X) ).

      float dDOTx = dot(D,X);
      for (i = 0 ; i < 3 ; i++)
         Y[i] = D[i] - dDOTx * X[i];
      normalize(Y);

// Its z axis is perpendicular to both X and Y, so Z = X�Y.

      cross(X,Y,Z);

// Mfwd = (Minv)T, since transposing inverts a rotation matrix.

      for (i = 0 ; i < 3 ; i++) {
         Mfwd[i][0] = Minv[0][i];
         Mfwd[i][1] = Minv[1][i];
         Mfwd[i][2] = Minv[2][i];
      }
   }

//------------ GENERAL VECTOR MATH SUPPORT -----------

   static float dot(float const a[], float const b[]) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

   static float length(float const v[]) { return sqrt( dot(v,v) ); }

   static void normalize(float v[]) {
      float norm = length(v);
      for (int i = 0 ; i < 3 ; i++)
         v[i] /= norm;
   }

   static void cross(float const a[], float const b[], float c[]) {
      c[0] = a[1] * b[2] - a[2] * b[1];
      c[1] = a[2] * b[0] - a[0] * b[2];
      c[2] = a[0] * b[1] - a[1] * b[0];
   }

   static void rot(float const M[3][3], float const src[], float dst[]) {
      for (int i = 0 ; i < 3 ; i++)
         dst[i] = dot(M[i],src);
   }
};



//-----------------------------------------------------------------------------
// Purpose: visual debugging code
//-----------------------------------------------------------------------------
#if 1
inline void debugLine(const Vector& origin, const Vector& dest, int r, int g, int b, bool noDepthTest, float duration) { };
#else
extern void drawLine( const Vector &p1, const Vector &p2, int r = 0, int g = 0, int b = 1, bool noDepthTest = true, float duration = 0.1 );
void debugLine(const Vector& origin, const Vector& dest, int r, int g, int b, bool noDepthTest, float duration)
{
	drawLine( origin, dest, r, g, b, noDepthTest, duration );
}
#endif


//-----------------------------------------------------------------------------
// Purpose: for a 2 bone chain, find the IK solution and reset the matrices
//-----------------------------------------------------------------------------
bool Studio_SolveIK( mstudioikchain_t *pikchain, Vector &targetFoot, matrix3x4a_t *pBoneToWorld )
{
#if 0
	// FIXME: something with the CS models breaks this, why?
	if (pikchain->pLink(0)->kneeDir.LengthSqr() > 0.0)
	{
		Vector targetKneeDir, targetKneePos;
		// FIXME: knee length should be as long as the legs
		Vector tmp = pikchain->pLink( 0 )->kneeDir;
		VectorRotate( tmp, pBoneToWorld[ pikchain->pLink( 0 )->bone ], targetKneeDir );
		MatrixPosition( pBoneToWorld[ pikchain->pLink( 1 )->bone ], targetKneePos );
		return Studio_SolveIK( pikchain->pLink( 0 )->bone, pikchain->pLink( 1 )->bone, pikchain->pLink( 2 )->bone, targetFoot, targetKneePos, targetKneeDir, pBoneToWorld );
	}
	else
#endif
	{
		return Studio_SolveIK( pikchain->pLink( 0 )->bone, pikchain->pLink( 1 )->bone, pikchain->pLink( 2 )->bone, targetFoot, pBoneToWorld );
	}
}


#define KNEEMAX_EPSILON 0.9998 // (0.9998 is about 1 degree)

//-----------------------------------------------------------------------------
// Purpose: Solve Knee position for a known hip and foot location, but no specific knee direction preference
//-----------------------------------------------------------------------------

bool Studio_SolveIK( int iThigh, int iKnee, int iFoot, Vector &targetFoot, matrix3x4a_t *pBoneToWorld )
{
	Vector worldFoot, worldKnee, worldThigh;

	MatrixPosition( pBoneToWorld[ iThigh ], worldThigh );
	MatrixPosition( pBoneToWorld[ iKnee ], worldKnee );
	MatrixPosition( pBoneToWorld[ iFoot ], worldFoot );

	//debugLine( worldThigh, worldKnee, 0, 0, 255, true, 0 );
	//debugLine( worldKnee, worldFoot, 0, 0, 255, true, 0 );

	Vector ikFoot, ikKnee;

	ikFoot = targetFoot - worldThigh;
	ikKnee = worldKnee - worldThigh;

	float l1 = (worldKnee-worldThigh).Length();
	float l2 = (worldFoot-worldKnee).Length();
	float l3 = (worldFoot-worldThigh).Length();

	// leg too straight to figure out knee?
	if (l3 > (l1 + l2) * KNEEMAX_EPSILON)
	{
		return false;
	}

	// If any of the thigh to knee to foot bones are co-positional, then solving ik doesn't make sense.
	// We're probably looking at uninitialized bones or something
	if ( l1 <= 0 || l2 <= 0 || l3 <= 0 )
	{
		return false;
	}

	Vector ikHalf = (worldFoot-worldThigh) * (l1 / l3);

	// FIXME: what to do when the knee completely straight?
	Vector ikKneeDir = ikKnee - ikHalf;
	VectorNormalize( ikKneeDir );

	return Studio_SolveIK( iThigh, iKnee, iFoot, targetFoot, worldKnee, ikKneeDir, pBoneToWorld );
}

//-----------------------------------------------------------------------------
// Purpose: Realign the matrix so that its X axis points along the desired axis.
//-----------------------------------------------------------------------------
void Studio_AlignIKMatrix( matrix3x4a_t &mMat, const Vector &vAlignTo )
{
	Vector tmp1, tmp2, tmp3;

	// Column 0 (X) becomes the vector.
	tmp1 = vAlignTo;
	VectorNormalize( tmp1 );
	MatrixSetColumn( tmp1, 0, mMat );

	// Column 1 (Y) is the cross of the vector and column 2 (Z).
	MatrixGetColumn( mMat, 2, tmp3 );
	tmp2 = tmp3.Cross( tmp1 );
	VectorNormalize( tmp2 );
	// FIXME: check for X being too near to Z
	MatrixSetColumn( tmp2, 1, mMat );

	// Column 2 (Z) is the cross of columns 0 (X) and 1 (Y).
	tmp3 = tmp1.Cross( tmp2 );
	MatrixSetColumn( tmp3, 2, mMat );
}


//-----------------------------------------------------------------------------
// Purpose: Solve Knee position for a known hip and foot location, and a known knee direction
//-----------------------------------------------------------------------------

bool Studio_SolveIK( int iThigh, int iKnee, int iFoot, Vector &targetFoot, Vector &targetKneePos, Vector &targetKneeDir, matrix3x4a_t *pBoneToWorld )
{
	Vector worldFoot, worldKnee, worldThigh;

	MatrixPosition( pBoneToWorld[ iThigh ], worldThigh );
	MatrixPosition( pBoneToWorld[ iKnee ], worldKnee );
	MatrixPosition( pBoneToWorld[ iFoot ], worldFoot );

	//debugLine( worldThigh, worldKnee, 0, 0, 255, true, 0 );
	//debugLine( worldThigh, worldThigh + targetKneeDir, 0, 0, 255, true, 0 );
	// debugLine( worldKnee, targetKnee, 0, 0, 255, true, 0 );

	Vector ikFoot, ikTargetKnee, ikKnee;

	ikFoot = targetFoot - worldThigh;
	ikKnee = targetKneePos - worldThigh;

	float l1 = (worldKnee-worldThigh).Length();
	float l2 = (worldFoot-worldKnee).Length();

	// exaggerate knee targets for legs that are nearly straight
	// FIXME: should be configurable, and the ikKnee should be from the original animation, not modifed
	float d = (targetFoot-worldThigh).Length() - MIN( l1, l2 );
	d = MAX( l1 + l2, d );
	// FIXME: too short knee directions cause trouble
	d = d * 100;

	ikTargetKnee = ikKnee + targetKneeDir * d;

	// debugLine( worldKnee, worldThigh + ikTargetKnee, 0, 0, 255, true, 0 );

	int color[3] = { 0, 255, 0 };

	// too far away? (0.9998 is about 1 degree)
	if (ikFoot.Length() > (l1 + l2) * KNEEMAX_EPSILON)
	{
		VectorNormalize( ikFoot );
		VectorScale( ikFoot, (l1 + l2) * KNEEMAX_EPSILON, ikFoot );
		color[0] = 255; color[1] = 0; color[2] = 0;
	}

	// too close?
	// limit distance to about an 80 degree knee bend
	float minDist = MAX( fabs(l1 - l2) * 1.15, MIN( l1, l2 ) * 0.15 );
	if (ikFoot.Length() < minDist)
	{
		// too close to get an accurate vector, just use original vector
		ikFoot = (worldFoot - worldThigh);
		VectorNormalize( ikFoot );
		VectorScale( ikFoot, minDist, ikFoot );
	}

	CIKSolver ik;
	if (ik.solve( l1, l2, ikFoot.Base(), ikTargetKnee.Base(), ikKnee.Base() ))
	{
		matrix3x4a_t& mWorldThigh = pBoneToWorld[ iThigh ];
		matrix3x4a_t& mWorldKnee = pBoneToWorld[ iKnee ];
		matrix3x4a_t& mWorldFoot = pBoneToWorld[ iFoot ];

		//debugLine( worldThigh, ikKnee + worldThigh, 255, 0, 0, true, 0 );
		//debugLine( ikKnee + worldThigh, ikFoot + worldThigh, 255, 0, 0, true,0 );

		// debugLine( worldThigh, ikKnee + worldThigh, color[0], color[1], color[2], true, 0 );
		// debugLine( ikKnee + worldThigh, ikFoot + worldThigh, color[0], color[1], color[2], true,0 );


		// build transformation matrix for thigh
		Studio_AlignIKMatrix( mWorldThigh, ikKnee );
		Studio_AlignIKMatrix( mWorldKnee, ikFoot - ikKnee );


		mWorldKnee[0][3] = ikKnee.x + worldThigh.x;
		mWorldKnee[1][3] = ikKnee.y + worldThigh.y;
		mWorldKnee[2][3] = ikKnee.z + worldThigh.z;

		mWorldFoot[0][3] = ikFoot.x + worldThigh.x;
		mWorldFoot[1][3] = ikFoot.y + worldThigh.y;
		mWorldFoot[2][3] = ikFoot.z + worldThigh.z;

		return true;
	}
	else
	{
		/*
		debugLine( worldThigh, worldThigh + ikKnee, 255, 0, 0, true, 0 );
		debugLine( worldThigh + ikKnee, worldThigh + ikFoot, 255, 0, 0, true, 0 );
		debugLine( worldThigh + ikFoot, worldThigh, 255, 0, 0, true, 0 );
		debugLine( worldThigh + ikKnee, worldThigh + ikTargetKnee, 255, 0, 0, true, 0 );
		*/
		return false;
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

float Studio_IKRuleWeight( mstudioikrule_t &ikRule, const mstudioanimdesc_t *panim, float flCycle, int &iFrame, float &fraq )
{
	if (ikRule.end > 1.0f && flCycle < ikRule.start)
	{
		flCycle = flCycle + 1.0f;
	}

	float value = 0.0f;
	fraq = (panim->numframes - 1) * (flCycle - ikRule.start) + ikRule.iStart;
	iFrame = (int)fraq;
	fraq = fraq - iFrame;

	if (flCycle < ikRule.start)
	{
		iFrame = ikRule.iStart;
		fraq = 0.0f;
		return 0.0f;
	}
	else if (flCycle < ikRule.peak )
	{
		value = (flCycle - ikRule.start) / (ikRule.peak - ikRule.start);
	}
	else if (flCycle < ikRule.tail )
	{
		return 1.0f;
	}
	else if (flCycle < ikRule.end )
	{
		value = 1.0f - ((flCycle - ikRule.tail) / (ikRule.end - ikRule.tail));
	}
	else
	{
		fraq = (panim->numframes - 1) * (ikRule.end - ikRule.start) + ikRule.iStart;
		iFrame = (int)fraq;
		fraq = fraq - iFrame;
	}
	return SimpleSpline( value );
}


float Studio_IKRuleWeight( ikcontextikrule_t &ikRule, float flCycle )
{
	if (ikRule.end > 1.0f && flCycle < ikRule.start)
	{
		flCycle = flCycle + 1.0f;
	}

	float value = 0.0f;
	if (flCycle < ikRule.start)
	{
		return 0.0f;
	}
	else if (flCycle < ikRule.peak )
	{
		value = (flCycle - ikRule.start) / (ikRule.peak - ikRule.start);
	}
	else if (flCycle < ikRule.tail )
	{
		return 1.0f;
	}
	else if (flCycle < ikRule.end )
	{
		value = 1.0f - ((flCycle - ikRule.tail) / (ikRule.end - ikRule.tail));
	}
	return 3.0f * value * value - 2.0f * value * value * value;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

bool Studio_IKShouldLatch( ikcontextikrule_t &ikRule, float flCycle )
{
	if (ikRule.end > 1.0f && flCycle < ikRule.start)
	{
		flCycle = flCycle + 1.0f;
	}

	if (flCycle < ikRule.peak )
	{
		return false;
	}
	else if (flCycle < ikRule.end )
	{
		return true;
	}
	return false;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

float Studio_IKTail( ikcontextikrule_t &ikRule, float flCycle )
{
	if (ikRule.end > 1.0f && flCycle < ikRule.start)
	{
		flCycle = flCycle + 1.0f;
	}

	if (flCycle <= ikRule.tail )
	{
		return 0.0f;
	}
	else if (flCycle < ikRule.end )
	{
		return ((flCycle - ikRule.tail) / (ikRule.end - ikRule.tail));
	}
	return 0.0;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------


bool Studio_IKAnimationError( const CStudioHdr *pStudioHdr, mstudioikrule_t *pRule, const mstudioanimdesc_t *panim, float flCycle, BoneVector &pos, BoneQuaternion &q, float &flWeight )
{
	float fraq;
	int iFrame;

	if (!pRule)
		return false;

	flWeight = Studio_IKRuleWeight( *pRule, panim, flCycle, iFrame, fraq );
	Assert( fraq >= 0.0 && fraq < 1.0 );
	Assert( flWeight >= 0.0f && flWeight <= 1.0f );

	// This shouldn't be necessary, but the Assert should help us catch whoever is screwing this up
	flWeight = clamp( flWeight, 0.0f, 1.0f );

	if (pRule->type != IkRuleType::Ground && flWeight < 0.0001)
		return false;

	mstudioikerror_t *pError = pRule->pError( iFrame );
	if (pError != nullptr)
	{
		if (fraq < 0.001)
		{
			q = pError[0].q;
			pos = pError[0].pos;
		}
		else
		{
			QuaternionBlend( pError[0].q, pError[1].q, fraq, q );
			pos = pError[0].pos * (1.0f - fraq) + pError[1].pos * fraq;
		}
		return true;
	}

	mstudiocompressedikerror_t *pCompressed = pRule->pCompressedError();
	if (pCompressed != nullptr)
	{
		CalcDecompressedAnimation( pCompressed, iFrame - pRule->iStart, fraq, pos, q );
		return true;
	}
	// no data, disable IK rule
	Assert( 0 );
	flWeight = 0.0f;
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: For a specific sequence:rule, find where it starts, stops, and what 
//			the estimated offset from the connection point is.
//			return true if the rule is within bounds.
//-----------------------------------------------------------------------------

bool Studio_IKSequenceError( const CStudioHdr *pStudioHdr, mstudioseqdesc_t &seqdesc, int iSequence, float flCycle, int iRule, const float poseParameter[], mstudioanimdesc_t *panim[4], float weight[4], ikcontextikrule_t &ikRule )
{
	int i;

	memset( &ikRule, 0, sizeof(ikRule) );
	ikRule.start = ikRule.peak = ikRule.tail = ikRule.end = 0;


	float prevStart = 0.0f;

	// find overall influence
	for (i = 0; i < 4; i++)
	{
		if (weight[i])
		{
			if (iRule >= panim[i]->numikrules || panim[i]->numikrules != panim[0]->numikrules)
			{
				Assert( 0 );
				return false;
			}

			mstudioikrule_t *pRule = panim[i]->pIKRule( iRule );
			if (pRule != nullptr)
			{
				float dt = 0.0f;
				if (prevStart != 0.0f)
				{
					if (pRule->start - prevStart > 0.5)
					{
						dt = -1.0;
					}
					else if (pRule->start - prevStart < -0.5)
					{
						dt = 1.0;
					}
				}
				else
				{
					prevStart = pRule->start;
				}

				ikRule.start += (pRule->start + dt) * weight[i];
				ikRule.peak += (pRule->peak + dt) * weight[i];
				ikRule.tail += (pRule->tail + dt) * weight[i];
				ikRule.end += (pRule->end + dt) * weight[i];
			}
			else
			{
				mstudioikrulezeroframe_t *pZeroFrameRule = panim[i]->pIKRuleZeroFrame( iRule );
				if (pZeroFrameRule)
				{
					float dt = 0.0f;
					if (prevStart != 0.0f)
					{
						if (pZeroFrameRule->start.GetFloat() - prevStart > 0.5)
						{
							dt = -1.0;
						}
						else if (pZeroFrameRule->start.GetFloat() - prevStart < -0.5)
						{
							dt = 1.0;
						}
					}
					else
					{
						prevStart = pZeroFrameRule->start.GetFloat();
					}

					ikRule.start += (pZeroFrameRule->start.GetFloat() + dt) * weight[i];
					ikRule.peak += (pZeroFrameRule->peak.GetFloat() + dt) * weight[i];
					ikRule.tail += (pZeroFrameRule->tail.GetFloat() + dt) * weight[i];
					ikRule.end += (pZeroFrameRule->end.GetFloat() + dt) * weight[i];
				}
				else
				{
					// Msg("{} {} - IK Stall\n", pStudioHdr->name(), seqdesc.pszLabel() );
					return false;
				}
			}
		}
	}
	if (ikRule.start > 1.0)
	{
		ikRule.start -= 1.0;
		ikRule.peak -= 1.0;
		ikRule.tail -= 1.0;
		ikRule.end -= 1.0;
	}
	else if (ikRule.start < 0.0)
	{
		ikRule.start += 1.0;
		ikRule.peak += 1.0;
		ikRule.tail += 1.0;
		ikRule.end += 1.0;
	}

	ikRule.flWeight = Studio_IKRuleWeight( ikRule, flCycle );
	if (ikRule.flWeight <= 0.001f)
	{
		// go ahead and allow IkRuleType::Ground rules a virtual looping section
		if ( weight[0] )
		{
			if ( panim[ 0 ]->pIKRule( iRule ) == nullptr)
				return false;
			if ( HasFlag(panim[ 0 ]->flags, StudioSequenceFlags::Looping) && panim[ 0 ]->pIKRule( iRule )->type == IkRuleType::Ground && ikRule.end - ikRule.start > 0.75 )
			{
				ikRule.flWeight = 0.001;
				flCycle = ikRule.end - 0.001;
			}
			else
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}

	Assert( ikRule.flWeight > 0.0f );

	ikRule.pos.Init();
	ikRule.q.Init();

	// find target error
	float total = 0.0f;
	for (i = 0; i < 4; i++)
	{
		if (weight[i])
		{
			BoneVector pos1;
			BoneQuaternion q1;
			float w;

			mstudioikrule_t *pRule = panim[i]->pIKRule( iRule );
			if (pRule != nullptr)
			{
				ikRule.chain = pRule->chain;	// FIXME: this is anim local
				ikRule.bone = pRule->bone;		// FIXME: this is anim local
				ikRule.type = pRule->type;
				ikRule.slot = pRule->slot;

				ikRule.height += pRule->height * weight[i];
				ikRule.floor += pRule->floor * weight[i];
				ikRule.radius += pRule->radius * weight[i];
				ikRule.drop += pRule->drop * weight[i];
				ikRule.top += pRule->top * weight[i];
			}
			else
			{
				// look to see if there's a zeroframe version of the rule
				mstudioikrulezeroframe_t *pZeroFrameRule = panim[i]->pIKRuleZeroFrame( iRule );
				if (pZeroFrameRule)
				{
					// zeroframe doesn't contain details, so force a IkRuleType::Release
					ikRule.type = IkRuleType::Release;
					ikRule.chain = pZeroFrameRule->chain;
					ikRule.slot = pZeroFrameRule->slot;
					ikRule.bone = -1;
					// Msg("IkRuleType::Release {:d} {:d} : {:.2f}\n", ikRule.chain, ikRule.slot, ikRule.flWeight );
				}
				else
				{
					// Msg("{} {} - IK Stall\n", pStudioHdr->name(), seqdesc.pszLabel() );
					return false;
				}
			}

			// keep track of tail condition
			ikRule.release += Studio_IKTail( ikRule, flCycle ) * weight[i];

			// only check rules with error values
			switch( ikRule.type )
			{
			case IkRuleType::Self:
			case IkRuleType::World:
			case IkRuleType::Ground:
			case IkRuleType::Attachment:
				{
					int bResult = Studio_IKAnimationError( pStudioHdr, pRule, panim[i], flCycle, pos1, q1, w );

					if (bResult)
					{
						ikRule.pos = ikRule.pos + pos1 * weight[i];
						QuaternionAccumulate( ikRule.q, weight[i], q1, ikRule.q );
						total += weight[i];
					}
				}
				break;
			default:
				total += weight[i];
				break;
			}

			ikRule.latched = Studio_IKShouldLatch( ikRule, flCycle ) * ikRule.flWeight;

			if (ikRule.type == IkRuleType::Attachment)
			{
				ikRule.szLabel = pRule->pszAttachment();
			}
		}
	}

	if (total <= 0.0001f)
	{
		return false;
	}

	if (total < 0.999f)
	{
		VectorScale( ikRule.pos, 1.0f / total, ikRule.pos );
		QuaternionScale( ikRule.q, 1.0f / total, ikRule.q );
	}

	if (ikRule.type == IkRuleType::Self && ikRule.bone != -1)
	{
		// FIXME: this is anim local, not seq local!
		ikRule.bone = pStudioHdr->RemapSeqBone( iSequence, ikRule.bone );
		if (ikRule.bone == -1)
			return false;
	}

	QuaternionNormalize( ikRule.q );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------


CIKContext::CIKContext()
{
	m_target.EnsureCapacity( 12 ); // FIXME: this sucks, shouldn't it be grown?
	m_iFramecounter = -1;
	m_pStudioHdr = nullptr;
	m_flTime = -1.0f;
	m_target.SetSize( 0 );
}


void CIKContext::Init( const CStudioHdr *pStudioHdr, const QAngle &angles, const Vector &pos, float flTime, int iFramecounter, BoneFlags boneMask )
{

	m_pStudioHdr = pStudioHdr;
	m_ikChainRule.RemoveAll(); // m_numikrules = 0;
	if (pStudioHdr->numikchains())
	{
		m_ikChainRule.SetSize( pStudioHdr->numikchains() );

		// FIXME: Brutal hackery to prevent a crash
		if (m_target.size() == 0)
		{
			m_target.SetSize(12);
			memset( m_target.data(), 0, sizeof(m_target[0])*m_target.size() );
			ClearTargets();
		}

	}
	else
	{
		m_target.SetSize( 0 );
	}
	AngleMatrix( angles, pos, m_rootxform );
	m_iFramecounter = iFramecounter;
	m_flTime = flTime;
	m_boneMask = boneMask;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: build boneToWorld transforms for a specific bone
//-----------------------------------------------------------------------------
void CIKContext::BuildBoneChain(
	const BoneVector pos[],
	const BoneQuaternion q[],
	int	iBone,
	matrix3x4a_t *pBoneToWorld,
	CBoneBitList &boneComputed )
{
	Assert( m_pStudioHdr->boneFlags( iBone ) & m_boneMask );
	::BuildBoneChain( m_pStudioHdr, m_rootxform, pos, q, iBone, pBoneToWorld, boneComputed );
}


//-----------------------------------------------------------------------------
// Purpose: turn a specific bones boneToWorld transform into a pos and q in parents bonespace
//-----------------------------------------------------------------------------
void SolveBone(
	const CStudioHdr *pStudioHdr,
	int	iBone,
	matrix3x4a_t *pBoneToWorld,
	BoneVector pos[],
	BoneQuaternion q[]
	)
{
	int iParent = pStudioHdr->boneParent( iBone );

	matrix3x4a_t worldToBone;
	MatrixInvert( pBoneToWorld[iParent], worldToBone );

	matrix3x4a_t local;
	ConcatTransforms_Aligned( worldToBone, pBoneToWorld[iBone], local );

	MatrixAngles( local, q[iBone], pos[iBone] );
}


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------

int CIKTarget::GetOwner( void )
{
	return latched.owner;
}

//-----------------------------------------------------------------------------
// Purpose: update the latched IK values that are in a moving frame of reference
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: sets the ground position of an ik target
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: sets the ground "identity" orientation of an ik target
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: sets the ground "identity" orientation of an ik target
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: calculates a ground "identity" orientation based on the surface
//			normal of the ground and the desired ground identity orientation
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: estimates the ground impact at the center location assuming a the edge of 
//			an Z axis aligned disc collided with it the surface.
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------



//-----------------------------------------------------------------------------
// Purpose: Invalidate any IK locks.
//-----------------------------------------------------------------------------

void CIKContext::ClearTargets( void )
{
	int i;
	for (i = 0; i < m_target.size(); i++)
	{
		m_target[i].latched.iFramecounter = -9999;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Run through the rules that survived and turn a specific bones boneToWorld 
//			transform into a pos and q in parents bonespace
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Purpose: insert release rules if the ik rules were in error
//-----------------------------------------------------------------------------



void CIKContext::SolveDependencies( BoneVector pos[], BoneQuaternion q[], matrix3x4a_t boneToWorld[], CBoneBitList &boneComputed	)
{
	matrix3x4a_t worldTarget;
	int i, j;

	ikchainresult_t chainResult[32]; // allocate!!!

	// init chain rules
	for( i = 0; i < m_pStudioHdr->numikchains(); i++ )
	{
		mstudioikchain_t *pchain = m_pStudioHdr->pIKChain( i );
		ikchainresult_t *pChainResult = &chainResult[ i ];
		int bone = pchain->pLink( 2 )->bone;

		pChainResult->target = -1;
		pChainResult->flWeight = 0.0f;

		// don't bother with chain if the bone isn't going to be calculated
		if ( !HasFlag(m_pStudioHdr->boneFlags( bone ), m_boneMask))
			continue;

		// eval current ik'd bone
		BuildBoneChain( pos, q, bone, boneToWorld, boneComputed );

		MatrixAngles( boneToWorld[bone], pChainResult->q, pChainResult->pos );
	}

	for( j = 0; j < m_ikChainRule.size(); j++ )
	{
		for( i = 0; i < m_ikChainRule.at( j ).size(); i++ )
		{
			ikcontextikrule_t *pRule = &m_ikChainRule.at( j ).at( i );
			ikchainresult_t *pChainResult = &chainResult[ pRule->chain ];
			pChainResult->target = -1;


			switch( pRule->type )
			{
			case IkRuleType::Self:
				{
					// xform IK target error into world space
					matrix3x4a_t local;
					QuaternionMatrix( pRule->q, pRule->pos, local );
					// eval target bone space
					if (pRule->bone != -1)
					{
						BuildBoneChain( pos, q, pRule->bone, boneToWorld, boneComputed );
						ConcatTransforms_Aligned( boneToWorld[pRule->bone], local, worldTarget );
					}
					else
					{
						ConcatTransforms_Aligned( m_rootxform, local, worldTarget );
					}

					float flWeight = pRule->flWeight * pRule->flRuleWeight;
					pChainResult->flWeight = pChainResult->flWeight * (1.0f - flWeight) + flWeight;

					Vector p2;
					Quaternion q2;

					// target p and q
					MatrixAngles( worldTarget, q2, p2 );

					// debugLine( pChainResult->pos, p2, 0, 0, 255, true, 0.1 );

					// blend in position and angles
					pChainResult->pos = pChainResult->pos * (1.0f - flWeight) + p2 * flWeight;
					QuaternionSlerp( pChainResult->q, q2, flWeight, pChainResult->q );
				}
				break;
			case IkRuleType::World:
				Assert( 0 );
				break;

			case IkRuleType::Attachment:
				break;

			case IkRuleType::Ground:
				break;

			case IkRuleType::Release:
				{
					// move target back towards original location
					float flWeight = pRule->flWeight * pRule->flRuleWeight;
					mstudioikchain_t *pchain = m_pStudioHdr->pIKChain( pRule->chain );
					int bone = pchain->pLink( 2 )->bone;

					Vector p2;
					Quaternion q2;

					BuildBoneChain( pos, q, bone, boneToWorld, boneComputed );
					MatrixAngles( boneToWorld[bone], q2, p2 );

					// blend in position and angles
					pChainResult->pos = pChainResult->pos * (1.0 - flWeight) + p2 * flWeight;
					QuaternionSlerp( pChainResult->q, q2, flWeight, pChainResult->q );
				}
				break;
			case IkRuleType::Unlatch:
				{
					/*
					pChainResult->flWeight = pChainResult->flWeight * (1 - pRule->flWeight) + pRule->flWeight;

					pChainResult->pos = pChainResult->pos * (1.0 - pRule->flWeight ) + pChainResult->local.pos * pRule->flWeight;
					QuaternionSlerp( pChainResult->q, pChainResult->local.q, pRule->flWeight, pChainResult->q );
					*/
				}
				break;
			}
		}
	}

	for (i = 0; i < m_target.size(); i++)
	{
		CIKTarget *pTarget = &m_target[i];

		if (m_target[i].est.flWeight > 0.0)
		{
			matrix3x4a_t worldFootpad;
			matrix3x4a_t local;
			//mstudioikchain_t *pchain = m_pStudioHdr->pIKChain( m_target[i].chain );
			ikchainresult_t *pChainResult = &chainResult[ pTarget->chain ];

			AngleMatrix( RadianEuler(pTarget->offset.q), pTarget->offset.pos, local );

			AngleMatrix( RadianEuler(pTarget->est.q), pTarget->est.pos, worldFootpad );

			ConcatTransforms_Aligned( worldFootpad, local, worldTarget );

			Vector p2;
			Quaternion q2;
			// target p and q
			MatrixAngles( worldTarget, q2, p2 );
			// MatrixAngles( worldTarget, pChainResult->q, pChainResult->pos );

			// blend in position and angles
			pChainResult->flWeight = pTarget->est.flWeight;
			pChainResult->pos = pChainResult->pos * (1.0 - pChainResult->flWeight ) + p2 * pChainResult->flWeight;
			QuaternionSlerp( pChainResult->q, q2, pChainResult->flWeight, pChainResult->q );
		}

		if (pTarget->latched.bNeedsLatch)
		{
			// keep track of latch position
			pTarget->latched.bHasLatch = true;
			pTarget->latched.q = pTarget->est.q;
			pTarget->latched.pos = pTarget->est.pos;
		}
	}

	for (i = 0; i < m_pStudioHdr->numikchains(); i++)
	{
		ikchainresult_t *pChainResult = &chainResult[ i ];
		mstudioikchain_t *pchain = m_pStudioHdr->pIKChain( i );

		if (pChainResult->flWeight > 0.0)
		{
			Vector tmp;
			MatrixPosition( boneToWorld[pchain->pLink( 2 )->bone], tmp );
			// debugLine( pChainResult->pos, tmp, 255, 255, 255, true, 0.1 );

			// do exact IK solution
			// FIXME: once per link!
			if (Studio_SolveIK(pchain, pChainResult->pos, boneToWorld ))
			{
				Vector p3;
				MatrixGetColumn( boneToWorld[pchain->pLink( 2 )->bone], 3, p3 );
				QuaternionMatrix( pChainResult->q, p3, boneToWorld[pchain->pLink( 2 )->bone] );

				// rebuild chain
				// FIXME: is this needed if everyone past this uses the boneToWorld array?
				SolveBone( m_pStudioHdr, pchain->pLink( 2 )->bone, boneToWorld, pos, q );
				SolveBone( m_pStudioHdr, pchain->pLink( 1 )->bone, boneToWorld, pos, q );
				SolveBone( m_pStudioHdr, pchain->pLink( 0 )->bone, boneToWorld, pos, q );
			}
			else
			{
				// FIXME: need to invalidate the targets that forced this...
				if (pChainResult->target != -1)
				{
					CIKTarget *pTarget = &m_target[pChainResult->target];
					VectorScale( pTarget->latched.deltaPos, 0.8, pTarget->latched.deltaPos );
					QuaternionScale( pTarget->latched.deltaQ, 0.8, pTarget->latched.deltaQ );
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------


void CIKContext::SolveLock(
	const mstudioiklock_t *plock,
	int i,
	BoneVector pos[],
	BoneQuaternion q[],
	matrix3x4a_t boneToWorld[],
	CBoneBitList &boneComputed
	)
{
	mstudioikchain_t *pchain = m_pStudioHdr->pIKChain( plock->chain );
	int bone = pchain->pLink( 2 )->bone;

	// don't bother with iklock if the bone isn't going to be calculated
	if ( !HasFlag(m_pStudioHdr->boneFlags( bone ), m_boneMask))
		return;

	// eval current ik'd bone
	BuildBoneChain( pos, q, bone, boneToWorld, boneComputed );

	Vector p1, p2, p3;
	Quaternion q2, q3;

	// current p and q
	MatrixPosition( boneToWorld[bone], p1 );

	// blend in position
	p3 = p1 * (1.0 - plock->flPosWeight ) + m_ikLock[i].pos * plock->flPosWeight;

	// do exact IK solution
	if (m_ikLock[i].kneeDir.LengthSqr() > 0)
	{
		Studio_SolveIK(pchain->pLink( 0 )->bone, pchain->pLink( 1 )->bone, pchain->pLink( 2 )->bone, p3, m_ikLock[i].kneePos, m_ikLock[i].kneeDir, boneToWorld );
	}
	else
	{
		Studio_SolveIK(pchain, p3, boneToWorld );
	}

	// slam orientation
	MatrixPosition( boneToWorld[bone], p3 );
	QuaternionMatrix( m_ikLock[i].q, p3, boneToWorld[bone] );

	// rebuild chain
	q2 = q[ bone ];
	SolveBone( m_pStudioHdr, pchain->pLink( 2 )->bone, boneToWorld, pos, q );
	QuaternionSlerp( q[bone], q2, plock->flLocalQWeight, q[bone] );

	SolveBone( m_pStudioHdr, pchain->pLink( 1 )->bone, boneToWorld, pos, q );
	SolveBone( m_pStudioHdr, pchain->pLink( 0 )->bone, boneToWorld, pos, q );
}


