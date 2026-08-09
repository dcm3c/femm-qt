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

#define Log log

int CFemmeDocCore::StaticAxisymmetric(CBigLinProb &L)
{
	int i,j,k,s,w;
	double Me[3][3],Mx[3][3],My[3][3],Mxy[3][3],Mn[3][3];
	double l[3],p[3],q[3],g[3],be[3],u[3],v[3],res,lastres=0,dv,vol;
	int n[3];					// numbers of nodes for a particular element;
	double a,K,r,t=0,x,y,B,mu,R,rn[3],a_hat,R_hat,Cduct;
	double c=PI*4.e-05;
	double units[]={2.54,0.1,1.,100.,0.00254,1.e-04};
	double *V_old,*CircInt1=0,*CircInt2=0,*CircInt3=0,*CircInt2t=0;
	int flag,Iter=0,pctr;
	int LinearFlag=TRUE;
	int bIncremental = FALSE;
	int bRestart = FALSE;
	double murel, muinc;

	if (PrevSoln.GetLength() > 0) bIncremental = PrevType;
	res=0;

	// Transient (backward-Euler) analysis; see prob1big for the rationale
	// behind keeping this independent of PrevType.
	const int bTransient = (DeltaT>0);
	const int VoltRowBase = NumNodes + NumFluxConstraints + NumCurrentConstraints;

	CElement *El;
	V_old=(double *) calloc(NumNodesPlus,sizeof(double));

	const int bAprevLoaded = (bTransient && Aprev!=NULL);

	if ((bTransient) && (Aprev==NULL))
		Aprev=(double *) calloc(NumNodes,sizeof(double));

	for(i=0;i<NumBlockLabels;i++) GetFillFactor(i);

	extRo*=units[LengthUnits];
	extRi*=units[LengthUnits];
	extZo*=units[LengthUnits];

	// check to see if any circuits have been defined and process them;
	if (NumCircProps>0)
	{
		CircInt1=(double *)calloc(NumCircProps,sizeof(double));
		CircInt2=(double *)calloc(NumCircProps,sizeof(double));
		CircInt3=(double *)calloc(NumCircProps,sizeof(double));
		// Conductivity integral for the transient classification -- see the
		// planar twin for why it cannot reuse CircInt2.
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
				r=(meshnode[n[0]].x+meshnode[n[1]].x+meshnode[n[2]].x)/3.;

				// if coils are wound, they act like they have
				// a zero "bulk" conductivity...
				Cduct=blockproplist[El->blk].Cduct;
				if (labellist[El->lbl].bIsWound) Cduct=0;

				// evaluate integrals;
				CircInt1[labellist[El->lbl].InCircuit]+=a;

				CircInt2[labellist[El->lbl].InCircuit]+=
					100.*a*Cduct/r;
				CircInt2t[labellist[El->lbl].InCircuit]+=
					100.*a*EddyCduct(El->blk,El->lbl)/r;
				
				CircInt3[labellist[El->lbl].InCircuit]+=
					blockproplist[El->blk].Jr*a*100.;
			}
		}

		for(i=0;i<NumCircProps;i++)
		{
			// Transient solid conductor in a current-driven circuit: the
			// voltage gradient becomes a matrix unknown (prob4big Case 2).
			if (bTransient && (circproplist[i].CircType==0) && (CircInt2t[i]!=0))
			{
				circproplist[i].Case=2;
			}
			else if (circproplist[i].CircType==0)
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
			else{
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

	// Split assembly -- see prob1big/prob4big.
	int baselineOK=1;
	if (Iter==1) L.Wipe();
	else if (Iter>1){
		if (L.RestoreBaseline()==0){ L.Wipe(); baselineOK=0; }
	}
	else{
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
			// mesh reuse: the previous solution is already in memory (raw,
			// as stored); the guess and Aprev rescales below handle units.
			if (bAprevLoaded){
				for(i=0;i<NumNodes;i++) L.V[i]=Aprev[i]/c;
				bRestart=1;
			}
			else
			bRestart = LoadPrevA(PrevSoln,L.V,Aprev);

			if ((bTransient) && (bRestart==0))
			{
				MsgBox("Transient step: could not read the previous solution\n"
				       "\"%s\".\nIt must be a DC solution on the same mesh.",
				       (const char *)PrevSoln);
				return FALSE;
			}
		}

		if (bRestart>0)
		{
			for(i=0;i<NumNodes;i++) if (meshnode[i].x > 1.e-06)	L.V[i]/=(meshnode[i].x*0.01*2*PI);
		}

		// The axisymmetric .ans stores 2*pi*r*A rather than A, so A_prev
		// needs the same rescale the initial guess just got, plus the /c
		// LoadPrevA applies to V but not to the raw copy. On-axis nodes
		// carry no flux, hence no eddy current -- leave them zero.
		if (bTransient && Aprev!=NULL)
		{
			for(i=0;i<NumNodes;i++)
				Aprev[i] = (meshnode[i].x > 1.e-06)
				         ? (Aprev[i]/(meshnode[i].x*0.01*2*PI)) : 0.0;
		}
	}

	int sweepA, sweepB;
	if (Iter==0 || !baselineOK){ sweepA=0; sweepB=0; }
	else if (Iter==1){ sweepA=1; sweepB=2; }
	else { sweepA=2; sweepB=2; }

	for (int sweepSel=sweepA; sweepSel<=sweepB; sweepSel++){
	if (sweepSel==2 && Iter==1) L.SaveBaseline();

	// ---- Transient (backward-Euler) eddy currents, axisymmetric ----------
	//
	// prob4big.cpp:412-430 and :488-497 with j*omega -> 1/DeltaT and real
	// arithmetic, plus the A_prev forcing backward Euler adds. Two things
	// differ from the planar case beyond the radius weighting:
	//
	//  - the mass matrix is LUMPED, not consistent: prob4big interpolates
	//    the induced current as constant over the element, giving an
	//    all-equal 3x3 block (K*4/3 in every entry) rather than the
	//    (2,1,1) pattern. The A_prev forcing inherits that, so every node
	//    of an element gets the same value, proportional to sum(Vprev).
	//  - the Case-2 voltage row's diagonal is K/R where the node coupling
	//    is K/3 -- a real asymmetry, from the 1/r in the axisymmetric
	//    voltage gradient, not a transcription slip.
	//
	// Baseline sweep only: both terms are V-independent within a step, so
	// this keeps them out of the per-pass reassembly and out of the
	// double-add hazard (see prob1big).
	if (bTransient && (sweepSel<=1))
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
		R=(meshnode[n[0]].x+meshnode[n[1]].x+meshnode[n[2]].x)/3.;
		if (R<1.e-06) continue;   // degenerate on-axis sliver

		double Km=R*a*Cduct*c/(6.*DeltaT);
		double Vps=0;
		for(j=0;j<3;j++) Vps += (Aprev!=NULL)?(Aprev[n[j]]/c):0.0;

		for(j=0;j<3;j++){
			for(k=j;k<3;k++) L.AddTo(Km*4./3., n[j], n[k]);
			L.b[n[j]] += Km*4./3.*Vps;
		}

		int ic=labellist[El->lbl].InCircuit;
		if ((ic>=0) && (circproplist[ic].Case==2)){
			int col=VoltRowBase+ic;
			double Kc=2.*a*Cduct*c/DeltaT;
			for(j=0;j<3;j++) L.AddTo(Kc/3., n[j], col);
			L.AddTo(Kc/R, col, col);
			L.b[col] += (Kc/3.)*Vps;
			L.b[col] += 2.*R*blockproplist[El->blk].Jr*a/3.;
		}
	}

	// Total-current right-hand side, once per Case-2 circuit. prob4big:672
	// carries an extra factor of 2 relative to the planar form.
	if (bTransient && (sweepSel<=1))
	for(i=0;i<NumCircProps;i++)
		if (circproplist[i].Case==2)
			L.b[VoltRowBase+i] -= 2.*0.01*circproplist[i].Amps_re;

	// Pin the diagonal of voltage rows no conductor claimed (see prob1big).
	if (bTransient && (sweepSel<=1))
	for(j=0;j<NumVoltageUnknowns;j++)
		if (L.Get(VoltRowBase+j,VoltRowBase+j)==0)
			L.Put(1.0,VoltRowBase+j,VoltRowBase+j);

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
				Me[j][k] = 0.;
				Mx[j][k] = 0.;
				My[j][k] = 0.;
				Mxy[j][k] = 0.;
				Mn[j][k] = 0.;
			}
			be[j]=0.;
		}

		// Determine shape parameters.
		// l == element side lengths;
		// p corresponds to the `b' parameter in Allaire
		// q corresponds to the `c' parameter in Allaire
		El=&meshele[i];		
	
		for(k=0;k<3;k++){
			n[k]=El->p[k];
			rn[k]=meshnode[n[k]].x;
		}

		p[0]=meshnode[n[1]].y - meshnode[n[2]].y;
		p[1]=meshnode[n[2]].y - meshnode[n[0]].y;
		p[2]=meshnode[n[0]].y - meshnode[n[1]].y;	
		q[0]=meshnode[n[2]].x - meshnode[n[1]].x;
		q[1]=meshnode[n[0]].x - meshnode[n[2]].x;
		q[2]=meshnode[n[1]].x - meshnode[n[0]].x;	
		g[0]=(meshnode[n[2]].x + meshnode[n[1]].x)/2.;
		g[1]=(meshnode[n[0]].x + meshnode[n[2]].x)/2.;
		g[2]=(meshnode[n[1]].x + meshnode[n[0]].x)/2.;	

		for(j=0,k=1;j<3;k++,j++){
			if (k==3) k=0;
			l[j]=sqrt( pow(meshnode[n[k]].x-meshnode[n[j]].x,2.) +
					   pow(meshnode[n[k]].y-meshnode[n[j]].y,2.) );
		}

		a=(p[0]*q[1]-p[1]*q[0])/2.;
		R=(meshnode[n[0]].x+meshnode[n[1]].x+meshnode[n[2]].x)/3.;
		
		for(j=0,a_hat=0;j<3;j++) a_hat+=(rn[j]*rn[j]*p[j]/(4.*R));
		a_hat=fabs(a_hat);
		vol=2.*R*a_hat;

		for(j=0,flag=0;j<3;j++) if(rn[j]<1.e-06) flag++;
		switch(flag)
		{
			case 2:
				R_hat=R;

			break;

			case 1:

				if(rn[0]<1.e-06){
					if (fabs(rn[1]-rn[2])<1.e-06) R_hat=rn[2]/2.;
					else R_hat=(rn[1] - rn[2])/(2.*log(rn[1]) - 2.*log(rn[2]));
				}
				if(rn[1]<1.e-06){
					if (fabs(rn[2]-rn[0])<1.e-06) R_hat=rn[0]/2.;
					else R_hat=(rn[2] - rn[0])/(2.*log(rn[2]) - 2.*log(rn[0]));
				}
				if(rn[2]<1.e-06){
					if (fabs(rn[0]-rn[1])<1.e-06) R_hat=rn[1]/2.;
					else R_hat=(rn[0] - rn[1])/(2.*log(rn[0]) - 2.*log(rn[1]));
				}

			break;

			default:

				if (fabs(q[0])<1.e-06)
					R_hat=(q[1]*q[1])/(2.*(-q[1] + rn[0]*log(rn[0]/rn[2])));
				else if (fabs(q[1])<1.e-06)
					R_hat=(q[2]*q[2])/(2.*(-q[2] + rn[1]*log(rn[1]/rn[0])));
				else if (fabs(q[2])<1.e-06)
					R_hat=(q[0]*q[0])/(2.*(-q[0] + rn[2]*log(rn[2]/rn[1])));
			    else
					R_hat=-(q[0]*q[1]*q[2])/
						   (2.*(q[0]*rn[0]*log(rn[0]) + 
						        q[1]*rn[1]*log(rn[1]) + 
								q[2]*rn[2]*log(rn[2])));

			break;
		}

		// Mr Contribution
		// Derived from flux formulation with c0 + c1 r^2 + c2 z 
		// interpolation in the element.
		K=(-1./(2.*a_hat*R));
		for(j=0;j<3;j++)
			for(k=j;k<3;k++)
				Mx[j][k] += K*p[j]*rn[j]*p[k]*rn[k];

		// need this loop to avoid singularities.  This just puts something
		// on the main diagonal of nodes that are on the r=0 line.
		// The program later sets these nodes to zero, but it's good to
		// for scaling reasons to grab entries from the neighboring diagonals
		// rather than just setting these entries to 1 or something....
		for(j=0;j<3;j++)
			if (rn[j]<1.e-06) Mx[j][j]+=Mx[0][0]+Mx[1][1]+Mx[2][2];

		// Mz Contribution;  
		// Derived from flux formulation with c0 + c1 r^2 + c2 z 
		// interpolation in the element.
		K=(-1./(2.*a_hat*R_hat));
		for(j=0;j<3;j++)
			for(k=j;k<3;k++)
				My[j][k] += K*(q[j]*rn[j])*(q[k]*rn[k])*
			                  (g[j]/R)*(g[k]/R);
		
		// Mrz Contribution;  
		// Derived from flux formulation with c0 + c1 r^2 + c2 z 
		// interpolation in the element.
		K = (-1. / (2.*a_hat*R_hat));
		for (j = 0;j<3;j++)
			for (k = j;k<3;k++)
				Mxy[j][k] += K*((q[j] * rn[j])*(g[j] / R))*(p[k] * rn[k]) + K*((q[k] * rn[k])*(g[k] / R))*(p[j] * rn[j]);

		// Fill out rest of entries of Mx and My;
		Mx[1][0]=Mx[0][1]; Mx[2][0]=Mx[0][2]; Mx[2][1]=Mx[1][2];
		My[1][0]=My[0][1]; My[2][0]=My[0][2]; My[2][1]=My[1][2];
		Mxy[1][0] = Mxy[0][1]; Mxy[2][0] = Mxy[0][2]; Mxy[2][1] = Mxy[1][2];

		// contributions to Me, be from derivative boundary conditions;
		for(j=0;j<3;j++){
			if (El->e[j] >= 0)
			if (lineproplist[El->e[j]].BdryFormat==2)
			{
				// conversion factor is 10^(-4) (I think...)
				k=j+1; if(k==3) k=0;
				r=(meshnode[n[j]].x+meshnode[n[k]].x)/2.;
				K=-0.0001*c*2.*r*lineproplist[ El->e[j] ].c0.re*l[j]/6.;
				k=j+1; if(k==3) k=0;
				Me[j][j]+=K*2.;
				Me[k][k]+=K*2.;
				Me[j][k]+=K;
				Me[k][j]+=K;

				K=(lineproplist[ El->e[j] ].c1.re*l[j]/2.)*0.0001*2*r;
				be[j]+=K;
				be[k]+=K;
			}
		}

		// contribution to be from current density in the block
		for(j=0,El->Jprev=0;j<3;j++)
		{
			// see the matching comment in prob1big: clear t FIRST, or a case
			// the branches below do not assign inherits the previous element's
			// value.
			t=0;
			if(labellist[El->lbl].InCircuit>=0)
			{
				k=labellist[El->lbl].InCircuit;
				if(circproplist[k].Case==1) t=circproplist[k].J.Re();
				if(circproplist[k].Case==0)
					t=-100.*circproplist[k].dV.Re()*blockproplist[El->blk].Cduct/R;
			}
			K=-2.*R*(blockproplist[El->blk].Jr+t)*a/3.;
			be[j]+=K;

			// record avg current density in the block for use in incremental solutions
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
			r=(meshnode[n[j]].x+meshnode[n[k]].x)/2.;
			// need to scale so that everything is in proper units...
			// conversion is 0.0001
			K=-0.0001*r*blockproplist[El->blk].H_c*(
				  cos(t*PI/180.)*(meshnode[n[k]].x-meshnode[n[j]].x) +
				  sin(t*PI/180.)*(meshnode[n[k]].y-meshnode[n[j]].y) );
			be[j]+=K;
			be[k]+=K;
		}	

		// update permeability for the element;
		if (Iter==0){ 
			k=meshele[i].blk;
		
			if (blockproplist[k].LamType == 0) {
				mu = blockproplist[k].LamFill;
				meshele[i].mu1 = blockproplist[k].mu_x*mu;
				meshele[i].mu2 = blockproplist[k].mu_y*mu;
			}
			if (blockproplist[k].LamType == 1) {
				mu = blockproplist[k].LamFill;
				K = blockproplist[k].mu_x;
				meshele[i].mu1 = K*mu + (1. - mu);
				meshele[i].mu2 = K / (mu + K*(1. - mu));
			}
			if (blockproplist[k].LamType == 2) {
				mu = blockproplist[k].LamFill;
				K = blockproplist[k].mu_y;
				meshele[i].mu1 = K*mu + (1. - mu);
				meshele[i].mu2 = K / (mu + K*(1. - mu));
			}
			if (blockproplist[k].LamType>2)
			{
				meshele[i].mu1 = 1;
				meshele[i].mu2 = 1;
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
					GetPrevAxiB(i,B1p,B2p);
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
						//	MsgBox("muinc = %g, murel=%g",muinc,murel);
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
				//	Derive B directly from energy;
				v[0]=0;v[1]=0;v[2]=0;
				for(j=0;j<3;j++)
					for(w=0;w<3;w++)
						v[j]+=(Mx[j][w]+My[j][w])*L.V[n[w]];
				for(j=0,dv=0;j<3;j++) dv+=L.V[n[j]]*v[j];
				dv*=(10000.*c*c/vol); 
				B=sqrt(fabs(dv));

				// find out new mu from saturation curve;
				blockproplist[k].GetBHProps(B,mu,dv);
				mu=1./(muo*mu);
				meshele[i].mu1=mu;
				meshele[i].mu2=mu;
				for(j=0;j<3;j++){
					for(w=0,v[j]=0;w<3;w++)
						v[j]+=(Mx[j][w]+My[j][w])*L.V[n[w]];
				}

				K=-200.*c*c*c*dv/vol;
				for(j=0;j<3;j++)
					for(w=0;w<3;w++)
						Mn[j][w]=K*v[j]*v[w];
			}

			if ((blockproplist[k].LamType==1) && (blockproplist[k].BHpoints>0)){
				
				//	Derive B directly from energy;
				t=blockproplist[k].LamFill;
				v[0]=0;v[1]=0;v[2]=0;
				for(j=0;j<3;j++)
					for(w=0;w<3;w++)
						v[j]+=(Mx[j][w]+My[j][w]/(t*t))*L.V[n[w]];
				for(j=0,dv=0;j<3;j++) dv+=L.V[n[j]]*v[j];
				dv*=(10000.*c*c/vol); 
				B=sqrt(fabs(dv));

				// Evaluate BH curve
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
				K=-100.*c*c*c*dv/(vol);
				for(j=0;j<3;j++)
					for(w=0;w<3;w++)
						Mn[j][w]=K*(v[j]*u[w]+v[w]*u[j]);
			}
			if ((blockproplist[k].LamType==2) && (blockproplist[k].BHpoints>0)){

				//	Derive B directly from energy;
				t=blockproplist[k].LamFill;
				v[0]=0;v[1]=0;v[2]=0;
				for(j=0;j<3;j++)
					for(w=0;w<3;w++)
						v[j]+=(Mx[j][w]/(t*t)+My[j][w])*L.V[n[w]];
				for(j=0,dv=0;j<3;j++) dv+=L.V[n[j]]*v[j];
				dv*=(10000.*c*c/vol); 
				B=sqrt(fabs(dv));
				
				// Evaluate BH curve
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
				K=-100.*c*c*c*dv/(vol);
				for(j=0;j<3;j++)
					for(w=0;w<3;w++)
						Mn[j][w]=K*(v[j]*u[w]+v[w]*u[j]);
		
			}
		}

		// "Warp" the permeability of this element if part of 
		// the conformally mapped external region
		if((labellist[meshele[i].lbl].IsExternal) && (Iter==0))
		{
			double Z=(meshnode[n[0]].y+meshnode[n[1]].y+meshnode[n[2]].y)/3. - extZo;
			double kludge=(R*R+Z*Z)*extRi/(extRo*extRo*extRo);
			meshele[i].mu1/=kludge;
			meshele[i].mu2/=kludge;
		}

		// combine block matrices into global matrices;
		for(j=0;j<3;j++)
			for(k=0;k<3;k++){
				Me[j][k]+= (Mx[j][k]/Re(El->mu2) + My[j][k]/Re(El->mu1) + Mxy[j][k] * Re(El->v12) + Mn[j][k]);
				be[j]+=Mn[j][k]*L.V[n[k]];
			}
			
		for (j=0;j<3;j++){
			for (k=j;k<3;k++)
				L.Put(L.Get(n[j],n[k])-Me[j][k],n[j],n[k]);
			L.b[n[j]]-=be[j];
		}
	}
	}  // end sweepSel (split assembly)

	// add in contribution from point currents;
	for(i=0;i<NumNodes;i++)
		if(meshnode[i].bc>=0)
		{
			r=meshnode[i].x;
			L.b[i]+=(0.01*nodeproplist[meshnode[i].bc].Jr*2.*r);
		}

	// apply fixed boundary conditions at points;
	for(i=0;i<NumNodes;i++)
	{
		if(fabs(meshnode[i].x)<(units[LengthUnits]*1.e-06)) L.SetValue(i,0.);
		else 
			if(meshnode[i].bc >=0)
			if((nodeproplist[meshnode[i].bc].Jr==0) &&
			   (nodeproplist[meshnode[i].bc].Ji==0))
					L.SetValue(i,nodeproplist[meshnode[i].bc].Ar/c);
	}

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
					if(x!=0) L.SetValue(meshele[i].p[j],a/c);
					
					// second point on the side;
					x=meshnode[meshele[i].p[k]].x;
					y=meshnode[meshele[i].p[k]].y;
					x/=units[LengthUnits]; y/=units[LengthUnits];
					s=meshele[i].e[j];
					a=lineproplist[s].A0 + x*lineproplist[s].A1 +
					  y*lineproplist[s].A2;
					// just take``real'' component.
					a*=cos(lineproplist[s].phi*DEG); 
					if (x!=0) L.SetValue(meshele[i].p[k],a/c);
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
					if (x!=0) L.SetValue(meshele[i].p[j],a/c);
					
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
					if (x!=0) L.SetValue(meshele[i].p[k],a/c);
				}

			}
		}

	// Apply any periodicity/antiperiodicity boundary conditions that we have
	for(k=0;k<NumPBCs;k++)
	{
		if (pbclist[k].t==0) L.Periodicity(pbclist[k].x,pbclist[k].y);
		if (pbclist[k].t==1) L.AntiPeriodicity(pbclist[k].x,pbclist[k].y);
	}

	// solve the problem;
	for(j=0;j<NumNodes;j++) V_old[j]=L.V[j];
	if (bTransient) for(j=VoltRowBase;j<NumNodesPlus;j++) V_old[j]=L.V[j];

	// Inexact inner solves for the nonlinear iteration -- see prob1big.
	L.Precision=std::min(1.e-4,1.e-4*res);
	if (L.Precision<Precision) L.Precision=Precision;

	if (L.PCGSolve(Iter + bRestart)==FALSE) return FALSE;

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
	// full Precision -- see prob1big.
	if((res<100.*Precision) && Iter>0 && (L.Precision<=Precision)) LinearFlag=TRUE;

	Iter++;
	
} while(LinearFlag==FALSE);

	// convert answer back to Webers for plotting purposes.
	for (i=0;i<NumNodes;i++){
		L.b[i]=L.V[i]*c;
		L.b[i]*=(meshnode[i].x*0.01*2*PI);
	}
	for (i=NumNodes;i<NumNodesPlus;i++) L.b[i]=L.V[i];
	// Convert the voltage unknown to the dV the postprocessor expects.
	// prob4big:827 carries an extra 0.01 relative to the planar form.
	if (bTransient)
		for (i=0;i<NumCircProps;i++)
			if (circproplist[i].Case==2)
				L.b[VoltRowBase+i]=c*0.01*L.V[VoltRowBase+i]/DeltaT;

	free(V_old);
	if(NumCircProps>0){
		free(CircInt1);
		free(CircInt2);
		free(CircInt3);
		free(CircInt2t);
	}

	return TRUE;
}
