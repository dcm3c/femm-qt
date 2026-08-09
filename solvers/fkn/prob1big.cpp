// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

#include "StdAfx.h"
#include<stdio.h>
#include<math.h>
#include "fkn.h"
#include "fknDlg.h"
#include "complex.h"
#include "mesh.h"
#include "spars.h"
#include "femmedoccore.h"
#include "mag_dir_eval.h"
#include "utf8_io.h"
#include "fastio.h"
#include "band_interp.h"

double Power(double x, int y)
{
	return pow(x,(double) y);
}

// The conductivity that participates in transient eddy currents, in MS/m.
//
// This must be the single source of truth: the same value has to decide
// whether a circuit needs a voltage unknown at all, scale the sigma/DeltaT
// mass term, scale the circuit coupling, and scale the total-current
// equation. If those disagree, a region can end up with a voltage row whose
// diagonal is zero (singular) or a current equation inconsistent with the
// matrix it is coupled to. prob2big has exactly that divergence latent --
// its mass term at :458-468 zeroes Cduct for wound and in-plane-laminated
// blocks while its Case-2 coupling at :535 uses the raw value.
//
// Exclusions mirror the harmonic solver: a stranded/wound region carries no
// bulk eddy current (its strands are individually insulated), and an
// in-plane laminated region accounts for eddy currents through a
// frequency-dependent permeability instead.
double CFemmeDocCore::EddyCduct(int blk, int lbl)
{
	if (labellist[lbl].bIsWound) return 0;                                  // prob2big:468
	if ((blockproplist[blk].LamType==0) && (blockproplist[blk].Lam_d>0)) return 0; // prob2big:463-464
	return blockproplist[blk].Cduct;
}

int CFemmeDocCore::Static2D(CBigLinProb &L)
{
	int i,j,k,w,s;
	double Me[3][3],be[3],beCirc[3];		// element matrices;
	double Mx[3][3],My[3][3],Mxy[3][3],Mn[3][3];
	double l[3],p[3],q[3];		// element shape parameters;
	int n[3];					// numbers of nodes for a particular element;
	double a,K,Ki,r,t=0,x,y,B,B1,B2,mu,v[3],u[3],dv,res,lastres=0,Cduct;
	double *V_old,*CircInt1=0,*CircInt2=0,*CircInt3=0,*CircInt2t=0;
	double c=PI*4.e-05;
	double units[]={2.54,0.1,1.,100.,0.00254,1.e-04};
	int Iter=0,pctr;
	int LinearFlag=TRUE;
	int bIncremental = FALSE;
	int bRestart = FALSE;
	double murel, muinc;

	if (PrevSoln.GetLength() > 0) bIncremental = PrevType;
	res=0;

	// A transient (backward-Euler) analysis. Independent of bIncremental:
	// PrevType keeps meaning incremental/frozen permeability, and Aprev here
	// is the previous TIME STEP rather than a linearization base point.
	const int bTransient = (DeltaT>0);

	// First row of the transient voltage-unknown block. The flux- and
	// current-constraint rows keep the indices they have always had; the
	// voltage rows are appended after them (see SetNumNodesPlus).
	const int VoltRowBase = NumNodes + NumFluxConstraints + NumCurrentConstraints;

	CElement *El;
	V_old=(double *) calloc(NumNodesPlus,sizeof(double));

	// On the mesh-reuse path, LoadPrev() has already parsed the previous
	// solution -- Aprev is filled and the node numbering is this mesh's own
	// by construction. Re-reading the file via LoadPrevA below would be a
	// second full parse of the node section for identical data.
	const int bAprevLoaded = (bTransient && Aprev!=NULL);

	// Aprev is normally allocated by LoadPrev() on the incremental path. A
	// transient run needs it regardless, and zero-filled is the right default:
	// no previous solution means a zero initial condition.
	if ((bTransient) && (Aprev==NULL))
		Aprev=(double *) calloc(NumNodes,sizeof(double));

	for(i=0;i<NumBlockLabels;i++) GetFillFactor(i);

	// check to see if any circuits have been defined and process them;
	if (NumCircProps>0)
	{
		CircInt1=(double *)calloc(NumCircProps,sizeof(double));
		CircInt2=(double *)calloc(NumCircProps,sizeof(double));
		CircInt3=(double *)calloc(NumCircProps,sizeof(double));
		// Conductivity integral for the transient classification. Kept
		// separate from CircInt2 rather than replacing it: CircInt2 zeroes
		// only for wound labels, so using it to decide which circuits get a
		// voltage unknown would hand one to an in-plane laminated region --
		// whose eddy currents live in its permeability instead, giving that
		// row a zero diagonal and a singular matrix. Leaving CircInt2 byte
		// identical also guarantees DC results cannot move.
		CircInt2t=(double *)calloc(NumCircProps,sizeof(double));
		for(i=0;i<NumEls;i++){
			if(meshele[i].lbl>=0)
			if(labellist[meshele[i].lbl].InCircuit!=-1){
				El=&meshele[i];
				
				// get element area;
				for(k=0;k<3;k++) n[k]=El->p[k];
				p[0]=meshnode[n[1]].y - meshnode[n[2]].y;
				p[1]=meshnode[n[2]].y - meshnode[n[0]].y;
				p[2]=meshnode[n[0]].y - meshnode[n[1]].y;	
				q[0]=meshnode[n[2]].x - meshnode[n[1]].x;
				q[1]=meshnode[n[0]].x - meshnode[n[2]].x;
				q[2]=meshnode[n[1]].x - meshnode[n[0]].x;
				a=(p[0]*q[1]-p[1]*q[0])/2.;
			//	r=(meshnode[n[0]].x+meshnode[n[1]].x+meshnode[n[2]].x)/3.;

				// if coils are wound, they act like they have
				// a zero "bulk" conductivity...
				Cduct=blockproplist[El->blk].Cduct;
				if (labellist[El->lbl].bIsWound) Cduct=0;
				
				// evaluate integrals;
				CircInt1[labellist[El->lbl].InCircuit]+=a;
				CircInt2[labellist[El->lbl].InCircuit]+=
					a*Cduct;
				CircInt2t[labellist[El->lbl].InCircuit]+=
					a*EddyCduct(El->blk,El->lbl);
				CircInt3[labellist[El->lbl].InCircuit]+=
					blockproplist[El->blk].Jr*a*100.;
			}
		}

		for(i=0;i<NumCircProps;i++)
		{
			// Case 0 :: voltage gradient is applied to the region;
			// Case 1 :: flat current density is applied to the region;
			// Case 2 :: voltage gradient applied to the region, but it is an
			//           unknown we have to solve for (transient only).
			//
			// At DC a conducting region in a current-driven circuit is Case 0
			// with dV computed analytically below, which is exact because DC
			// current distributes uniformly across the section. Eddy currents
			// are precisely what breaks that premise, so a transient makes the
			// voltage gradient a matrix unknown instead -- prob2big:157-161.
			// CircType 2 (flux-constrained) is included: a solid region in a
			// flux circuit -- a cage bar tied by end rings -- participates
			// through its voltage gradient instead of a current-density
			// shape. Because the series expansion gives every block label its
			// own circuit entry sharing one FluxCirc, a circuit mixing wound
			// and solid parts classifies each part separately and both feed
			// the same flux row. See the coupling below.
			if (bTransient &&
			    ((circproplist[i].CircType==0) || (circproplist[i].CircType==2)) &&
			    (CircInt2t[i]!=0))
			{
				circproplist[i].Case=2;
			}
			else if ((circproplist[i].CircType==0) || (circproplist[i].CircType==2)) // have to do this
			{
				if(CircInt2[i]==0){
					circproplist[i].Case=1;
					if(CircInt1[i]==0.) circproplist[i].J=0.;
					else circproplist[i].J=0.01*(circproplist[i].Amps_re -
						CircInt3[i])/CircInt1[i];
				}
				else{ 
					circproplist[i].Case=0;
					circproplist[i].dV=-0.01*(circproplist[i].Amps_re - 
						CircInt3[i])/CircInt2[i];
				}
			}
			else{ // does this ever get invoked?
				circproplist[i].Case=0;
				circproplist[i].dV=circproplist[i].dVolts_re;
			}
		}
	}





	// first, need to define permeability in each block.  In nonlinear
	// case, this is sort of a hassle.  Linear permeability is simply
	// copied from the associated block definition, but nonlinear
	// permeability must be updated from iteration to iteration...

	// build element matrices using the matrices derived in Allaire's book.

do{

	TheView->SetDlgItemText(IDC_FRAME1,"Matrix Construction");
	TheView->m_prog1.SetPos(0);
	pctr=0;

	// Split assembly (see prob4big for the full story): pass 1 assembles the
	// V-independent contributions (air gap elements, constraints, linear
	// elements), snapshots them, and adds the nonlinear elements; later
	// passes restore the snapshot and reassemble only the nonlinear elements.
	int baselineOK=1;
	if (Iter==1) L.Wipe();
	else if (Iter>1){
		if (L.RestoreBaseline()==0){ L.Wipe(); baselineOK=0; }
	}
	else{

		// could be starting from a previous solution; load up the initial guess from Aprev
		if (bIncremental==1)
		{
			// try to restart from a previous file of the same name
			// to cut down on the time to solve the incremental problem
			CString myFile;
			myFile.Format("%s.ans",PathName);
			if (LoadPrevA(myFile,L.V)) bRestart = 2;
		}
		else if ((bIncremental==0) && (PrevSoln.GetLength()>0))
		{
			// load initial guess from the explicitly specified previous solution
			// -- and, in a transient analysis, keep it: it is the backward-Euler
			// A_prev, data rather than merely a starting point. When LoadPrev
			// already parsed the file (mesh reuse), build the guess from the
			// in-memory copy instead of reading the file a second time.
			if (bAprevLoaded){
				for(i=0;i<NumNodes;i++) L.V[i]=Aprev[i]/c;
				bRestart=1;
			}
			else
			bRestart = LoadPrevA(PrevSoln,L.V,Aprev);

			// A transient step that cannot read its previous solution would
			// otherwise march from A_prev == 0 with no diagnostic, which looks
			// like a plausible answer to the wrong problem. LoadPrevA returns
			// FALSE on a node-count mismatch or a non-DC previous file.
			if ((bTransient) && (bRestart==0))
			{
				MsgBox("Transient step: could not read the previous solution\n"
				       "\"%s\".\nIt must be a DC solution on the same mesh.",
				       (const char *)PrevSoln);
				return FALSE;
			}

			if (bRestart)
			{
				// Crib initial condition for flux constrain currents from the currents specified in the input file
				// Probably the wrong way to do this, but it's easy...
				for (i=0;i<NumFluxConstraints;i++)
					L.V[i + NumNodes] = circproplist[fluxproplist[i]].Amps_re; 
			}
		}
	}

	int sweepA, sweepB;
	if (Iter==0 || !baselineOK){ sweepA=0; sweepB=0; }
	else if (Iter==1){ sweepA=1; sweepB=2; }
	else { sweepA=2; sweepB=2; }

	for (int sweepSel=sweepA; sweepSel<=sweepB; sweepSel++){
	if (sweepSel==2 && Iter==1) L.SaveBaseline();

	if (sweepSel<=1){    // V-independent contributions: baseline only

	// first, tack in air gap element contributions
	for(i=0;i<NumAGEs;i++)
	{
		double MG[10][10];
		double ci,co;
		int nn[10];
		double ww[10];
		double dt;

		// K = dr/(R*dtta)
		dt=(PI/180.)*(agelist[i].totalArcLength/agelist[i].totalArcElements);
		K=2.*(agelist[i].ro-agelist[i].ri)/
		   (dt*(agelist[i].ro+agelist[i].ri));
		Ki=1./K;
		ci=agelist[i].InnerShift;
		co=agelist[i].OuterShift;

		if (ci>co)
		{
			ci=ci-co;
			co=0;
		}
		else{
			ci=1-co+ci;
			co=1;
		}

		// build the element matrix for each quad element in the annulus (same for each element)
		// matrix for quad element derived from serendipity element
#ifndef QUINTIC
		MG[0][0] = (5*Power (-1 + ci,2)*Power (ci,4)*(K + Ki))/48.;
		MG[0][1] = -((-1 + ci)*Power (ci,3)*(5*(-1 + ci*(-5 + 4*ci))*K + (-5 + ci*(-19 + 14*ci))*Ki))/48.;
		MG[0][2] = ((-1 + ci)*Power (ci,2)*(5*(2 + ci*(-1 - 9*ci + 6*Power (ci,2)))*K + (10 + ci*(1 + 3*ci*(-7 + 4*ci)))*Ki))/48.;
		MG[0][3] = -(Power (-1 + ci,2)*Power (ci,2)*(5*(-2 + ci*(-3 + 4*ci))*K + (2 + ci*(-3 + 2*ci))*Ki))/48.;
		MG[0][4] = (Power (-1 + ci,3)*Power (ci,3)*(5*K - Ki))/48.;
		MG[0][5] = ((-1 + ci)*Power (ci,2)*(-1 + co)*Power (co,2)*(K - 5*Ki))/48.;
		MG[0][6] = -((-1 + ci)*Power (ci,2)*co*((-1 + co*(-5 + 4*co))*K + (5 + (19 - 14*co)*co)*Ki))/48.;
		MG[0][7] = ((-1 + ci)*Power (ci,2)*((2 + co*(-1 - 9*co + 6*Power (co,2)))*K - (10 + co*(1 + 3*co*(-7 + 4*co)))*Ki))/48.;
		MG[0][8] = -((-1 + ci)*Power (ci,2)*(-1 + co)*((-2 + co*(-3 + 4*co))*K + (-2 + (3 - 2*co)*co)*Ki))/48.;
		MG[0][9] = ((-1 + ci)*Power (ci,2)*Power (-1 + co,2)*co*(K + Ki))/48.;
		MG[1][1] = (Power (ci,2)*(5*Power (1 + (5 - 4*ci)*ci,2)*K + (5 + ci*(38 + ci*(49 + 4*ci*(-29 + 11*ci))))*Ki))/48.;
		MG[1][2] = (-5*ci*(-1 + 2*ci)*(-2 + 3*(-1 + ci)*ci)*(-1 + ci*(-5 + 4*ci))*K + ci*(10 + ci*(39 - ci*(50 + ci*(85 + 6*ci*(-23 + 8*ci)))))*Ki)/48.;
		MG[1][3] = ((-1 + ci)*ci*(5*(2 + ci*(13 + ci*(3 + 16*(-2 + ci)*ci)))*K + (-2 + 5*ci*(1 + ci*(3 + 4*(-2 + ci)*ci)))*Ki))/48.;
		MG[1][4] = -(Power (-1 + ci,2)*Power (ci,2)*(5*(-1 + ci*(-5 + 4*ci))*K + Ki + ci*(-1 + 2*ci)*Ki))/48.;
		MG[1][5] = -(ci*(-1 + co)*Power (co,2)*((-1 + ci*(-5 + 4*ci))*K + (5 + (19 - 14*ci)*ci)*Ki))/48.;
		MG[1][6] = (ci*co*((-1 + ci*(-5 + 4*ci))*(-1 + co*(-5 + 4*co))*K + (-5 + ci*(-19 + 14*ci) - 19*co + ci*(-77 + 58*ci)*co + 2*(7 + (29 - 22*ci)*ci)*Power (co,2))*Ki))/48.;
		MG[1][7] = (-(ci*(-1 + ci*(-5 + 4*ci))*(2 + co*(-1 - 9*co + 6*Power (co,2)))*K) + ci*(-10 + co*(-1 + 3*(7 - 4*co)*co) + ci*(-38 + co + 99*Power (co,2) - 60*Power (co,3)) + Power (ci,2)*(28 + 2*co*(-1 + 3*co*(-13 + 8*co))))*Ki)/48.;
		MG[1][8] = (ci*(-1 + co)*((-1 + ci*(-5 + 4*ci))*(-2 + co*(-3 + 4*co))*K + (2 + co*(-3 + 2*co) + Power (ci,2)*(4 + 2*(9 - 10*co)*co) + ci*(-2 + co*(-21 + 22*co)))*Ki))/48.;
		MG[1][9] = -(ci*Power (-1 + co,2)*co*((-1 + ci*(-5 + 4*ci))*K + (-1 + ci - 2*Power (ci,2))*Ki))/48.;
		MG[2][2] = (5*Power (-2 + ci + 9*Power (ci,2) - 6*Power (ci,3),2)*K + (20 + (-1 + ci)*ci*(-4 + 3*(-1 + ci)*ci*(-25 + 24*(-1 + ci)*ci)))*Ki)/48.;
		MG[2][3] = (-5*(4 + Power (ci,2)*(-33 + ci*(18 + ci*(65 + 6*ci*(-13 + 4*ci)))))*K + (4 + Power (ci,2)*(39 - ci*(30 + ci*(115 + 6*ci*(-25 + 8*ci)))))*Ki)/48.;
		MG[2][4] = (Power (-1 + ci,2)*ci*(5*(2 + ci*(-1 - 9*ci + 6*Power (ci,2)))*K + (-2 + ci*(-5 + 3*ci*(-5 + 4*ci)))*Ki))/48.;
		MG[2][5] = ((-1 + co)*Power (co,2)*((2 + ci*(-1 - 9*ci + 6*Power (ci,2)))*K - (10 + ci*(1 + 3*ci*(-7 + 4*ci)))*Ki))/48.;
		MG[2][6] = (-((2 + ci*(-1 - 9*ci + 6*Power (ci,2)))*co*(-1 + co*(-5 + 4*co))*K) + co*(-10 - 38*co + 28*Power (co,2) + Power (ci,2)*(21 + 99*co - 78*Power (co,2)) + ci*(-1 + co - 2*Power (co,2)) + 12*Power (ci,3)*(-1 + co*(-5 + 4*co)))*Ki)/48.;
		MG[2][7] = ((2 + ci*(-1 - 9*ci + 6*Power (ci,2)))*(2 + co*(-1 - 9*co + 6*Power (co,2)))*K - (2*(10 + co) + 6*Power (co,2)*(-7 + 4*co) + 3*Power (ci,2)*(-14 + co*(5 + (55 - 36*co)*co)) + ci*(2 + co*(5 + 3*(5 - 4*co)*co)) + 12*Power (ci,3)*(2 + co*(-1 - 9*co + 6*Power (co,2))))*Ki)/48.;
		MG[2][8] = (-((2 + ci*(-1 - 9*ci + 6*Power (ci,2)))*(2 + co - 7*Power (co,2) + 4*Power (co,3))*K) + (-1 + co)*(4 + 2*ci*(5 + 3*(5 - 4*ci)*ci) + 3*(-2 + ci*(3 + (17 - 12*ci)*ci))*co + 2*(2 + ci*(-7 + 3*ci*(-11 + 8*ci)))*Power (co,2))*Ki)/48.;
		MG[2][9] = (Power (-1 + co,2)*co*((2 + ci*(-1 - 9*ci + 6*Power (ci,2)))*K + (2 + ci*(5 + 3*(5 - 4*ci)*ci))*Ki))/48.;
		MG[3][3] = (Power (-1 + ci,2)*(5*Power (2 + (3 - 4*ci)*ci,2)*K + (20 + ci*(36 + ci*(-35 - 60*ci + 44*Power (ci,2))))*Ki))/48.;
		MG[3][4] = -(Power (-1 + ci,3)*ci*(5*(-2 + ci*(-3 + 4*ci))*K + (-10 + ci*(-9 + 14*ci))*Ki))/48.;
		MG[3][5] = -((-1 + ci)*(-1 + co)*Power (co,2)*((-2 + ci*(-3 + 4*ci))*K + (-2 + (3 - 2*ci)*ci)*Ki))/48.;
		MG[3][6] = ((-1 + ci)*co*((-2 + ci*(-3 + 4*ci))*(-1 + co*(-5 + 4*co))*K + (2 + ci*(-3 + 2*ci) - 2*co + ci*(-21 + 22*ci)*co + 2*(2 + (9 - 10*ci)*ci)*Power (co,2))*Ki))/48.;
		MG[3][7] = (-((2 + ci - 7*Power (ci,2) + 4*Power (ci,3))*(2 + co*(-1 - 9*co + 6*Power (co,2)))*K) + (-1 + ci)*(4 + 2*co*(5 + 3*(5 - 4*co)*co) + ci*(-6 + 3*co*(3 + (17 - 12*co)*co)) + 2*Power (ci,2)*(2 + co*(-7 + 3*co*(-11 + 8*co))))*Ki)/48.;
		MG[3][8] = ((-1 + ci)*(-1 + co)*((-2 + ci*(-3 + 4*ci))*(-2 + co*(-3 + 4*co))*K + (-20 + 3*ci*(1 + 2*co)*(-6 + 5*co) + 2*co*(-9 + 14*co) + Power (ci,2)*(28 + 30*co - 44*Power (co,2)))*Ki))/48.;
		MG[3][9] = -((-1 + ci)*Power (-1 + co,2)*co*((-2 + ci*(-3 + 4*ci))*K + (10 + (9 - 14*ci)*ci)*Ki))/48.;
		MG[4][4] = (5*Power (-1 + ci,4)*Power (ci,2)*(K + Ki))/48.;
		MG[4][5] = (Power (-1 + ci,2)*ci*(-1 + co)*Power (co,2)*(K + Ki))/48.;
		MG[4][6] = -(Power (-1 + ci,2)*ci*co*((-1 + co*(-5 + 4*co))*K + (-1 + co - 2*Power (co,2))*Ki))/48.;
		MG[4][7] = (Power (-1 + ci,2)*ci*((2 + co*(-1 - 9*co + 6*Power (co,2)))*K + (2 + co*(5 + 3*(5 - 4*co)*co))*Ki))/48.;
		MG[4][8] = -(Power (-1 + ci,2)*ci*(-1 + co)*((-2 + co*(-3 + 4*co))*K + (10 + (9 - 14*co)*co)*Ki))/48.;
		MG[4][9] = (Power (-1 + ci,2)*ci*Power (-1 + co,2)*co*(K - 5*Ki))/48.;
		MG[5][5] = (5*Power (-1 + co,2)*Power (co,4)*(K + Ki))/48.;
		MG[5][6] = -((-1 + co)*Power (co,3)*(5*(-1 + co*(-5 + 4*co))*K + (-5 + co*(-19 + 14*co))*Ki))/48.;
		MG[5][7] = ((-1 + co)*Power (co,2)*(5*(2 + co*(-1 - 9*co + 6*Power (co,2)))*K + (10 + co*(1 + 3*co*(-7 + 4*co)))*Ki))/48.;
		MG[5][8] = -(Power (-1 + co,2)*Power (co,2)*(5*(-2 + co*(-3 + 4*co))*K + (2 + co*(-3 + 2*co))*Ki))/48.;
		MG[5][9] = (Power (-1 + co,3)*Power (co,3)*(5*K - Ki))/48.;
		MG[6][6] = (Power (co,2)*(5*Power (1 + (5 - 4*co)*co,2)*K + (5 + co*(38 + co*(49 + 4*co*(-29 + 11*co))))*Ki))/48.;
		MG[6][7] = (-5*co*(-1 + 2*co)*(-2 + 3*(-1 + co)*co)*(-1 + co*(-5 + 4*co))*K + co*(10 + co*(39 - co*(50 + co*(85 + 6*co*(-23 + 8*co)))))*Ki)/48.;
		MG[6][8] = ((-1 + co)*co*(5*(2 + co*(13 + co*(3 + 16*(-2 + co)*co)))*K + (-2 + 5*co*(1 + co*(3 + 4*(-2 + co)*co)))*Ki))/48.;
		MG[6][9] = -(Power (-1 + co,2)*Power (co,2)*(5*(-1 + co*(-5 + 4*co))*K + Ki + co*(-1 + 2*co)*Ki))/48.;
		MG[7][7] = (5*Power (-2 + co + 9*Power (co,2) - 6*Power (co,3),2)*K + (20 + (-1 + co)*co*(-4 + 3*(-1 + co)*co*(-25 + 24*(-1 + co)*co)))*Ki)/48.;
		MG[7][8] = (-5*(4 + Power (co,2)*(-33 + co*(18 + co*(65 + 6*co*(-13 + 4*co)))))*K + (4 + Power (co,2)*(39 - co*(30 + co*(115 + 6*co*(-25 + 8*co)))))*Ki)/48.;
		MG[7][9] = (Power (-1 + co,2)*co*(5*(2 + co*(-1 - 9*co + 6*Power (co,2)))*K + (-2 + co*(-5 + 3*co*(-5 + 4*co)))*Ki))/48.;
		MG[8][8] = (Power (-1 + co,2)*(5*Power (2 + (3 - 4*co)*co,2)*K + (20 + co*(36 + co*(-35 - 60*co + 44*Power (co,2))))*Ki))/48.;
		MG[8][9] = -(Power (-1 + co,3)*co*(5*(-2 + co*(-3 + 4*co))*K + (-10 + co*(-9 + 14*co))*Ki))/48.;
		MG[9][9] = (5*Power (-1 + co,4)*Power (co,2)*(K + Ki))/48.;
#else
        MG[0][0] = (5*Power(ci,6)*Power(3 - 5*ci + 2*Power(ci,2),2)*(K + Ki))/48.;
        MG[0][1] = -((-1 + ci)*Power(ci,4)*(-3 + 2*ci)*(5*(1 + ci*(1 + 4*(-1 + ci)*ci*(-3 + 2*ci)))*K + (5 + ci*(5 + 14*(-1 + ci)*ci*(-3 + 2*ci)))*Ki))/48.;
        MG[0][2] = ((-1 + ci)*Power(ci,3)*(-3 + 2*ci)*(5*(-2 + ci*(1 + 3*ci*(1 + 2*(-1 + ci)*ci*(-3 + 2*ci))))*K + (-10 + ci*(-1 + 3*ci*(3 + 4*(-1 + ci)*ci*(-3 + 2*ci))))*Ki))/48.;
        MG[0][3] = -(Power(-1 + ci,2)*Power(ci,3)*(-3 + 2*ci)*(5*(2 + 3*ci - 12*Power(ci,3) + 8*Power(ci,4))*K + (-2 + 3*ci - 6*Power(ci,3) + 4*Power(ci,4))*Ki))/48.;
        MG[0][4] = (Power(-1 + ci,4)*Power(ci,4)*(-3 + 2*ci)*(1 + 2*ci)*(5*K - Ki))/48.;
        MG[0][5] = ((-1 + ci)*Power(ci,3)*(-3 + 2*ci)*(-1 + co)*Power(co,3)*(-3 + 2*co)*(K - 5*Ki))/48.;
        MG[0][6] = -((-1 + ci)*Power(ci,3)*(-3 + 2*ci)*co*((1 + co*(1 + 4*(-1 + co)*co*(-3 + 2*co)))*K - (5 + co*(5 + 14*(-1 + co)*co*(-3 + 2*co)))*Ki))/48.;
        MG[0][7] = ((-1 + ci)*Power(ci,3)*(-3 + 2*ci)*((-2 + co*(1 + 3*co*(1 + 2*(-1 + co)*co*(-3 + 2*co))))*K + 10*Ki + co*(1 - 3*co*(3 + 4*(-1 + co)*co*(-3 + 2*co)))*Ki))/48.;
        MG[0][8] = -((-1 + ci)*Power(ci,3)*(-3 + 2*ci)*(-1 + co)*((2 + 3*co - 12*Power(co,3) + 8*Power(co,4))*K + (2 - 3*co + 6*Power(co,3) - 4*Power(co,4))*Ki))/48.;
        MG[0][9] = ((-1 + ci)*Power(ci,3)*(-3 + 2*ci)*Power(-1 + co,3)*co*(1 + 2*co)*(K + Ki))/48.;
        MG[1][1] = (Power(ci,2)*(5*Power(1 + ci*(1 + 4*(-1 + ci)*ci*(-3 + 2*ci)),2)*K + (5 + ci*(10 + ci*(89 + 4*ci*(-14 + ci*(-3 + 2*ci)*(-26 + 11*ci*(8 + ci*(-7 + 2*ci)))))))*Ki))/48.;
        MG[1][2] = (-5*ci*(-1 + 2*ci)*(1 + ci*(1 + 4*(-1 + ci)*ci*(-3 + 2*ci)))*(2 + 3*ci*(1 + ci*(1 + 2*(-2 + ci)*ci)))*K + ci*(10 + ci*(11 + ci*(76 - ci*(191 + 2*Power(ci,2)*(125 + 6*ci*(-113 + 4*ci*(37 + 4*(-5 + ci)*ci)))))))*Ki)/48.;
        MG[1][3] = ((-1 + ci)*ci*(5*(2 + 3*ci - 12*Power(ci,3) + 8*Power(ci,4))*(1 + ci*(1 + 4*(-1 + ci)*ci*(-3 + 2*ci)))*K + (-2 + (-1 + ci)*ci*(-1 + 4*(-1 + ci)*ci*(4 + 5*ci*(3 + ci*(1 + 4*(-2 + ci)*ci)))))*Ki))/48.;
        MG[1][4] = -(Power(-1 + ci,3)*Power(ci,2)*(1 + 2*ci)*(5*(1 + ci*(1 + 4*(-1 + ci)*ci*(-3 + 2*ci)))*K + (1 + 2*(-1 + ci)*ci)*(-1 + ci*(-3 + 2*ci))*Ki))/48.;
        MG[1][5] = -(ci*(-1 + co)*Power(co,3)*(-3 + 2*co)*((1 + ci*(1 + 4*(-1 + ci)*ci*(-3 + 2*ci)))*K - (5 + ci*(5 + 14*(-1 + ci)*ci*(-3 + 2*ci)))*Ki))/48.;
        MG[1][6] = (ci*co*((1 + ci*(1 + 4*(-1 + ci)*ci*(-3 + 2*ci)))*(1 + co*(1 + 4*(-1 + co)*co*(-3 + 2*co)))*K - (5 + co*(5 + 14*(-1 + co)*co*(-3 + 2*co)) + ci*(5 + co*(5 + 14*(-1 + co)*co*(-3 + 2*co))) + 6*Power(ci,2)*(7 + co*(7 + 22*(-1 + co)*co*(-3 + 2*co))) - 10*Power(ci,3)*(7 + co*(7 + 22*(-1 + co)*co*(-3 + 2*co))) + 4*Power(ci,4)*(7 + co*(7 + 22*(-1 + co)*co*(-3 + 2*co))))*Ki))/48.;
        MG[1][7] = -(ci*((1 + ci*(1 + 4*(-1 + ci)*ci*(-3 + 2*ci)))*(-2 + co*(1 + 3*co*(1 + 2*(-1 + co)*co*(-3 + 2*co))))*K + (10 + 2*ci*(5 + 14*(-1 + ci)*ci*(-3 + 2*ci)) + co + ci*(1 - 2*(-1 + ci)*ci*(-3 + 2*ci))*co - 3*(3 + ci*(3 + 10*(-1 + ci)*ci*(-3 + 2*ci)))*Power(co,2) - 36*(1 + ci*(1 + 4*(-1 + ci)*ci*(-3 + 2*ci)))*Power(co,3) + 60*(1 + ci*(1 + 4*(-1 + ci)*ci*(-3 + 2*ci)))*Power(co,4) - 24*(1 + ci*(1 + 4*(-1 + ci)*ci*(-3 + 2*ci)))*Power(co,5))*Ki))/48.;
        MG[1][8] = (ci*(-1 + co)*((1 + ci*(1 + 4*(-1 + ci)*ci*(-3 + 2*ci)))*(2 + 3*co - 12*Power(co,3) + 8*Power(co,4))*K - (-2 + 2*ci*(-1 + 2*ci)*(1 + 2*(-2 + ci)*ci) + 3*co + 3*ci*(1 + 6*(-1 + ci)*ci*(-3 + 2*ci))*co - 6*(1 + ci*(1 + 10*(-1 + ci)*ci*(-3 + 2*ci)))*Power(co,3) + 4*(1 + ci*(1 + 10*(-1 + ci)*ci*(-3 + 2*ci)))*Power(co,4))*Ki))/48.;
        MG[1][9] = -(ci*Power(-1 + co,3)*co*(1 + 2*co)*((1 + ci*(1 + 4*(-1 + ci)*ci*(-3 + 2*ci)))*K + Ki + ci*(1 - 2*(-1 + ci)*ci*(-3 + 2*ci))*Ki))/48.;
        MG[2][2] = (5*Power(-2 + ci*(1 + 3*ci*(1 + 2*(-1 + ci)*ci*(-3 + 2*ci))),2)*K + (20 + ci*(4 + ci*(-31 + 3*ci*(-46 + ci*(111 + 8*Power(ci,2)*(14 + 3*ci*(-28 + ci*(37 + 4*(-5 + ci)*ci))))))))*Ki)/48.;
        MG[2][3] = ((-1 + ci)*(-5*(-1 + 2*ci)*(2 + 3*ci - 12*Power(ci,3) + 8*Power(ci,4))*(2 + 3*ci*(1 + ci*(1 + 2*(-2 + ci)*ci)))*K - (4 + ci*(4 + ci*(15 + ci*(99 + 2*ci*(-31 + ci*(-79 + 6*ci*(-27 + 4*ci*(21 + 4*(-4 + ci)*ci))))))))*Ki))/48.;
        MG[2][4] = (Power(-1 + ci,3)*ci*(1 + 2*ci)*(5*(-2 + ci*(1 + 3*ci*(1 + 2*(-1 + ci)*ci*(-3 + 2*ci))))*K + (2 + ci*(5 + 3*ci*(1 + 4*(-1 + ci)*ci*(-3 + 2*ci))))*Ki))/48.;
        MG[2][5] = ((-1 + co)*Power(co,3)*(-3 + 2*co)*((-2 + ci*(1 + 3*ci*(1 + 2*(-1 + ci)*ci*(-3 + 2*ci))))*K + 10*Ki + ci*(1 - 3*ci*(3 + 4*(-1 + ci)*ci*(-3 + 2*ci)))*Ki))/48.;
        MG[2][6] = -(co*((-2 + ci*(1 + 3*ci*(1 + 2*(-1 + ci)*ci*(-3 + 2*ci))))*(1 + co*(1 + 4*(-1 + co)*co*(-3 + 2*co)))*K + (ci*(1 + co - 6*Power(co,2) + 10*Power(co,3) - 4*Power(co,4)) - 36*Power(ci,3)*(1 + co*(1 + 4*(-1 + co)*co*(-3 + 2*co))) + 60*Power(ci,4)*(1 + co*(1 + 4*(-1 + co)*co*(-3 + 2*co))) - 24*Power(ci,5)*(1 + co*(1 + 4*(-1 + co)*co*(-3 + 2*co))) - 3*Power(ci,2)*(3 + co*(3 + 10*(-1 + co)*co*(-3 + 2*co))) + 2*(5 + co*(5 + 14*(-1 + co)*co*(-3 + 2*co))))*Ki))/48.;
        MG[2][7] = ((-2 + ci*(1 + 3*ci*(1 + 2*(-1 + ci)*ci*(-3 + 2*ci))))*(-2 + co*(1 + 3*co*(1 + 2*(-1 + co)*co*(-3 + 2*co))))*K - (2*(10 + co) - 6*Power(co,2)*(3 + 4*(-1 + co)*co*(-3 + 2*co)) + 36*Power(ci,3)*(-2 + co*(1 + 3*co*(1 + 2*(-1 + co)*co*(-3 + 2*co)))) - 60*Power(ci,4)*(-2 + co*(1 + 3*co*(1 + 2*(-1 + co)*co*(-3 + 2*co)))) + 24*Power(ci,5)*(-2 + co*(1 + 3*co*(1 + 2*(-1 + co)*co*(-3 + 2*co)))) + ci*(2 + co*(5 + 3*co*(1 + 4*(-1 + co)*co*(-3 + 2*co)))) + 3*Power(ci,2)*(-6 + co*(1 + co*(7 + 12*(-1 + co)*co*(-3 + 2*co)))))*Ki)/48.;
        MG[2][8] = -((-1 + co)*((-2 + ci*(1 + 3*ci*(1 + 2*(-1 + ci)*ci*(-3 + 2*ci))))*(2 + 3*co - 12*Power(co,3) + 8*Power(co,4))*K - (4 + 2*ci*(5 + 3*ci*(1 + 4*(-1 + ci)*ci*(-3 + 2*ci))) - 6*co + 3*ci*(3 + ci*(5 + 12*(-1 + ci)*ci*(-3 + 2*ci)))*co - 6*(-2 + ci*(7 + 3*ci*(3 + 8*(-1 + ci)*ci*(-3 + 2*ci))))*Power(co,3) + 4*(-2 + ci*(7 + 3*ci*(3 + 8*(-1 + ci)*ci*(-3 + 2*ci))))*Power(co,4))*Ki))/48.;
        MG[2][9] = (Power(-1 + co,3)*co*(1 + 2*co)*((-2 + ci*(1 + 3*ci*(1 + 2*(-1 + ci)*ci*(-3 + 2*ci))))*K - (2 + ci*(5 + 3*ci*(1 + 4*(-1 + ci)*ci*(-3 + 2*ci))))*Ki))/48.;
        MG[3][3] = (Power(-1 + ci,2)*(5*Power(2 + 3*ci - 12*Power(ci,3) + 8*Power(ci,4),2)*K + (20 + ci*(36 + ci*(21 + 4*ci*(-3 + 2*ci)*(14 + 15*ci - 33*Power(ci,3) + 22*Power(ci,4)))))*Ki))/48.;
        MG[3][4] = -(Power(-1 + ci,4)*ci*(1 + 2*ci)*(5*(2 + 3*ci - 12*Power(ci,3) + 8*Power(ci,4))*K + (10 + 9*ci - 42*Power(ci,3) + 28*Power(ci,4))*Ki))/48.;
        MG[3][5] = -((-1 + ci)*(-1 + co)*Power(co,3)*(-3 + 2*co)*((2 + 3*ci - 12*Power(ci,3) + 8*Power(ci,4))*K + (2 - 3*ci + 6*Power(ci,3) - 4*Power(ci,4))*Ki))/48.;
        MG[3][6] = ((-1 + ci)*co*((2 + 3*ci - 12*Power(ci,3) + 8*Power(ci,4))*(1 + co*(1 + 4*(-1 + co)*co*(-3 + 2*co)))*K - (-2*(1 + co) + 4*(-1 + co)*Power(co,2)*(-3 + 2*co) + 3*ci*(1 + co*(1 + 6*(-1 + co)*co*(-3 + 2*co))) - 6*Power(ci,3)*(1 + co*(1 + 10*(-1 + co)*co*(-3 + 2*co))) + 4*Power(ci,4)*(1 + co*(1 + 10*(-1 + co)*co*(-3 + 2*co))))*Ki))/48.;
        MG[3][7] = -((-1 + ci)*((2 + 3*ci - 12*Power(ci,3) + 8*Power(ci,4))*(-2 + co*(1 + 3*co*(1 + 2*(-1 + co)*co*(-3 + 2*co))))*K - (4 + 10*co + 6*Power(co,2)*(1 + 4*(-1 + co)*co*(-3 + 2*co)) - 6*Power(ci,3)*(-2 + co*(7 + 3*co*(3 + 8*(-1 + co)*co*(-3 + 2*co)))) + 4*Power(ci,4)*(-2 + co*(7 + 3*co*(3 + 8*(-1 + co)*co*(-3 + 2*co)))) + 3*ci*(-2 + co*(3 + co*(5 + 12*(-1 + co)*co*(-3 + 2*co)))))*Ki))/48.;
        MG[3][8] = ((-1 + ci)*(-1 + co)*((2 + 3*ci - 12*Power(ci,3) + 8*Power(ci,4))*(2 + 3*co - 12*Power(co,3) + 8*Power(co,4))*K - (20 + 18*co + 28*Power(co,3)*(-3 + 2*co) + 3*ci*(6 + 7*co - 30*Power(co,3) + 20*Power(co,4)) - 6*Power(ci,3)*(14 + 15*co - 66*Power(co,3) + 44*Power(co,4)) + 4*Power(ci,4)*(14 + 15*co - 66*Power(co,3) + 44*Power(co,4)))*Ki))/48.;
        MG[3][9] = -((-1 + ci)*Power(-1 + co,3)*co*(1 + 2*co)*((2 + 3*ci - 12*Power(ci,3) + 8*Power(ci,4))*K - (10 + 9*ci - 42*Power(ci,3) + 28*Power(ci,4))*Ki))/48.;
        MG[4][4] = (5*Power(-1 + ci,6)*Power(ci,2)*Power(1 + 2*ci,2)*(K + Ki))/48.;
        MG[4][5] = (Power(-1 + ci,3)*ci*(1 + 2*ci)*(-1 + co)*Power(co,3)*(-3 + 2*co)*(K + Ki))/48.;
        MG[4][6] = -(Power(-1 + ci,3)*ci*(1 + 2*ci)*co*((1 + co*(1 + 4*(-1 + co)*co*(-3 + 2*co)))*K + Ki + co*(1 - 2*(-1 + co)*co*(-3 + 2*co))*Ki))/48.;
        MG[4][7] = (Power(-1 + ci,3)*ci*(1 + 2*ci)*((-2 + co*(1 + 3*co*(1 + 2*(-1 + co)*co*(-3 + 2*co))))*K - (2 + co*(5 + 3*co*(1 + 4*(-1 + co)*co*(-3 + 2*co))))*Ki))/48.;
        MG[4][8] = -(Power(-1 + ci,3)*ci*(1 + 2*ci)*(-1 + co)*((2 + 3*co - 12*Power(co,3) + 8*Power(co,4))*K - (10 + 9*co - 42*Power(co,3) + 28*Power(co,4))*Ki))/48.;
        MG[4][9] = (Power(-1 + ci,3)*ci*(1 + 2*ci)*Power(-1 + co,3)*co*(1 + 2*co)*(K - 5*Ki))/48.;
        MG[5][5] = (5*Power(co,6)*Power(3 - 5*co + 2*Power(co,2),2)*(K + Ki))/48.;
        MG[5][6] = -((-1 + co)*Power(co,4)*(-3 + 2*co)*(5*(1 + co*(1 + 4*(-1 + co)*co*(-3 + 2*co)))*K + (5 + co*(5 + 14*(-1 + co)*co*(-3 + 2*co)))*Ki))/48.;
        MG[5][7] = ((-1 + co)*Power(co,3)*(-3 + 2*co)*(5*(-2 + co*(1 + 3*co*(1 + 2*(-1 + co)*co*(-3 + 2*co))))*K + (-10 + co*(-1 + 3*co*(3 + 4*(-1 + co)*co*(-3 + 2*co))))*Ki))/48.;
        MG[5][8] = -(Power(-1 + co,2)*Power(co,3)*(-3 + 2*co)*(5*(2 + 3*co - 12*Power(co,3) + 8*Power(co,4))*K + (-2 + 3*co - 6*Power(co,3) + 4*Power(co,4))*Ki))/48.;
        MG[5][9] = (Power(-1 + co,4)*Power(co,4)*(-3 + 2*co)*(1 + 2*co)*(5*K - Ki))/48.;
        MG[6][6] = (Power(co,2)*(5*Power(1 + co*(1 + 4*(-1 + co)*co*(-3 + 2*co)),2)*K + (5 + co*(10 + co*(89 + 4*co*(-14 + co*(-3 + 2*co)*(-26 + 11*co*(8 + co*(-7 + 2*co)))))))*Ki))/48.;
        MG[6][7] = (-5*co*(-1 + 2*co)*(1 + co*(1 + 4*(-1 + co)*co*(-3 + 2*co)))*(2 + 3*co*(1 + co*(1 + 2*(-2 + co)*co)))*K + co*(10 + co*(11 + co*(76 - co*(191 + 2*Power(co,2)*(125 + 6*co*(-113 + 4*co*(37 + 4*(-5 + co)*co)))))))*Ki)/48.;
        MG[6][8] = ((-1 + co)*co*(5*(2 + 3*co - 12*Power(co,3) + 8*Power(co,4))*(1 + co*(1 + 4*(-1 + co)*co*(-3 + 2*co)))*K + (-2 + (-1 + co)*co*(-1 + 4*(-1 + co)*co*(4 + 5*co*(3 + co*(1 + 4*(-2 + co)*co)))))*Ki))/48.;
        MG[6][9] = -(Power(-1 + co,3)*Power(co,2)*(1 + 2*co)*(5*(1 + co*(1 + 4*(-1 + co)*co*(-3 + 2*co)))*K + (1 + 2*(-1 + co)*co)*(-1 + co*(-3 + 2*co))*Ki))/48.;
        MG[7][7] = (5*Power(-2 + co*(1 + 3*co*(1 + 2*(-1 + co)*co*(-3 + 2*co))),2)*K + (20 + co*(4 + co*(-31 + 3*co*(-46 + co*(111 + 8*Power(co,2)*(14 + 3*co*(-28 + co*(37 + 4*(-5 + co)*co))))))))*Ki)/48.;
        MG[7][8] = ((-1 + co)*(-5*(-1 + 2*co)*(2 + 3*co - 12*Power(co,3) + 8*Power(co,4))*(2 + 3*co*(1 + co*(1 + 2*(-2 + co)*co)))*K - (4 + co*(4 + co*(15 + co*(99 + 2*co*(-31 + co*(-79 + 6*co*(-27 + 4*co*(21 + 4*(-4 + co)*co))))))))*Ki))/48.;
        MG[7][9] = (Power(-1 + co,3)*co*(1 + 2*co)*(5*(-2 + co*(1 + 3*co*(1 + 2*(-1 + co)*co*(-3 + 2*co))))*K + (2 + co*(5 + 3*co*(1 + 4*(-1 + co)*co*(-3 + 2*co))))*Ki))/48.;
        MG[8][8] = (Power(-1 + co,2)*(5*Power(2 + 3*co - 12*Power(co,3) + 8*Power(co,4),2)*K + (20 + co*(36 + co*(21 + 4*co*(-3 + 2*co)*(14 + 15*co - 33*Power(co,3) + 22*Power(co,4)))))*Ki))/48.;
        MG[8][9] = -(Power(-1 + co,4)*co*(1 + 2*co)*(5*(2 + 3*co - 12*Power(co,3) + 8*Power(co,4))*K + (10 + 9*co - 42*Power(co,3) + 28*Power(co,4))*Ki))/48.;
        MG[9][9] = (5*Power(-1 + co,6)*Power(co,2)*Power(1 + 2*co,2)*(K + Ki))/48.;
#endif

		// Add each annulus element to the global stiffness matrix
		for(k=0;k<agelist[i].totalArcElements;k++)
		{
			// inner nodes
			if ((k-1)<0){
				nn[0]=agelist[i].node[agelist[i].totalArcElements-1].n0;
				ww[0]=agelist[i].node[agelist[i].totalArcElements-1].w0;
			}
			else{
				nn[0]=agelist[i].node[k-1].n0;
				ww[0]=agelist[i].node[k-1].w0;
			}

			nn[1]=agelist[i].node[k].n0;
			nn[2]=agelist[i].node[k].n1; 
			nn[3]=agelist[i].node[k+1].n1; 
			ww[1]=agelist[i].node[k].w0;
			ww[2]=agelist[i].node[k].w1; 
			ww[3]=agelist[i].node[k+1].w1; 

			if((k+2)>agelist[i].totalArcElements){
				nn[4]=agelist[i].node[1].n1;
				ww[4]=agelist[i].node[1].w1;
			}
			else{
				nn[4]=agelist[i].node[k+2].n1; 
				ww[4]=agelist[i].node[k+2].w1; 
			}

			// outer nodes
			if ((k-1)<0){
				nn[5]=agelist[i].node[agelist[i].totalArcElements-1].n2;
				ww[5]=agelist[i].node[agelist[i].totalArcElements-1].w2;
			}
			else{
				nn[5]=agelist[i].node[k-1].n2;
				ww[5]=agelist[i].node[k-1].w2;
			}

			nn[6]=agelist[i].node[k].n2;
			nn[7]=agelist[i].node[k].n3; 
			nn[8]=agelist[i].node[k+1].n3; 
			ww[6]=agelist[i].node[k].w2;
			ww[7]=agelist[i].node[k].w3; 
			ww[8]=agelist[i].node[k+1].w3; 

			if((k+2)>agelist[i].totalArcElements){
				nn[9]=agelist[i].node[1].n3;
				ww[9]=agelist[i].node[1].w3;
			}
			else{
				nn[9]=agelist[i].node[k+2].n3; 
				ww[9]=agelist[i].node[k+2].w3; 
			}

			// fix antiperiodic weights...
			if ((k==0) && (agelist[i].BdryFormat==1))
			{
				ww[0]=-ww[0];
				ww[5]=-ww[5];
			}
			if (((k+1)==agelist[i].totalArcElements) && (agelist[i].BdryFormat==1))
			{
				ww[4]=-ww[4];
				ww[9]=-ww[9];
			}
		
			// scale by weight to get periodic/antiperiodic right and tack into mesh
			for(int ii=0;ii<10;ii++)
				for(int jj=ii;jj<10;jj++)
					L.AddTo(MG[ii][jj]*ww[ii]*ww[jj],nn[ii],nn[jj]); 
		}
		
	}

	// ---- Transient (backward-Euler) eddy currents in solid conductors -----
	//
	// In a solid conductor J = sigma*(v - dA/dt), with v the axial voltage
	// gradient (uniform over the cross-section in 2D). Backward Euler makes
	// dA/dt = (A - Aprev)/DeltaT, contributing (sigma/DeltaT)*M to the matrix
	// and (sigma/DeltaT)*M*Aprev to the RHS, where M is the consistent
	// linear-triangle mass matrix. This is prob2big.cpp:457-476 with
	// j*omega -> 1/DeltaT and real arithmetic, plus the Aprev forcing that
	// backward Euler adds and the harmonic form has no analogue for.
	//
	// Both terms are V-INDEPENDENT within a time step, so they belong in the
	// baseline sweep -- assembled once here for every element, nonlinear ones
	// included, leaving only nu(B) to the per-pass reassembly. Assembling
	// them at sweep level unguarded would double-add them on the Iter==1 pass
	// (sweeps 1 and 2 both run), the defect fixed for the flux-constraint
	// block in 3ab9ced.
	//
	// Sign: prob1big inserts -Me and subtracts be (see the element loop
	// below), so its assembled system is the physical one and these terms go
	// in with a PLUS sign, written straight into L.
	if (bTransient)
	for(i=0;i<NumEls;i++)
	{
		El=&meshele[i];
		Cduct=EddyCduct(El->blk,El->lbl);
		if (Cduct==0) continue;

		for(k=0;k<3;k++) n[k]=El->p[k];
		p[0]=meshnode[n[1]].y-meshnode[n[2]].y;
		p[1]=meshnode[n[2]].y-meshnode[n[0]].y;
		p[2]=meshnode[n[0]].y-meshnode[n[1]].y;
		q[0]=meshnode[n[2]].x-meshnode[n[1]].x;
		q[1]=meshnode[n[0]].x-meshnode[n[2]].x;
		q[2]=meshnode[n[1]].x-meshnode[n[0]].x;
		a=(p[0]*q[1]-p[1]*q[0])/2.;

		// Km*(1+delta_jk) is (sigma*c/DeltaT)*M, M being the consistent mass
		// matrix (a/6 on the diagonal, a/12 off it) -- exactly the shape
		// prob2big builds by adding K to Me[j][k] and Me[k][j] for k>=j.
		double Km=a*Cduct*c/(12.*DeltaT);
		double Vp[3],Vps=0;
		for(j=0;j<3;j++){
			Vp[j]=(Aprev!=NULL)?(Aprev[n[j]]/c):0.0;
			Vps+=Vp[j];
		}

		for(j=0;j<3;j++){
			for(k=j;k<3;k++) L.AddTo((j==k)?(2.*Km):Km, n[j], n[k]);
			L.b[n[j]] += Km*(Vps+Vp[j]);
		}

		// Case 2: the region's voltage gradient is a matrix unknown. The
		// coupling entries put sigma*v into the node equations as a source;
		// symmetric storage mirrors them into the voltage row, which IS the
		// total-current equation, integral of sigma*(v - dA/dt) = I. The
		// coefficients are a*Cd*c/DeltaT and its third, i.e. 12*Km and 4*Km.
		int ic=labellist[El->lbl].InCircuit;
		if ((ic>=0) && (circproplist[ic].Case==2)){
			int col=VoltRowBase+ic;
			for(j=0;j<3;j++) L.AddTo(4.*Km, n[j], col);
			L.AddTo(12.*Km, col, col);
			// Aprev's share of the current equation. Not optional: without it
			// the constraint is wrong by (sigma*c/DeltaT)*integral Vprev.
			L.b[col] += 4.*Km*Vps;
			// applied source current density over this element
			L.b[col] += blockproplist[El->blk].Jr*a;
		}
	}

	// Close the total-current equation of each Case-2 region, once per
	// circuit. Two ways, depending on where its current comes from.
	if (bTransient)
	for(i=0;i<NumCircProps;i++)
		if (circproplist[i].Case==2)
		{
			if (circproplist[i].FluxCirc>=0)
			{
				// Solid region in a flux-constrained circuit. The current is
				// not prescribed -- it is the flux circuit's own unknown --
				// so it moves to the left-hand side instead of the RHS.
				//
				// Symmetric storage then does the other half for free, and
				// the coefficient is forced rather than chosen. The mirrored
				// entry puts 0.01*X_v into the flux row, and since the
				// voltage unknown is scaled X_v = dV*DeltaT/c, that term is
				//
				//     0.01*DeltaT*dV/c == DeltaT*dV*Depth_metres/(Depth*units*c)
				//
				// because Depth*units*0.01 is precisely the depth in metres.
				// The flux row is scaled by 1/(Depth*units*c), so this is
				// exactly the DeltaT*Depth*v term the terminal relation
				//
				//     lambda_wound + DeltaT*Depth*sum(v_solid) + M*i = Lambda
				//
				// calls for: one equation per circuit, each region adding its
				// own voltage contribution. Getting the transpose right is
				// what confirms the scaling.
				L.AddTo(0.01, VoltRowBase+i, NumNodes+circproplist[i].FluxCirc);
			}
			else
			{
				// Ordinary current-driven circuit: the current is given.
				// Negated relative to prob2big:700-705, which assembles with
				// the opposite global sign (see the note above).
				L.b[VoltRowBase+i] -= 0.01*circproplist[i].Amps_re;
			}
		}

	}    // end sweepSel<=1 (V-independent contributions)

	for(i=0;i<NumEls;i++){

		if (sweepSel!=0){
			int isNL = (blockproplist[meshele[i].blk].BHpoints != 0);
			if (sweepSel==1 && isNL)  continue;
			if (sweepSel==2 && !isNL) continue;
		}

		// update ``building matrix'' progress bar...
		j=(i*20)/NumEls+1;
		if(j>pctr){
			j=pctr*5; if (j>100) j=100;
			TheView->m_prog1.SetPos(j);
			pctr++;
		}

		// zero out Me, be;
		for(j=0;j<3;j++){
			for(k=0;k<3;k++){
				Me[j][k]=0.;
				Mx[j][k]=0.;
				My[j][k]=0.;
				Mn[j][k]=0.;
				Mxy[j][k] = 0.;
			}
			be[j]=0.;
			beCirc[j]=0.;
		}

		// Determine shape parameters.
		// l == element side lengths;
		// p corresponds to the `b' parameter in Allaire
		// q corresponds to the `c' parameter in Allaire
		El=&meshele[i];		
	
		for(k=0;k<3;k++) n[k]=El->p[k];
		p[0]=meshnode[n[1]].y - meshnode[n[2]].y;
		p[1]=meshnode[n[2]].y - meshnode[n[0]].y;
		p[2]=meshnode[n[0]].y - meshnode[n[1]].y;	
		q[0]=meshnode[n[2]].x - meshnode[n[1]].x;
		q[1]=meshnode[n[0]].x - meshnode[n[2]].x;
		q[2]=meshnode[n[1]].x - meshnode[n[0]].x;
		for(j=0,k=1;j<3;k++,j++){
			if (k==3) k=0;
			l[j]=sqrt( pow(meshnode[n[k]].x-meshnode[n[j]].x,2.) +
					   pow(meshnode[n[k]].y-meshnode[n[j]].y,2.) );
		}
		a=(p[0]*q[1]-p[1]*q[0])/2.;
		r=(meshnode[n[0]].x+meshnode[n[1]].x+meshnode[n[2]].x)/3.;

		// x-contribution; only need to do main diagonal and above;
		K = (-1. / (4.*a));
		for (j = 0; j < 3; j++)
			for (k = j; k < 3; k++)
			{
				Mx[j][k] += K*p[j] * p[k];
				if (j != k) Mx[k][j] += K*p[j] * p[k];
			}

		// y-contribution; only need to do main diagonal and above;
		K = (-1. / (4.*a));
		for (j = 0; j < 3; j++)
			for (k = j; k < 3; k++)
			{
				My[j][k] += K*q[j] * q[k];
				if (j != k) My[k][j] += K*q[j] * q[k];
			}
		
		// xy-contribution; 
		K = (-1. / (4.*a));
		for (j = 0; j < 3; j++)
			for (k = j; k < 3; k++)
			{
				Mxy[j][k] += K*(p[j] * q[k] + p[k] * q[j]);
				if (j != k) Mxy[k][j] += K*(p[j] * q[k] + p[k] * q[j]);
			}

		// contributions to Me, be from derivative boundary conditions;
		for(j=0;j<3;j++){
			if (El->e[j] >= 0)
			if (lineproplist[El->e[j]].BdryFormat==2)
			{
				// conversion factor is 10^(-4) (I think...)
				K=-0.0001*c*lineproplist[ El->e[j] ].c0.re*l[j]/6.;
				k=j+1; if(k==3) k=0;
				Me[j][j]+=K*2.;
				Me[k][k]+=K*2.;
				Me[j][k]+=K;
				Me[k][j]+=K;

				K=(lineproplist[ El->e[j] ].c1.re*l[j]/2.)*0.0001;
				be[j]+=K;
				be[k]+=K;
			}
		}
		
		// contribution to be from current density in the block
		for(j=0,El->Jprev=0;j<3;j++){
			// t must be cleared FIRST: the branches below only assign it for
			// Case 0 and Case 1, so any other case would silently inherit the
			// previous element's value -- garbage source current density, and
			// garbage in El->Jprev, which is written to the .ans and read back
			// by LoadPrev(). prob2big clears its equivalent (Jv) the same way.
			t=0;
			if(labellist[El->lbl].InCircuit>=0)
			{
				k=labellist[El->lbl].InCircuit;
				if(circproplist[k].Case==1) t=circproplist[k].J.Re();
				if(circproplist[k].Case==0)
					t=-circproplist[k].dV.Re()*blockproplist[El->blk].Cduct;
			}
			K=-(blockproplist[El->blk].Jr+t)*a/3.;
			
			// put on RHS if we are solving
			if(labellist[El->lbl].InCircuit>=0)
			{
				if (circproplist[labellist[El->lbl].InCircuit].CircType==2)
					beCirc[j] -= K; // if this is a flux circuit, contribution ultimately gets added to LHS
				else
					be[j] += K; // if this is a regular circuit, contribution goes on the RHS
			}
			else be[j]+=K; // if we are explicitly specifying current density, this goes on the RHS

			// record avg current density in the block for use in incremental solutions
			// for now, assume that we aren't doing flux circuits and incremental solutions 
			// at the same time.  If we do, might have to do more here....
			if (bIncremental==FALSE) El->Jprev+=(blockproplist[El->blk].Jr+t)/3.;
		}

		// contribution to be from magnetization in the block;
		t=labellist[El->lbl].MagDir;
		if (labellist[El->lbl].MagDirFctn!=NULL) // functional magnetization direction
		{
			CComplex X;
			for (j=0,X=0;j<3;j++) X+=(meshnode[n[j]].x + I*meshnode[n[j]].y);
			X=X/units[LengthUnits]/3.;
			t = mag_dir_eval(labellist[El->lbl].MagDirFctn, X);
		}
		for(j=0;j<3;j++){
			k=j+1; if(k==3) k=0;
			// need to scale so that everything is in proper units...
			// conversion is 0.0001
			K=0.0001*blockproplist[El->blk].H_c*(
			  cos(t*PI/180.)*(meshnode[n[k]].x-meshnode[n[j]].x) +
			  sin(t*PI/180.)*(meshnode[n[k]].y-meshnode[n[j]].y) )/2.;
			be[j]+=K;
			be[k]+=K;
		}	

//////// Nonlinear Part

		// update permeability for the element;
		if (Iter==0){ 
			k=meshele[i].blk;
		
			if (blockproplist[k].LamType==0){
				t=blockproplist[k].LamFill;
				meshele[i].mu1=blockproplist[k].mu_x*t + (1.-t);
				meshele[i].mu2=blockproplist[k].mu_y*t + (1.-t);
			}
			if (blockproplist[k].LamType==1){
				t=blockproplist[k].LamFill;
				mu=blockproplist[k].mu_x;
				meshele[i].mu1=mu*t + (1.-t);
				meshele[i].mu2=mu/(t + mu*(1.-t));
			}
			if (blockproplist[k].LamType==2){
				t=blockproplist[k].LamFill;
				mu=blockproplist[k].mu_y;
				meshele[i].mu2=mu*t + (1.-t);
				meshele[i].mu1=mu/(t + mu*(1.-t));
			}
			if (blockproplist[k].LamType>2)
			{
				meshele[i].mu1=1;
				meshele[i].mu2=1;
			}


			if (blockproplist[k].BHpoints != 0)
			{
				if (bIncremental == FALSE)
				{
					// There's no previous solution.  This is a standard nonlinear problem
					LinearFlag = FALSE;
				}
				else {
					double B1p, B2p;

					// too lazy to consistently code incremental/frozen formulation for on-edge lams.
					// detect this condition, throw an error, and exit.
					if (blockproplist[k].LamType > 0)
					{
						MsgBox("On-edge Lam Types not yet supported in\nincremental/frozen permeability problems");
						exit(0);
					}

					//	Get B from previous solution
					GetPrev2DB(i, B1p, B2p);
					B = sqrt(B1p*B1p + B2p*B2p);



					// look up incremental permeability and assign it to the element;
					blockproplist[k].IncrementalPermeability(B, muinc, murel);

					if (B == 0)
					{
						meshele[i].mu1 = muinc;
						meshele[i].mu2 = muinc;
						meshele[i].v12 = 0;
					}
					else {
						if (bIncremental == 1)
						{
							// Need to actually compute B1 and B2 to build incremental permeability tensor
							meshele[i].mu1 = B*B*muinc*murel / (B1p*B1p*murel + B2p*B2p*muinc);
							meshele[i].mu2 = B*B*muinc*murel / (B1p*B1p*muinc + B2p*B2p*murel);
							meshele[i].v12 = -B1p*B2p*(murel - muinc) / (B*B*murel*muinc);
						}
						else {
							// Define "frozen permeability"
							meshele[i].mu1 = murel;
							meshele[i].mu2 = murel;
							meshele[i].v12 = 0;
						}
					}
				}
			}

		}

		if ((Iter>0) || (bRestart==TRUE))
		{ 
			k=meshele[i].blk;
		
			if ((blockproplist[k].LamType==0) &&
			    (meshele[i].mu1==meshele[i].mu2)
				&&(blockproplist[k].BHpoints>0))
			{
				for(j=0,B1=0.,B2=0.;j<3;j++){
					B1+=L.V[n[j]]*q[j];
					B2+=L.V[n[j]]*p[j];
				}
				B=c*sqrt(B1*B1+B2*B2)/(0.02*a); 
				// correction for lengths in cm of 1/0.02
	
				// find out new mu from saturation curve;
				blockproplist[k].GetBHProps(B,mu,dv);
				mu=1./(muo*mu);
				meshele[i].mu1=mu;
				meshele[i].mu2=mu;
				for(j=0;j<3;j++){
					for(w=0,v[j]=0;w<3;w++)
						v[j]+=(Mx[j][w]+My[j][w])*L.V[n[w]];
				}
				K=-200.*c*c*c*dv/a;
				for(j=0;j<3;j++)
					for(w=0;w<3;w++)
						Mn[j][w]=K*v[j]*v[w];
			}

			if ((blockproplist[k].LamType==1) && (blockproplist[k].BHpoints>0)){
				t=blockproplist[k].LamFill;
				for(j=0,B1=0.,B2=0.;j<3;j++){
					B1+=L.V[n[j]]*q[j];
					B2+=L.V[n[j]]*p[j]/t;
				}
				B=c*sqrt(B1*B1+B2*B2)/(0.02*a);
				blockproplist[k].GetBHProps(B,mu,dv);
				mu=1./(muo*mu);
				meshele[i].mu1=mu*t;
				meshele[i].mu2=mu/(t+mu*(1.-t));
				for(j=0;j<3;j++){
					for(w=0,v[j]=0,u[j]=0;w<3;w++)
					{
						v[j]+=(My[j][w]/t+Mx[j][w])*L.V[n[w]];
						u[j]+=(My[j][w]/t + t*Mx[j][w])*L.V[n[w]];
					}
				}
				K=-100.*c*c*c*dv/(a);
				for(j=0;j<3;j++)
					for(w=0;w<3;w++)
						Mn[j][w]=K*(v[j]*u[w]+v[w]*u[j]);
			}
			if ((blockproplist[k].LamType==2) && (blockproplist[k].BHpoints>0)){
				t=blockproplist[k].LamFill;
				for(j=0,B1=0.,B2=0.;j<3;j++){
					B1+=(L.V[n[j]]*q[j])/t;
					B2+=L.V[n[j]]*p[j];
				}
				B=c*sqrt(B1*B1+B2*B2)/(0.02*a);
				blockproplist[k].GetBHProps(B,mu,dv);
				mu=1./(muo*mu);
				meshele[i].mu2=mu*t;
				meshele[i].mu1=mu/(t+mu*(1.-t));
				for(j=0;j<3;j++){
					for(w=0,v[j]=0,u[j]=0;w<3;w++)
					{
						v[j]+=(Mx[j][w]/t + My[j][w])*L.V[n[w]];
						u[j]+=(Mx[j][w]/t + t*My[j][w])*L.V[n[w]];
					}
				}
				K=-100.*c*c*c*dv/(a);
				for(j=0;j<3;j++)
					for(w=0;w<3;w++)
						Mn[j][w]=K*(v[j]*u[w]+v[w]*u[j]);
			} 
		}

		// combine block matrices into global matrices;
		for(j=0;j<3;j++) 
			for(k=0;k<3;k++){
				// finish building element matrix by summing x-, y- and cross components with N-R iteration contribution
				Me[j][k]+= (Mx[j][k]/Re(El->mu2) + My[j][k]/Re(El->mu1) + Mxy[j][k] * Re(El->v12) + Mn[j][k]);
				// finish building RHS forcing vector by adding in the NL contribution
				be[j]+=Mn[j][k]*L.V[n[k]];
			}

		for (j=0;j<3;j++){
			for (k=j;k<3;k++)
				L.AddTo(-Me[j][k],n[j],n[k]); // add element matrix into global matrix
			L.b[n[j]]-=be[j]; // add element forcing vector into global RHS

			// !!!!
			if (labellist[El->lbl].InCircuit!=-1)
			{
				if (circproplist[labellist[El->lbl].InCircuit].CircType==2)
				{
					// add terms to global matrix that create current from flux constraint / add up all the flux
					k = circproplist[labellist[El->lbl].InCircuit].FluxCirc + NumNodes;
					L.AddTo(-beCirc[j],k,n[j]);
				}
			}
			
		}
	}
	
	// Add flux contstraint matrix into global matrix. V-independent, so
	// baseline-sweep only: unguarded, the Iter==1 pass (which runs sweeps 1
	// AND 2) added it once into the baseline and once on top, doubling the
	// external coupling seen by every nonlinear pass -- caught as
	// lambda + 2*M*i = Lambda against OF's lambda + M*i = Lambda.
	if (sweepSel<=1)
	for (j=0;j<NumFluxConstraints;j++)
	{
		for (k=j;k<NumFluxConstraints;k++)
		{
			K = CircuitMatrix[j][k]/(Depth*units[LengthUnits]*c);
			L.AddTo(-K,j+NumNodes,k+NumNodes);
		}
	}

	// Add current-constraint matrix into global matrix. Constraint m owns
	// row NumNodes+NumFluxConstraints+m, which enforces
	//     sum_j C[m][j] * i_j  =  I_m
	// over the flux-circuit currents. The unknown paired with that row is
	// formally a Lagrange multiplier, but physically it is the VOLTAGE of
	// the external-network node where the KCL constraint holds (a Y
	// winding's star point, a cage's end-ring node): the symmetric storage
	// mirrors each C entry into flux-circuit row j, injecting that node
	// voltage into the circuit's voltage balance -- which is exactly how a
	// node potential enters nodal analysis. V-independent, so baseline
	// sweep only, like the flux block above.
	// The rows carry the same 1/(Depth*units*c) normalization as the flux
	// rows: entered raw, the C entries (~1) sit five orders below the
	// field entries and the multiplier direction is ill-conditioned enough
	// to stall QMR on degenerate cases. Row scaling leaves the enforced
	// constraint identical; only the internal multiplier rescales.
	if (sweepSel<=1 && ConstraintMatrix!=NULL)
	for (j=0;j<NumCurrentConstraints;j++)
	{
		for (k=0;k<NumFluxConstraints;k++)
		{
			if (ConstraintMatrix[j][k]!=0)
			{
				K = ConstraintMatrix[j][k]/(Depth*units[LengthUnits]*c);
				L.AddTo(K, NumNodes+NumFluxConstraints+j, NumNodes+k);
			}
		}
		// Tiny NEGATIVE diagonal on the constraint row (quasi-definite
		// regularization). A zero-diagonal saddle row is the structure an
		// incomplete factorization handles worst -- QMR stalled just shy
		// of tolerance on degenerate cases without this -- while a
		// (+,...,+,-) quasi-definite matrix factors stably. With the rows
		// sharing the flux normalization, the multiplier v carries flux-
		// linkage units, so the constraint perturbation is ~1e-6*v in
		// current units: nano-amp territory, orders below the solve
		// tolerance.
		L.AddTo(-1e-6/(Depth*units[LengthUnits]*c),
			NumNodes+NumFluxConstraints+j, NumNodes+NumFluxConstraints+j);
	}
	}  // end sweepSel (split assembly)

	// Pin the diagonal of every transient voltage row that no solid conductor
	// claimed. NumVoltageUnknowns allocates one row per circuit up front --
	// which circuits actually need one is not known until the element sweep
	// has run -- so the leftovers would otherwise be all-zero rows. They are
	// not fatal (BuildDI substitutes 1 for a zero diagonal, and a zero RHS
	// leaves the unknown at 0), but they do perturb the incomplete
	// factorization and hence the iteration path, which showed up as small
	// movement in results that must not move at all. An explicit unit
	// diagonal makes them exactly inert.
	//
	// Note this cannot use L.Get(0,0) the way prob2big:781 does: that idiom
	// relies on running after the element loop, and the rows are pinned here
	// only once, outside the sweep, so a fixed value is both simpler and safe.
	for (j=0;j<NumVoltageUnknowns;j++)
		if (L.Get(VoltRowBase+j,VoltRowBase+j)==0)
			L.Put(1.0,VoltRowBase+j,VoltRowBase+j);

	// Add in RHS contribution from each flux constraint

	for (i=0;i<NumFluxConstraints;i++)
	{
		// compute scaled flux linkage value to add into the RHS vector
		K = circproplist[fluxproplist[i]].FluxLinkage / (Depth*units[LengthUnits]*c);

		// Add value into RHS vector at the right spot and with the right sign...
		L.b[i + NumNodes] -= K;
	}

	// Add in RHS contribution from each current constraint. An absent
	// [ConstraintCurrent] section means the homogeneous constraint
	// (sum C*i = 0, e.g. a cage's bar currents), so a NULL currentlist
	// needs no RHS at all.
	if (currentlist!=NULL)
		for (i=0;i<NumCurrentConstraints;i++)
			L.b[NumNodes + NumFluxConstraints + i] +=
				currentlist[i]/(Depth*units[LengthUnits]*c);

	// add in contribution from point currents;
	for(i=0;i<NumNodes;i++)
		if(meshnode[i].bc>=0)
			L.b[i]+=(0.01*nodeproplist[meshnode[i].bc].Jr);
		
	// apply fixed boundary conditions at points;
	for(i=0;i<NumNodes;i++)
		if(meshnode[i].bc >=0)
			if((nodeproplist[meshnode[i].bc].Jr==0) &&
			   (nodeproplist[meshnode[i].bc].Ji==0))
				L.SetValue(i,nodeproplist[meshnode[i].bc].Ar / c);

	// apply fixed boundary conditions along segments;
	for(i=0;i<NumEls;i++)
		for(j=0;j<3;j++){
			k=j+1; if(k==3) k=0;
			if(meshele[i].e[j]>=0)
			if(lineproplist[ meshele[i].e[j] ].BdryFormat==0)
			{
				if(Coords==0){
					// first point on the side;
					x=meshnode[meshele[i].p[j]].x;
					y=meshnode[meshele[i].p[j]].y;
					x/=units[LengthUnits]; y/=units[LengthUnits];
					s=meshele[i].e[j];
					a=lineproplist[s].A0 + x*lineproplist[s].A1 +
					  y*lineproplist[s].A2;
					// just take ``real'' component.
					a*=cos(lineproplist[s].phi*DEG); 
					L.SetValue(meshele[i].p[j],a/c);
					
					// second point on the side;
					x=meshnode[meshele[i].p[k]].x;
					y=meshnode[meshele[i].p[k]].y;
					x/=units[LengthUnits]; y/=units[LengthUnits];
					s=meshele[i].e[j];
					a=lineproplist[s].A0 + x*lineproplist[s].A1 +
					  y*lineproplist[s].A2;
					// just take``real'' component.
					a*=cos(lineproplist[s].phi*DEG); 
					L.SetValue(meshele[i].p[k],a/c);
				}
				else{
					// first point on the side;
					x=meshnode[meshele[i].p[j]].x;
					y=meshnode[meshele[i].p[j]].y;
					r=sqrt(x*x+y*y);
					if((x==0)&&(y==0)) t=0;
					else t=atan2(y,x)/DEG;
					r/=units[LengthUnits];
					s=meshele[i].e[j];
					a=lineproplist[s].A0 + r*lineproplist[s].A1 +
					  t*lineproplist[s].A2;
					a*=cos(lineproplist[s].phi*DEG); // just take ``real'' component.
					L.SetValue(meshele[i].p[j],a/c);
					
					// second point on the side;
					x=meshnode[meshele[i].p[k]].x;
					y=meshnode[meshele[i].p[k]].y;
					r=sqrt(x*x+y*y);
					if((x==0) && (y==0)) t=0;
					else t=atan2(y,x)/DEG;
					r/=units[LengthUnits];
					s=meshele[i].e[j];
					a=lineproplist[s].A0 + r*lineproplist[s].A1 +
					  t*lineproplist[s].A2;
					a*=cos(lineproplist[s].phi*DEG); // just take ``real'' component.
					L.SetValue(meshele[i].p[k],a/c);
				}

			}
		}
		
	// Apply any periodicity/antiperiodicity boundary conditions that we have
	for(k=0;k<NumPBCs;k++)
	{
		if (pbclist[k].t==0) L.Periodicity(pbclist[k].x,pbclist[k].y);
		if (pbclist[k].t==1) L.AntiPeriodicity(pbclist[k].x,pbclist[k].y);
	}

	// Dump problem info to Matlab, if that was commanded
	if ((Iter==0) && (bDump==TRUE)) MatlabDumpStatic2D(L);

	// solve the problem;
	for(j=0;j<NumNodes;j++) V_old[j]=L.V[j];
	// Snapshot the voltage unknowns too, so the relaxation below moves them
	// with the field. prob2big:817 relaxes its circuit unknowns for the same
	// reason. Deliberately not extended to the flux/current-constraint
	// multipliers, which would change existing behaviour.
	if (bTransient) for(j=VoltRowBase;j<NumNodesPlus;j++) V_old[j]=L.V[j];

	// Inexact inner solves for the nonlinear iteration, as the harmonic
	// solver has always done (prob2big): solving the linearized system 1000x
	// tighter than the current nonlinear residual buys nothing. res is 0 on
	// the first pass and small on the last, so those solve at full Precision
	// -- in particular a LINEAR problem (one pass) is unaffected.
	L.Precision=std::min(1.e-4,1.e-4*res);
	if (L.Precision<Precision) L.Precision=Precision;

	if (NumFluxConstraints > 0 || NumCurrentConstraints > 0) {
		if (L.QMRSolve(Iter + bRestart)==FALSE) return FALSE;
	} else {
		if (L.PCGSolve(Iter + bRestart)==FALSE) return FALSE;
	}
	// if (L.PCRSolve(Iter + bRestart)==FALSE) return FALSE;
	
	if (LinearFlag==FALSE){

        for(j=0,x=0,y=0;j<NumNodes;j++){
            x+=(L.V[j]-V_old[j])*(L.V[j]-V_old[j]);
            y+=(L.V[j]*L.V[j]);
        }

        if (y==0) LinearFlag=TRUE;
        else{
            lastres=res;
            res=sqrt(x/y);
        }


        // relaxation if we need it
        if(Iter>5)
        {
            if ((res>lastres) && (Relax>0.125)) Relax/=2.;
            else Relax+= 0.1 * (1. - Relax);
       
            for(j=0;j<NumNodes;j++) L.V[j]=Relax*L.V[j]+(1.0-Relax)*V_old[j];
            if (bTransient)
                for(j=VoltRowBase;j<NumNodesPlus;j++)
                    L.V[j]=Relax*L.V[j]+(1.0-Relax)*V_old[j];
        }

       
        // report some results
		char outstr[256];
		snprintf(outstr, sizeof(outstr),"Newton Iteration(%i) Relax=%.4g",Iter,Relax);
        TheView->SetDlgItemText(IDC_FRAME2,outstr);
        j=(int)  (100.*log10(res)/(log10(Precision)+2.));
        if (j>100) j=100;
        TheView->m_prog2.SetPos(j);
    }

	// nonlinear iteration has to have a looser tolerance
	// than the linear solver--otherwise, things can't ever
	// converge.  Arbitrarily choose 100*tolerance.
	// Accept convergence only off a pass whose inner solve met the file's
	// full Precision. The adaptive inner tolerance above is set from the
	// PREVIOUS pass's res, so a quadratic Newton jump (res 3e-4 -> 5e-7 in
	// one step) could otherwise terminate on a solve done at a loosened
	// tolerance. Rejecting that pass costs one more warm-started solve --
	// which, with res now small, runs at the full-Precision floor and
	// converges in a handful of iterations -- and restores the guarantee
	// that the SHIPPED solution satisfies both the inner and outer tests.
	if((res<100.*Precision) && (Iter>0) && (L.Precision<=Precision)) LinearFlag=TRUE;

	Iter++;
  	
} while(LinearFlag==FALSE);

	for(i=0;i<NumNodes;i++) L.b[i]=L.V[i]*c; // convert answer to Amps
	for (i=NumNodes;i<NumNodesPlus;i++)	L.b[i]=L.V[i]; // just copy over flux circuit currents and constraint voltages
	// The voltage unknown is scaled like the nodal potential, which is what
	// makes the coupled block symmetric; convert it to the dV the
	// postprocessor expects (prob2big:851-852 with j*omega -> 1/DeltaT).
	if (bTransient)
		for (i=0;i<NumCircProps;i++)
			if (circproplist[i].Case==2)
				L.b[VoltRowBase+i]=c*L.V[VoltRowBase+i]/DeltaT;

	free(V_old);

	if(NumCircProps>0){
		free(CircInt1);
		free(CircInt2);
		free(CircInt3);
		free(CircInt2t);
	}
	return TRUE;
}

//=========================================================================
//=========================================================================

int CFemmeDocCore::WriteStatic2D(CBigLinProb &L)
{
	// write solution to disk;
		
	char s[1024],q[1024];
	FILE *fp,*fz;
	int i,k;
	double cf;
	double unitconv[]={2.54,0.1,1.,100.,0.00254,1.e-04};
	
	// first, echo input .fem file to the .ans file;
	snprintf(s, sizeof(s),"%s.fem",PathName);
	if((fz=femm_fopen(s,"rt"))==NULL){
		Sleep(500);
		if((fz=femm_fopen(s,"rt"))==NULL){
			MsgBox("Couldn't open %s.fem\n",PathName);
			return FALSE;
		}
	}

	snprintf(s, sizeof(s),"%s.ans",PathName);
	if((fp=femm_fopen(s,"wt"))==NULL){
		Sleep(500);
		if((fp=femm_fopen(s,"wt"))==NULL){
			MsgBox("Couldn't write to %s.ans\n",PathName);
			return FALSE;
		}
	}

//  original implementation just echos the file as-is
//	while(fgets(c,1024,fz)!=NULL) fputs(c,fp);

	// We want to update the circuits so that the current in the circuit is
	// what was needed to generate the prescribed flux.  This makes all the
	// post-processing work like it's supposed to, and we can crib it later
	// as an initial condition
	//@@@
	int thisCircuit = -1;
	while (fgets(s,1024,fz)!=NULL)
	{
		sscanf(s,"%s",q);
		
		if( _strnicmp(q,"<begincircuit>",14)==0){	
			thisCircuit = -1;
			q[0]=0;
		}

		if( _strnicmp(q,"<circuitname>",13)==0){
			CString thisName;
			ParseString(s,&thisName);
			for(int j=0;j<NumCircProps;j++)
			{
				if (strcmp(circproplist[j].CircName,thisName)==0)
				{
					thisCircuit=circproplist[j].FluxCirc;
					break;
				}
			}
			q[0]=0;
		}

		if( _strnicmp(q,"<totalamps_re>",14)==0){
		   if (thisCircuit>=0)
			snprintf(s, sizeof(s),"    <TotalAmps_re> = %.15g\n",L.b[NumNodes + thisCircuit]);
		   q[0]=0;
		}

		if( _strnicmp(q,"<totalamps_im>",14)==0){
		   if (thisCircuit>=0)
			snprintf(s, sizeof(s),"    <TotalAmps_im> = 0\n");
		   q[0]=0;
		}

		fputs(s,fp);
	}

	fclose(fz);

	// The postprocessor cannot reconstruct J = sigma*(v - (A-Aprev)/DeltaT)
	// without the step size, and the .ans header is the only place it can
	// travel: it is echoed from the .fem, which has no such key, and the .tmp
	// sidecar is a solver input rather than a result. Unknown header keys are
	// ignored by readers, so this is invisible to anything that predates it.
	if (DeltaT>0) fprintf(fp,"[DeltaT] = %.17g\n",DeltaT);

	// then print out node, line, and element information. The whole solution
	// section goes through a block-flushed string buffer with to_chars
	// formatting (see fastio.h) -- per-fprintf formatting of the node and
	// element loops is most of the .ans write time at solver scale. Values
	// round-trip bit-exactly but the digit strings are shorter than %.17g's.
	std::string out;
	out.reserve(1u<<20);
	out+="[Solution]\n";
	cf=unitconv[LengthUnits];
	fio_append_int(out,NumNodes); out+='\n';
	for (i = 0;i < NumNodes;i++)
	{
		fio_append_g(out, meshnode[i].x / cf); out+='\t';
		fio_append_g(out, meshnode[i].y / cf); out+='\t';
		fio_append_g(out, L.b[i]); out+='\t';
		fio_append_int(out, meshnode[i].bc); out+='\t';
		// include A from previous solution if this is an incremental permeability problem
		if (Aprev != NULL) fio_append_g(out, Aprev[i]);
		out+='\n';
		fio_flush(fp,out);
	}

	fio_append_int(out,NumEls); out+='\n';
	for(i=0;i<NumEls;i++)
	{
		fio_append_int(out,meshele[i].p[0]); out+='\t';
		fio_append_int(out,meshele[i].p[1]); out+='\t';
		fio_append_int(out,meshele[i].p[2]); out+='\t';
		fio_append_int(out,meshele[i].lbl);  out+='\t';
		fio_append_int(out,meshele[i].e[0]); out+='\t';
		fio_append_int(out,meshele[i].e[1]); out+='\t';
		fio_append_int(out,meshele[i].e[2]); out+='\t';
		fio_append_g(out,meshele[i].Jprev);  out+='\n';
		fio_flush(fp,out);
	}
	fwrite(out.data(),1,out.size(),fp);
	out.clear();
/*
	// print out circuit info
	fprintf(fp,"%i\n",NumCircPropsOrig);
	for(i=0;i<NumCircPropsOrig;i++){
		if (circproplist[i].Case==0)
			fprintf(fp,"0	%.17g\n",circproplist[i].dV.Re());
		if (circproplist[i].Case==1)
			fprintf(fp,"1	%.17g\n",circproplist[i].J.Re());
	}
*/

	// print out circuit info on a blocklabel by blocklabel basis;
	fprintf(fp,"%i\n",NumBlockLabels);
	for(k=0;k<NumBlockLabels;k++){
		i=labellist[k].InCircuit;
		if(i<0) // if block not associated with any particular circuit
		{
			// print out some "dummy" propeties that say that
			// there is a fixed additional current density,
			// but that that additional current density is zero.
			fprintf(fp,"1	0\n");
		}
		else{
			if (circproplist[i].Case==0)
				fprintf(fp,"0	%.17g\n",circproplist[i].dV.Re());
			if (circproplist[i].Case==1)
			{
				// if it's not a flux circuit, just report the label's current density
				if (circproplist[i].FluxCirc<0) fprintf(fp,"1	%.17g\n",circproplist[i].J.Re());
				// if it's in a flux circuit, need to multiply by the circuit current to get the right current density
				else fprintf(fp,"1	%.17g\n",circproplist[i].J.Re()*L.b[circproplist[i].FluxCirc + NumNodes]);
			}
			// A transient solid conductor's voltage gradient was solved for
			// rather than given, but the postprocessor only needs to know its
			// value -- so report it as an ordinary voltage-driven region and
			// its existing Case-0 path applies it. prob2big:955-957 does the
			// same for the harmonic Case 2.
			if (circproplist[i].Case==2)
				fprintf(fp,"0	%.17g\n",
					L.b[NumNodes+NumFluxConstraints+NumCurrentConstraints+i]);
		}
	}

	// print out information on periodic boundary conditions for 
	// possible re-use in AC incremental permeability solutions
	fprintf(fp,"%i\n",NumPBCs);
	for(k=0;k<NumPBCs;k++) fprintf(fp,"%i	%i	%i\n",pbclist[k].x,pbclist[k].y,pbclist[k].t);
	
	// print out information on periodic boundary conditions for 
	// possible re-use in AC incremental permeability solutions
	// and in post-processing of forces and torques
	fprintf(fp,"%i\n",NumAGEs);
	for(i=0;i<NumAGEs;i++){
		fprintf(fp,"\"%s\"\n",agelist[i].BdryName);
		fprintf(fp,"%i %.17g %.17g %.17g %.17g %.17g %.17g %.17g %i %.17g %.17g\n",
			agelist[i].BdryFormat,agelist[i].InnerAngle,agelist[i].OuterAngle,
			agelist[i].ri,agelist[i].ro,agelist[i].totalArcLength,
			agelist[i].agc.re,agelist[i].agc.im,agelist[i].totalArcElements,
			agelist[i].InnerShift,agelist[i].OuterShift);
		for(k=0;k<=agelist[i].totalArcElements;k++){
			fprintf(fp,"%i %.17g %i %.17g %i %.17g %i %.17g\n",
				agelist[i].node[k].n0, agelist[i].node[k].w0,
				agelist[i].node[k].n1, agelist[i].node[k].w1,
				agelist[i].node[k].n2, agelist[i].node[k].w2,
				agelist[i].node[k].n3, agelist[i].node[k].w3);
		}
	}

	// Trailing [AgeRings] section: each air gap element's raw ring node
	// lists. The quad-point block above is angle-BAKED -- sorted at this
	// solution's rotor position -- so it cannot be re-paired for a new
	// angle. These lists are angle-invariant and let a transient step reuse
	// this mesh, rebuilding the pairing at its own rotor position instead of
	// remeshing. Trailing placement keeps the format backward-compatible:
	// every existing reader parses the AGE block positionally and stops.
	{
		int bRings=(NumAGEs>0);
		for(i=0;i<NumAGEs;i++)
			if (agelist[i].ringInner==NULL || agelist[i].ringOuter==NULL) bRings=0;
		if (bRings){
			fprintf(fp,"[AgeRings]\n");
			for(i=0;i<NumAGEs;i++){
				fprintf(fp,"%i\n",agelist[i].totalArcElements);
				for(k=0;k<agelist[i].totalArcElements;k++)
					fprintf(fp,"%i %i\n",agelist[i].ringInner[k],agelist[i].ringOuter[k]);
			}
		}
	}

	fclose(fp);
	return TRUE;
}


int CFemmeDocCore::MatlabDumpStatic2D(CBigLinProb &L)
{
	FILE *fp;
	int k;

	CString path = PathName;
	if ((fp=femm_fopen(path + ".m","wt"))==NULL){
		// couldn't open file
		return 0;
	}

	// rhs of problem
	fprintf(fp,"b = [");
	for (k=0;k<L.n;k++){
		fprintf(fp,"%.15g",L.b[k]);
		if ((k+1)==L.n) fprintf(fp,"]");
		fprintf(fp,";\n");
	}
	CString MatrixFileName = path.Mid(path.ReverseFind('\\')+1);
	fprintf(fp,"load %s.dat;\n",(const char*)MatrixFileName);
	fprintf(fp,"M = spconvert(%s);\n",(const char*)MatrixFileName);

	// "stiffness" matrix
	L.SaveMe(path +  ".dat");
	
	return TRUE;
}