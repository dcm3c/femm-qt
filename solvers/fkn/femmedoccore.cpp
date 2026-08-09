// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

// FemmeDocCore.cpp : implementation of the CFemmeDocCore class
//

 
#include "StdAfx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "fkn.h"
#include "fknDlg.h"
#include "complex.h"
#include "mesh.h"
#include "spars.h"
#include "femmedoccore.h"
#include "utf8_io.h"
#include "fastio.h"
#include <string.h>
#include <vector>
#include <algorithm>

// The .ans stores an air-gap-element name quoted ("AGE") with a trailing newline.
// Strip both so the name round-trips without accumulating quotes when a previous
// solution is reloaded (e.g. an incremental analysis) and re-written.
static void stripAgeName(char *s)
{
	int L = (int)strlen(s);
	while (L > 0 && (s[L-1] == '\n' || s[L-1] == '\r')) s[--L] = 0;
	if (L >= 2 && s[0] == '"' && s[L-1] == '"') { s[L-1] = 0; memmove(s, s + 1, L - 1); }
}

/////////////////////////////////////////////////////////////////////////////
// CFemmeDocCore construction/destruction

CFemmeDocCore::CFemmeDocCore()
{
	TheView = 0;
	Frequency = 0;
	Precision = 0;
	Relax = 0;
	LengthUnits = 0;
	ProblemType = 0;
	Coords = 0;
	BandWidth = 0;
	NumNodes = 0;
	NumEls = 0;
	NumBlockProps = 0;
	NumPBCs = 0;
	NumLineProps = 0;
	NumPointProps = 0;
	NumCircProps = 0;
	NumBlockLabels = 0;
	NumCircPropsOrig = 0;
	NumAGEs = 0;

	meshnode = 0;
	meshele = 0;
	blockproplist = 0;
	lineproplist = 0;
	nodeproplist = 0;
	circproplist = 0;
	labellist = 0;
	pbclist = 0;
	PathName = 0;
	Aprev = 0;
	PrevType = 0;
	DeltaT = 0;
	NumVoltageUnknowns = 0;
	NumNodesPlus = 0;
	extRo = extRi = extZo = 0;
	agelist = 0;
	fluxproplist = 0;
	CircuitMatrix = 0;
	ConstraintMatrix = 0;
	currentlist = 0;
	bDump = FALSE;
}

CFemmeDocCore::~CFemmeDocCore()
{
	// This space for rent.
}

void CFemmeDocCore::CleanUp()
{
	int k;

	if (meshnode!=NULL)		free(meshnode);
	if (meshele!=NULL)		free(meshele);
	if (blockproplist!=NULL){
		for(k=0;k<NumBlockProps;k++)
		{
			if(blockproplist[k].Bdata!=NULL) free(blockproplist[k].Bdata);
			if(blockproplist[k].Hdata!=NULL) free(blockproplist[k].Hdata);
			if(blockproplist[k].slope!=NULL) free(blockproplist[k].slope);
		}
		free(blockproplist);
	}
	if (lineproplist!=NULL)	 free(lineproplist);
	if (nodeproplist!=NULL)	 free(nodeproplist);
	if (circproplist!=NULL)
	{
		for(k=0;k<NumCircPropsOrig;k++)
		{
			// need to free up little strings that hold all the circuit names
			// strings were only allocated for the "original circuits";  copies point to
			// those original strings.  Therefore, we just cycle through
			// NumCircPropsOrig to free the strings...
			if(circproplist[k].CircName!=NULL) free(circproplist[k].CircName);
		}
		free(circproplist);
	}
	if (labellist!=NULL){
		for(k=0;k<NumBlockLabels;k++)
			if(labellist[k].MagDirFctn!=NULL) free(labellist[k].MagDirFctn);
		free(labellist);
	}
	if (CircuitMatrix!=NULL){
		for(k=0;k<NumFluxConstraints;k++)
			if(CircuitMatrix[k]!=NULL) free(CircuitMatrix[k]);
		free(CircuitMatrix);
	}
	if (ConstraintMatrix!=NULL){
		for(k=0;k<NumCurrentConstraints;k++)
			if(ConstraintMatrix[k]!=NULL) free(ConstraintMatrix[k]);
		free(ConstraintMatrix);
	}
	if (currentlist!=NULL) free(currentlist);
	if (pbclist!=NULL)		 free(pbclist);
	for (k=0;k<NumAGEs;k++){
		if (agelist[k].node!=NULL)      free(agelist[k].node);
		if (agelist[k].ringInner!=NULL) free(agelist[k].ringInner);
		if (agelist[k].ringOuter!=NULL) free(agelist[k].ringOuter);
	}
	if (agelist!=NULL)		 free(agelist);
	if (fluxproplist!=NULL)  free(fluxproplist);
	if (Aprev!=NULL)		 free(Aprev);
}

/////////////////////////////////////////////////////////////////////////////
// CFemmeDocCore commands

char* CFemmeDocCore::StripKey(char *c)
{
	char *d;
	int i,k;

	k=(int) strlen(c);

	for(i=0;i<k;i++){
		if (c[i] == '='){
			d=c+i+1;
			return d;
		}
	} 

	return c+k;
}

char* CFemmeDocCore::ParseDbl(char *t, double *f)
{
	if (t==NULL) return NULL;

	int i,j,k,u,ws;
	static char w[]="\t, \n";
	char *v;

	k=(int) strlen(t); if(k==0) return NULL;

	for(i=0,u=0,v=0;i<k;i++){
		for(j=0,ws=0;j<4;j++){
			if (t[i]==w[j]){
				ws=1;
				if (u==1) u=2;
			}
		}
		if ((ws==0) && (u==0)) u=1;
		if ((ws==0) && (u==2)){
			v=t+i;
			break;
		}
	}
	
	if (u==0) return NULL;	//nothing left in the string;
	if (v==NULL) v=t+k;
	
	sscanf(t,"%lf",f);
	
	return v;
}

char* CFemmeDocCore::ParseInt(char *t, int *f)
{
	if (t==NULL) return NULL;

	int i,j,k,u,ws;
	static char w[]="\t, \n";
	char *v;

	k=(int) strlen(t); if(k==0) return NULL;

	for(i=0,u=0,v=0;i<k;i++){
		for(j=0,ws=0;j<4;j++){
			if (t[i]==w[j]){
				ws=1;
				if (u==1) u=2;
			}
		}
		if ((ws==0) && (u==0)) u=1;
		if ((ws==0) && (u==2)){
			v=t+i;
			break;
		}
	}
	
	if (u==0) return NULL;	//nothing left in the string;
	if (v==NULL) v=t+k;
	
	sscanf(t,"%i",f);
	
	return v;
}

char* CFemmeDocCore::ParseString(char *t, CString *s)
{
	if (t==NULL) return NULL;
	if (strlen(t)==0) return t;

	int n1,n2,k;
	
	// find first quote in the source string
	for(k=0,n1=-1;k< (int) strlen(t);k++)
	{
		if (t[k]=='\"'){
			n1=k;
			break;
		}
	}

	if (n1<0) return t;

	// find second quote in the source string
	for(k=n1+1,n2=-1;k< (int) strlen(t);k++)
	{
		if (t[k]=='\"'){
			n2=k;
			break;
		}
	}

	if (n2<0) return t;
	
	*s=t;
	*s=s->Mid(n1+1,n2-n1-1);

	return (t+n2+1);
}

// Capture the geometry sections of a .fem-format stream -- [NumPoints],
// [NumSegments], [NumArcSegments], [NumHoles], [NumBlockLabels], each a
// count line followed by that many records -- as one string. Works on both
// a .fem and a .ans, since the .ans opens with a verbatim echo of its .fem.
// Returns FALSE if the sections cannot be found.
static int SlurpGeometry(FILE *fp, std::string &out)
{
	static const char *keys[]={"[numpoints]","[numsegments]",
		"[numarcsegments]","[numholes]","[numblocklabels]"};
	char s[1024];
	out.clear();
	for(int sec=0;sec<5;sec++){
		int n=-1;
		while (fgets(s,1024,fp)!=NULL){
			if (_strnicmp(s,keys[sec],(int)strlen(keys[sec]))==0){
				char *v=s; while(*v && *v!='=') v++;
				if (*v=='=') sscanf(v+1,"%i",&n);
				break;
			}
			// bail if we ran into the solution section without finding it
			if (_strnicmp(s,"[solution]",10)==0) return FALSE;
		}
		if (n<0) return FALSE;
		out+=keys[sec]; out+="="; 
		char cnt[32]; snprintf(cnt,sizeof(cnt),"%i\n",n); out+=cnt;
		for(int i=0;i<n;i++){
			if (fgets(s,1024,fp)==NULL) return FALSE;
			out+=s;
		}
	}
	return TRUE;
}

int CFemmeDocCore::LoadPrevA(CString myFile, double *V, double *Ap)
{
	// _Just_ load Aprev. This is needed for the case where we are restarting
	// from an old solution.  However, rotor angle might be slightly different
	// in this case, so we need to reload all the mesh info to get the right
	// mapping for any AGEs.  Only Aprev needs to be reloaded then.
	if (myFile.GetLength()==0) return FALSE;

	FILE *fp;
	int i,k;
	char s[1024],q[256];
	char *v;
	double prevFreq=0;
	int prevNumNodes;

	if ((fp=femm_fopen(myFile,"rt"))==NULL){
//		MsgBox("Couldn't read from specified previous solution\n");
		return FALSE;
	}

	// parse the file
	k=0;
	while (fgets(s,1024,fp)!=NULL)
	{
		sscanf(s,"%s",q);
		
		// Frequency of the problem
		if( _strnicmp(q,"[frequency]",11)==0){
			v=StripKey(s);
			sscanf(v,"%lf",&prevFreq);
			q[0]=0;
		}

		sscanf(s,"%s",q);
		if( _strnicmp(q,"[solution]",11)==0){
			k=1;
			break;
		}
	}

	// case where the solution is never found.
	if (k==0)
	{
		fclose(fp);
//		MsgBox("Couldn't read from specified previous solution\n");
		return FALSE;
	}

	// case were previous solution is an AC problem.
	// only DC  previous solutions are presently supported
	if (prevFreq!=0)
	{
		fclose(fp);
//		MsgBox("Only DC previous solutions are presently supported\n");
		return FALSE;
	}

	////////////////////////////
	// read in the previous solution!!!
	///////////////////////////

	// read in nodes
	fgets(s,1024,fp);
	sscanf(s,"%i",&prevNumNodes);
	if (prevNumNodes!=NumNodes)
	{
		fclose(fp);
//		MsgBox("%i != %i\n",NumNodes,prevNumNodes);
		return FALSE;
	}

	CNode node;
	double c=PI*4.e-05;
	for(i=0;i<NumNodes;i++){
		// A truncated or misaligned node section must be a hard failure --
		// under DeltaT>0 the caller treats FALSE as "cannot take this step",
		// and silently accepting a short read here would put garbage in
		// A_prev instead. Every node record has at least x y A bc.
		const char *p=s;
		if (fgets(s,1024,fp)==NULL ||
		    !fio_double(p,node.x) || !fio_double(p,node.y) ||
		    !fio_double(p,V[i])   || !fio_int(p,node.bc))
		{
			fclose(fp);
			return FALSE;
		}
		if (Ap) Ap[i]=V[i];   // raw Wb/m, as LoadPrev() and the .ans writer use
		V[i]/=c;
	}

	fclose(fp);
	return TRUE;
}

int CFemmeDocCore::LoadPrev()
{
	if (PrevSoln.GetLength()==0) return FALSE;

	FILE *fp;
	int i,k;
	char s[1024],q[256];
	char *v;
	double prevFreq=0;
	double c[]={2.54,0.1,1.,100.,0.00254,1.e-04};
	if ((fp=femm_fopen(PrevSoln,"rt"))==NULL){
		MsgBox("Couldn't read from specified previous solution\n");
		return FALSE;
	}

	// parse the file
	k=0;
	while (fgets(s,1024,fp)!=NULL)
	{
		sscanf(s,"%s",q);
		
		// Frequency of the problem
		if( _strnicmp(q,"[frequency]",11)==0){
			v=StripKey(s);
			sscanf(v,"%lf",&prevFreq);
			q[0]=0;
		}

		sscanf(s,"%s",q);
		if( _strnicmp(q,"[solution]",11)==0){
			k=1;
			break;
		}
	}

	// case where the solution is never found.
	if (k==0)
	{
		fclose(fp);
		MsgBox("Couldn't read from specified previous solution\n");
		return FALSE;
	}

	// case were previous solution is an AC problem.
	// only DC  previous solutions are presently supported
	if (prevFreq!=0)
	{
		fclose(fp);
		MsgBox("Only DC previous solutions are presently supported\n");
		return FALSE;
	}

	////////////////////////////
	// read in the previous solution!!!
	///////////////////////////

	// read in nodes
	fgets(s,1024,fp);
	sscanf(s,"%i",&NumNodes);
	Aprev=(double *)calloc(NumNodes,sizeof(double));
	meshnode=(CNode *)calloc(NumNodes,sizeof(CNode));
	CNode node;
	for(i=0;i<NumNodes;i++){
		// cursor parse (see fastio.h); a malformed record is a hard failure,
		// same policy as LoadPrevA
		const char *p=s;
		if (fgets(s,1024,fp)==NULL ||
		    !fio_double(p,node.x) || !fio_double(p,node.y) ||
		    !fio_double(p,Aprev[i]) || !fio_int(p,node.bc))
		{
			fclose(fp);
			return FALSE;
		}

		// convert all lengths to centimeters (better conditioning this way...)
		node.x*=c[LengthUnits];
		node.y*=c[LengthUnits];

		meshnode[i]=node;
	}

	// read elements
	fgets(s,1024,fp);
	sscanf(s,"%i",&NumEls);
	meshele=(CElement *)calloc(NumEls,sizeof(CElement));
	CElement elm;
	for(i=0;i<NumEls;i++)
	{
		const char *p=s;
		if (fgets(s,1024,fp)==NULL ||
		    !fio_int(p,elm.p[0]) || !fio_int(p,elm.p[1]) ||
		    !fio_int(p,elm.p[2]) || !fio_int(p,elm.lbl)  ||
		    !fio_int(p,elm.e[0]) || !fio_int(p,elm.e[1]) ||
		    !fio_int(p,elm.e[2]) || !fio_double(p,elm.Jprev))
		{
			fclose(fp);
			return FALSE;
		}
		// look up block type out of the list of block labels
		elm.blk=labellist[elm.lbl].BlockType;
		meshele[i]=elm;
	}

	// scroll through block label info
	fgets(s,1024,fp);
	sscanf(s,"%i",&k);
	for(i=0;i<k;i++) fgets(s,1024,fp);

	// read in PBC list
	if (fgets(s,1024,fp)!=NULL)
	{
		sscanf(s,"%i",&NumPBCs);
		if (NumPBCs!=0) pbclist=(CCommonPoint *)calloc(NumPBCs,sizeof(CCommonPoint));
		CCommonPoint pbc;
		for(i=0;i<NumPBCs;i++){
			fgets(s,1024,fp);
			sscanf(s,"%i	%i	%i\n",&pbc.x,&pbc.y,&pbc.t);
			pbclist[i]=pbc;
		}
	}

	fgets(s,1024,fp); sscanf(s,"%i",&NumAGEs);
	if (NumAGEs!=0) agelist=(CAirGapElement *)calloc(NumAGEs,sizeof(CAirGapElement));
	CAirGapElement age;
	for(i=0;i<NumAGEs;i++){
		fgets(age.BdryName,80,fp);
		stripAgeName(age.BdryName);   // the .ans stores the name as "AGE" -- drop quotes/newline
		fgets(s,1024,fp);
		sscanf(s,"%i %lf %lf %lf %lf %lf %lf %lf %i %lf %lf",
			&age.BdryFormat,&age.InnerAngle,&age.OuterAngle,
			&age.ri,&age.ro,&age.totalArcLength,
			&age.agc.re,&age.agc.im,&age.totalArcElements,
			&age.InnerShift,&age.OuterShift);
		age.node=(CQuadPoint *)calloc(age.totalArcElements+1,sizeof(CQuadPoint));
		for(k=0;k<=age.totalArcElements;k++){
			fgets(s,1024,fp);
			sscanf(s,"%i %lf %i %lf %i %lf %i %lf",
				&age.node[k].n0, &age.node[k].w0,
				&age.node[k].n1, &age.node[k].w1,
				&age.node[k].n2, &age.node[k].w2,
				&age.node[k].n3, &age.node[k].w3);
		}
		age.ringInner=NULL;
		age.ringOuter=NULL;
		agelist[i]=age;
	}

	// Everything below restores ASSIGNMENTS from the previous solution --
	// node bc markers, element edge boundary indices, element region labels
	// -- while property VALUES always come from the current .fem. That is
	// only valid if the two runs assign identically, so compare the geometry
	// sections of the current .fem against the verbatim .fem echo the .ans
	// opens with. A mismatch (e.g. an initial-condition file that clamps a
	// different face than the transient does) falls back to remeshing, which
	// handles any assignment correctly.
	if (DeltaT>0)
	{
		std::string gnow, gprev;
		FILE *ff;
		snprintf(s,sizeof(s),"%s.fem",PathName);
		if ((ff=femm_fopen(s,"rt"))==NULL){ fclose(fp); return FALSE; }
		int okNow=SlurpGeometry(ff,gnow);
		fclose(ff);
		long here=ftell(fp);
		rewind(fp);
		int okPrev=SlurpGeometry(fp,gprev);
		fseek(fp,here,SEEK_SET);
		if (!okNow || !okPrev || gnow!=gprev){ fclose(fp); return FALSE; }
	}

	// ---- optional trailing [AgeRings] section ----
	// The quad-point mapping just read is angle-BAKED: it was sorted at the
	// previous step's rotor position. The raw ring node lists appended after
	// the AGE block are angle-invariant, and are what lets a transient step
	// reuse this mesh while re-pairing the gap at the CURRENT angle.
	int bHaveRings=0;
	if ((NumAGEs>0) && (fgets(s,1024,fp)!=NULL) &&
	    (_strnicmp(s,"[AgeRings]",10)==0))
	{
		bHaveRings=1;
		for(i=0;i<NumAGEs;i++){
			int nn=0;
			fgets(s,1024,fp);
			sscanf(s,"%i",&nn);
			if (nn!=agelist[i].totalArcElements){ bHaveRings=0; break; }
			agelist[i].ringInner=(int *)calloc(nn,sizeof(int));
			agelist[i].ringOuter=(int *)calloc(nn,sizeof(int));
			for(k=0;k<nn;k++){
				fgets(s,1024,fp);
				sscanf(s,"%i %i",&agelist[i].ringInner[k],&agelist[i].ringOuter[k]);
			}
		}
	}

	fclose(fp);

	// A transient step re-pairs every air gap at the rotor angle the CURRENT
	// .fem specifies -- the whole point of expressing rotation through the
	// AGE's InnerAngle is that the mesh never changes, only this pairing.
	// Without the ring lists (an old .ans, or a mismatch) reuse is unsafe:
	// return FALSE and the caller falls back to remeshing.
	if ((DeltaT>0) && (NumAGEs>0))
	{
		if (!bHaveRings) return FALSE;

		double cu=c[LengthUnits];
		for(i=0;i<NumAGEs;i++){
			// current angles come from the boundary property of the same name
			int bp=-1;
			for(k=0;k<NumLineProps;k++)
				if (strcmp(lineproplist[k].BdryName,agelist[i].BdryName)==0) bp=k;
			if (bp<0) return FALSE;
			agelist[i].InnerAngle=lineproplist[bp].InnerAngle;
			agelist[i].OuterAngle=lineproplist[bp].OuterAngle;

			// meshnode is in cm here; the .ans param line's center is in
			// problem units. Only differences enter the builder, but they
			// must be taken in ONE unit system.
			int nn=agelist[i].totalArcElements;
			std::vector<double> ix(nn),iy(nn),ox(nn),oy(nn);
			for(k=0;k<nn;k++){
				ix[k]=meshnode[agelist[i].ringInner[k]].x;
				iy[k]=meshnode[agelist[i].ringInner[k]].y;
				ox[k]=meshnode[agelist[i].ringOuter[k]].x;
				oy[k]=meshnode[agelist[i].ringOuter[k]].y;
			}
			BuildAgeRing(agelist[i],ix.data(),iy.data(),ox.data(),oy.data(),
			             agelist[i].agc.re*cu,agelist[i].agc.im*cu);
		}
	}

	return TRUE;
}

int CFemmeDocCore::OnOpenDocument() 
{
	FILE *fp;
	int i,j,k,ic;
	char s[1024],q[1024];
	char *v;
	CPointProp	  PProp;
	CBoundaryProp BProp;
	CMaterialProp MProp;
	CCircuit	  CProp;
	CBlockLabel   blk;

	snprintf(s, sizeof(s),"%s.fem",PathName);
	if ((fp=femm_fopen(s,"rt"))==NULL){
		MsgBox("Couldn't read from specified .fem file");
		return FALSE;
	}

	// define some defaults
	Frequency=0.;
	Precision=1.e-08;
	Relax=1.;
	ProblemType=0;
	Coords=0;
	NumPointProps=0;
	NumLineProps=0;
	NumBlockProps=0;
	NumCircProps=0;
	NumFluxConstraints=0;
	NumCurrentConstraints=0;
	NumVoltageUnknowns=0;
	ACSolver=0;
	Depth=1;
	DeltaT=0;

	// parse the file

	while (fgets(s,1024,fp)!=NULL)
	{
		sscanf(s,"%s",q);
	
		// Frequency of the problem
		if( _strnicmp(q,"[frequency]",11)==0){
			v=StripKey(s);
			sscanf(v,"%lf",&Frequency);
			Frequency=fabs(Frequency);
			q[0]=0;
		}

		// Precision
		if( _strnicmp(q,"[precision]",11)==0){
			v=StripKey(s);
			sscanf(v,"%lf",&Precision);
			q[0]=0;
		}

		// Depth
		if( _strnicmp(q,"[depth]",7)==0){
			v=StripKey(s);
			sscanf(v,"%lf",&Depth);
			q[0]=0;
		}

		// AC Solver Type
		if( _strnicmp(q,"[acsolver]",8)==0){
			v=StripKey(s);
			sscanf(v,"%i",&ACSolver);
			q[0]=0;
		}

		// Units of length used by the problem
		if( _strnicmp(q,"[lengthunits]",13)==0){
			v=StripKey(s);
			sscanf(v,"%s",q);
			if( _strnicmp(q,"inches",6)==0) LengthUnits=0;
			else if( _strnicmp(q,"millimeters",11)==0) LengthUnits=1;
			else if( _strnicmp(q,"centimeters",1)==0) LengthUnits=2;
			else if( _strnicmp(q,"mils",4)==0) LengthUnits=4;
			else if( _strnicmp(q,"microns",6)==0) LengthUnits=5;
			else if( _strnicmp(q,"meters",6)==0) LengthUnits=3;
			q[0]=0;
		}

		// Problem Type (planar or axisymmetric)
		if( _strnicmp(q,"[problemtype]",13)==0){
			v=StripKey(s);
			sscanf(v,"%s",q);
			if( _strnicmp(q,"planar",6)==0) ProblemType=0;
			if( _strnicmp(q,"axisymmetric",3)==0) ProblemType=1;
			q[0]=0;
		}

		// Coordinates (cartesian or polar)
		if( _strnicmp(q,"[coordinates]",13)==0){
			v=StripKey(s);
			sscanf(v,"%s",q);
			if ( _strnicmp(q,"cartesian",4)==0) Coords=0;
			if ( _strnicmp(q,"polar",5)==0) Coords=1;
			q[0]=0;
		}
		
		// properties for axisymmetric external region
		if( _strnicmp(q,"[extzo]",7)==0){
			v=StripKey(s);
			sscanf(v,"%lf",&extZo);
			q[0]=0;
		}

		if( _strnicmp(q,"[extro]",7)==0){
			v=StripKey(s);
			sscanf(v,"%lf",&extRo);
			q[0]=0;
		}

		if( _strnicmp(q,"[extri]",7)==0){
			v=StripKey(s);
			sscanf(v,"%lf",&extRi);
			q[0]=0;
		}

		// name of previous solution file for AC incremental permeability solution
 		// Previous Solution File
		if( _strnicmp(q,"[prevsoln]",10)==0){
			v=StripKey(s);

			// have to do this carefully to accept a filename with spaces
			k=(int) strlen(v);
			for(i=0;i<k;i++)
				if(v[i]=='\"'){
					v=v+i+1;
					i=k;
				}

			k=(int) strlen(v);
			if(k>0) for(i=k-1;i>=0;i--){
				if(v[i]=='\"'){
					v[i]=0;
					i=-1;
				}
			}

			PrevSoln=v;
			q[0]=0;
		}

		// Mis-use the comment field to indicate that files should be 
		if( _strnicmp(q,"[comment]",9)==0){
			v=StripKey(s);
			sscanf(v,"%s",q);
			if( _strnicmp(q,"\"dump\"",6)==0) bDump=TRUE;
			q[0]=0;
		}

		// Previous solution type
		if (_strnicmp(q, "[prevtype]", 10) == 0) {
			v = StripKey(s);
			sscanf(v, "%i", &PrevType);
			q[0] = 0;
			// 0 == None
			// 1 == Incremental
			// 2 == Frozen
			// 3 == Restart
		}

		// Point Properties
		if( _strnicmp(q,"[pointprops]",12)==0){	
			v=StripKey(s);
			sscanf(v,"%i",&k);
			if (k>0) nodeproplist=(CPointProp *)calloc(k,sizeof(CPointProp));
			q[0]=0;
		}

		if( _strnicmp(q,"<beginpoint>",11)==0){	
			PProp.Jr=0.;
			PProp.Ji=0.;
			PProp.Ar=0.;
			PProp.Ai=0.;
			q[0]=0;
		}

		if( _strnicmp(q,"<A_re>",6)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&PProp.Ar);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<A_im>",6)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&PProp.Ai);
		   q[0]=0;
		}
	
		if( _strnicmp(q,"<I_re>",6)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&PProp.Jr);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<I_im>",6)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&PProp.Ji);
		   q[0]=0;
		}

		if( _strnicmp(q,"<endpoint>",9)==0){
			nodeproplist[NumPointProps]=PProp;
			NumPointProps++;
			q[0]=0;
		}

		// Boundary Properties;
		if( _strnicmp(q,"[bdryprops]",11)==0){	
			v=StripKey(s);
			sscanf(v,"%i",&k);
			if (k>0) lineproplist=(CBoundaryProp *)calloc(k,sizeof(CBoundaryProp));
			q[0]=0;
		}
 		
		if( _strnicmp(q,"<beginbdry>",11)==0){	
			BProp.BdryName[0]=0;
			BProp.BdryFormat=0;
			BProp.A0=0.;
			BProp.A1=0.;
			BProp.A2=0.;
			BProp.phi=0.;	
			BProp.Mu=0.;
			BProp.Sig=0.;
			BProp.c0=0.;
			BProp.c1=0.;
			q[0]=0;
		}

		if( _strnicmp(q,"<bdrytype>",10)==0){
		   v=StripKey(s);
		   sscanf(v,"%i",&BProp.BdryFormat);
		   q[0]=0;
		}
		
		if( _strnicmp(q,"<mu_ssd>",8)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&BProp.Mu);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<sigma_ssd>",11)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&BProp.Sig);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<A_0>",5)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&BProp.A0);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<A_1>",5)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&BProp.A1);
		   q[0]=0;
		}	
		
		if( _strnicmp(q,"<A_2>",5)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&BProp.A2);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<phi>",5)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&BProp.phi);
		   q[0]=0;
		}	
		
		if( _strnicmp(q,"<c0>",4)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&BProp.c0.re);
		   q[0]=0;
		}	
		
		if( _strnicmp(q,"<c1>",4)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&BProp.c1.re);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<c0i>",5)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&BProp.c0.im);
		   q[0]=0;
		}	
		
		if( _strnicmp(q,"<c1i>",5)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&BProp.c1.im);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<bdryname>",10)==0){
			CString bn;
			ParseString(s,&bn);
			snprintf(BProp.BdryName,sizeof(BProp.BdryName),"%s",(const char *)bn);
			q[0]=0;
		}

		if( _strnicmp(q,"<innerangle>",12)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&BProp.InnerAngle);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<outerangle>",12)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&BProp.OuterAngle);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<endbdry>",9)==0){
			lineproplist[NumLineProps]=BProp;
			NumLineProps++;
			q[0]=0;
		}

		// Block Properties;
		if( _strnicmp(q,"[blockprops]",12)==0){	
			v=StripKey(s);
			sscanf(v,"%i",&k);
			if (k>0) blockproplist=(CMaterialProp *)calloc(k,sizeof(CMaterialProp));
			q[0]=0;
		}

		if( _strnicmp(q,"<beginblock>",12)==0){	
			MProp.mu_x=1.;
			MProp.mu_y=1.;			// permeabilities, relative
			MProp.H_c=0.;				// magnetization, A/m
			MProp.Jr=0.;
			MProp.Ji=0.;				// applied current density, MA/m^2
			MProp.Cduct=0.;		    // conductivity of the material, MS/m
			MProp.Lam_d=0.;			// lamination thickness, mm
			MProp.Theta_hn=0.;			// hysteresis angle, degrees
			MProp.Theta_hx=0.;			// hysteresis angle, degrees
			MProp.Theta_hy=0.;			// hysteresis angle, degrees
			MProp.Theta_m=0.;			// magnetization direction, degrees;
			MProp.LamFill=1.;			// lamination fill factor;	
			MProp.LamType=0;			// type of lamination;
			MProp.NStrands=0;
			MProp.WireD=0;
			MProp.BHpoints=0;
			MProp.Bdata=0;
			MProp.Hdata=0;
			q[0]=0;
		}

		if( _strnicmp(q,"<mu_x>",6)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&MProp.mu_x);
		   q[0]=0;
		}
		
		if( _strnicmp(q,"<mu_y>",6)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&MProp.mu_y);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<H_c>",5)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&MProp.H_c);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<H_cAngle>",10)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&MProp.Theta_m);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<J_re>",6)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&MProp.Jr);
		   q[0]=0;
		}	
		
		if( _strnicmp(q,"<J_im>",6)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&MProp.Ji);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<sigma>",7)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&MProp.Cduct);
		   q[0]=0;
		}	
		
		if( _strnicmp(q,"<phi_h>",7)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&MProp.Theta_hn);
		   q[0]=0;
		}	
		

		if( _strnicmp(q,"<phi_hx>",7)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&MProp.Theta_hx);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<phi_hy>",8)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&MProp.Theta_hy);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<d_lam>",7)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&MProp.Lam_d);
		   q[0]=0;
		}	
	
		if( _strnicmp(q,"<LamFill>",8)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&MProp.LamFill);
		   q[0]=0;
		}

		if( _strnicmp(q,"<WireD>",7)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&MProp.WireD);
		   q[0]=0;
		}

		if( _strnicmp(q,"<LamType>",9)==0){
		   v=StripKey(s);
		   sscanf(v,"%i",&MProp.LamType);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<NStrands>",10)==0){
		   v=StripKey(s);
		   sscanf(v,"%i",&MProp.NStrands);
		   q[0]=0;
		}	

		if( _strnicmp(q,"<BHPoints>",10)==0){
			v=StripKey(s);
			sscanf(v,"%i",&MProp.BHpoints);
			if (MProp.BHpoints>0)
			{
				MProp.Hdata=(CComplex *)calloc(MProp.BHpoints,sizeof(CComplex));
				MProp.Bdata=(double *)  calloc(MProp.BHpoints,sizeof(double));
				for(j=0;j<MProp.BHpoints;j++){
					fgets(s,1024,fp);
					sscanf(s,"%lf	%lf",&MProp.Bdata[j],&MProp.Hdata[j].re);
					MProp.Hdata[j].im=0;
				}
			}
			q[0]=0;
		}

		if( _strnicmp(q,"<endblock>",9)==0)
		{
			if (MProp.BHpoints>0)
			{
				if((PrevSoln.GetLength()>0) && (Frequency>0) && (PrevType>0))
				{	
				    // first time through was just to get MuMax from AC curve...
					CComplex *tmpHdata=(CComplex *)calloc(MProp.BHpoints,sizeof(CComplex));
					double *tmpBdata=(double *)calloc(MProp.BHpoints,sizeof(double));
					for(i=0;i<MProp.BHpoints;i++)
					{
						tmpHdata[i]=MProp.Hdata[i];
						tmpBdata[i]=MProp.Bdata[i];
					}
					MProp.GetSlopes(Frequency*2.*PI);
					for(i=0;i<MProp.BHpoints;i++)
					{
						MProp.Hdata[i]=tmpHdata[i];
						MProp.Bdata[i]=tmpBdata[i];
					}
					free(tmpHdata);
					free(tmpBdata);
					free(MProp.slope); 
					MProp.slope=0;  

					// second time through is to get the DC curve
					MProp.GetSlopes(0);		
				}
				else MProp.GetSlopes(Frequency*2.*PI);
			}
			blockproplist[NumBlockProps]=MProp;
			NumBlockProps++;
			MProp.BHpoints=0;
			MProp.Bdata=0;
			MProp.Hdata=0;
			MProp.slope=0;
			q[0]=0;
		}

		// Circuit Properties
		if( _strnicmp(q,"[circuitprops]",15)==0){	
			v=StripKey(s);
			sscanf(v,"%i",&k);
			if(k>0) circproplist=(CCircuit *)calloc(k,sizeof(CCircuit));
			q[0]=0;
		}

		if( _strnicmp(q,"<begincircuit>",14)==0){	
			CProp.dVolts_re=0.;
			CProp.dVolts_im=0.;
			CProp.Amps_re=0.;
			CProp.Amps_im=0.;
			CProp.CircType=0;
			CProp.CircName=(char *)calloc(256,sizeof(char));
			CProp.FluxCirc=-1;
			q[0]=0;
		}

		if( _strnicmp(q,"<circuitname>",13)==0){
			CString thisName;
			ParseString(s,&thisName);
			// Bounded copy into the 256-byte CircName: a plain strcpy would
			// overflow on a >=256-byte name (strcpy into a calloc'd buffer trips
			// no compiler warning, so this slipped the sprintf->snprintf pass).
			strncpy(CProp.CircName,thisName,255);
			CProp.CircName[255]=0;
			q[0]=0;
		}

		if( _strnicmp(q,"<voltgradient_re>",17)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&CProp.dVolts_re);
		   q[0]=0;
		}

		if( _strnicmp(q,"<voltgradient_im>",17)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&CProp.dVolts_im);
		   q[0]=0;
		}

		if( _strnicmp(q,"<totalamps_re>",14)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&CProp.Amps_re);
		   q[0]=0;
		}

		if( _strnicmp(q,"<totalamps_im>",14)==0){
		   v=StripKey(s);
		   sscanf(v,"%lf",&CProp.Amps_im);
		   q[0]=0;
		}
		
		if( _strnicmp(q,"<circuittype>",13)==0){
		   v=StripKey(s);
		   sscanf(v,"%i",&CProp.CircType);
		   q[0]=0;
		}
		
		if( _strnicmp(q,"<endcircuit>",12)==0){
			circproplist[NumCircProps]=CProp;
			NumCircProps++;
			q[0]=0;
		}


		// read in regional attributes
		if(_strnicmp(q,"[numblocklabels]",13)==0){
			int i;
			CString str;
			v=StripKey(s);
			sscanf(v,"%i",&k);
			if (k>0) labellist=(CBlockLabel *)calloc(k, sizeof(CBlockLabel));
			NumBlockLabels=k;
			for(i=0;i<k;i++)
			{
				fgets(s,1024,fp);

//				sscanf(s,"%lf	%lf	%i	%lf	%i	%lf	%i	%i	%i",&blk.x,&blk.y,&blk.BlockType,&blk.MaxArea,
//					&blk.InCircuit,&blk.MagDir,&blk.InGroup,&blk.Turns,&blk.IsExternal);

				//some defaults
				blk.x=0;
				blk.y=0;
				blk.BlockType=0;
				blk.MaxArea=0.;
				blk.MagDir=0.;
				blk.Turns=1;
				blk.InCircuit=0;
				blk.InGroup=0;
				blk.IsExternal=0;
				blk.IsDefault=0;
				blk.MagDirFctn=0;
				str.Empty();

				// scan in data
				v=ParseDbl(s,&blk.x);
				v=ParseDbl(v,&blk.y);
				v=ParseInt(v,&blk.BlockType);				
				v=ParseDbl(v,&blk.MaxArea);
				v=ParseInt(v,&blk.InCircuit);
				v=ParseDbl(v,&blk.MagDir);
				v=ParseInt(v,&blk.InGroup);
				v=ParseInt(v,&blk.Turns);
				v=ParseInt(v,&blk.IsExternal);
				blk.IsDefault  = blk.IsExternal & 2;
				blk.IsExternal = blk.IsExternal & 1;
				v=ParseString(v,&str);
				if (str.GetLength()>0)
				{
					blk.MagDirFctn=(char *)calloc(str.GetLength()+1,sizeof(char));
					strcpy(blk.MagDirFctn,str);
				}
				blk.BlockType--;
				blk.InCircuit--;
				labellist[i]=blk;
			}
			q[0]=0;
		}
	}	

	// need to set these so that valid BH data doesn't get wiped
	// by the destructor of MProp
	MProp.BHpoints=0;
	MProp.Bdata=0;
	MProp.Hdata=0;

	fclose(fp);
			

	// Read the auxiliary file that carries the driver's per-run parameters:
	// the backward-Euler time step, and (planar only) the configuration for
	// flux-constrained circuits.
	//
	// The "no circuits, nothing to do" shortcut that used to sit here has
	// moved past this block: a solid conductor in NO circuit is a legitimate
	// transient problem (pure diffusion), and it still needs [DeltaT].
	if (Frequency==0)
	{
		snprintf(s, sizeof(s),"%s.tmp",PathName);
		if ((fp=femm_fopen(s,"rt"))!=NULL) // may not have a tmp file. If we see one, read it it.
		{
			while (fgets(s,1024,fp)!=NULL)
			{
				sscanf(s,"%s",q);

				// Applies to planar and axisymmetric alike.
				if( _strnicmp(q,"[DeltaT]",8)==0){
					v=StripKey(s);
					sscanf(v,"%lf",&DeltaT);
					if (DeltaT<0) DeltaT=0;
					q[0]=0;
				}

				// The flux/current-constraint machinery below is assembled by
				// prob1big only, so keep it planar-only as before -- otherwise
				// an axisymmetric run would grow constraint rows that nothing
				// ever fills in.
				if ((ProblemType!=0) || (NumCircProps==0)) continue;

				if( _strnicmp(q,"[Circuits]",10)==0){
					v=StripKey(s);
					sscanf(v,"%i",&NumFluxConstraints);
					if (NumFluxConstraints>0) 
						fluxproplist=(int *)calloc(NumFluxConstraints,sizeof(int));
					q[0]=0;

					CString thisName;
					for(k=0;k<NumFluxConstraints;k++)
					{
						// read circuit name into thisName
						fgets(s,1024,fp);
						ParseString(s,&thisName);
						
						// match up thisName to a circuit number
						fluxproplist[k]=-1;
						for(int j=0;j<NumCircProps;j++)
						{
							if (strcmp(circproplist[j].CircName,thisName)==0)
							{
								fluxproplist[k]=j;			 // keep track of which circuit this is
								circproplist[j].FluxCirc=k;  // for that circuit, keep a link to which number of circuit that it maps to
							 	circproplist[j].CircType=2;  // mark as a flux-type circuit   //!!! Need to turn this back on to actually process flux constraints!!!
								break;
							}
						}
					}
				}

				if( _strnicmp(q,"[CircuitMatrix]",15)==0){
					q[0]=0;

					int j;
					CircuitMatrix=(double **)calloc(NumFluxConstraints,sizeof(double*));
					for(k=0;k<NumFluxConstraints;k++)
						CircuitMatrix[k]=(double *)calloc(NumFluxConstraints,sizeof(double));
					for(k=0;k<NumFluxConstraints;k++)
					{
						fgets(s,1024,fp);
						v=s;
						for(j=0;j<NumFluxConstraints;j++)
							v=ParseDbl(v,&CircuitMatrix[k][j]);
					}
				}

				if( _strnicmp(q,"[CircuitFlux]",13)==0){	
					q[0]=0;
					for(k=0;k<NumFluxConstraints;k++)
					{
						fgets(s,1024,fp);
						if (fluxproplist[k]>=0){
							ParseDbl(s,&circproplist[fluxproplist[k]].FluxLinkage);
						}
					}
				}

				if( _strnicmp(q,"[Constraints]",13)==0){	
					v=StripKey(s);
					sscanf(v,"%i",&NumCurrentConstraints);
					q[0]=0;
				}

				if( _strnicmp(q,"[ConstraintMatrix]",18)==0){
					q[0]=0;

					// (the historical version of this block allocated
					// ConstraintMatrix but stored the rows into
					// CircuitMatrix -- clobbering the flux-constraint
					// matrix -- and nothing ever consumed the data)
					ConstraintMatrix=(double **)calloc(NumCurrentConstraints,sizeof(double*));
					for(k=0;k<NumCurrentConstraints;k++)
						ConstraintMatrix[k]=(double *)calloc(NumFluxConstraints,sizeof(double));
					for(k=0;k<NumCurrentConstraints;k++)
					{
						fgets(s,1024,fp);
						v=s;
						for(int j=0;j<NumFluxConstraints;j++)
							v=ParseDbl(v,&ConstraintMatrix[k][j]);
					}
				}
				
				if( _strnicmp(q,"[ConstraintCurrent]",19)==0){	
					q[0]=0;
					if (NumCurrentConstraints>0)
					{
						currentlist=(double *)calloc(NumCurrentConstraints,sizeof(double));
						for(k=0;k<NumCurrentConstraints;k++)
						{
							fgets(s,1024,fp);
							ParseDbl(s,&currentlist[k]);
						}
					}
				}

			}
			fclose(fp);
		}
	}

	// Moved down from above the .tmp block so that a circuit-free transient
	// problem still picks up [DeltaT].
	if (NumCircProps==0) return TRUE;

	// Process circuits for series connections.
	// The program deals with series "circuits" by making a separate
	// circuit property for each block in the series circuit.  Then,
	// each of this larger number of circuits can be processed using
	// the previous approach which considered all circuits to be
	// parallel connected.

	// first, make enough space for all possible circuits;
	CCircuit *temp=(CCircuit *)calloc(NumCircProps+NumBlockLabels,sizeof(CCircuit));
	for(k=0;k<NumCircProps;k++){
		temp[k]=circproplist[k];
		temp[k].OrigCirc=-1;
	}
	free(circproplist);
	circproplist=temp;
	NumCircPropsOrig=NumCircProps;

	// now, go through the block label list and make a new "circuit" 
	// for every block label that is an element of a "series" circuit.
	CCircuit ncirc;
	for(k=0;k<NumBlockLabels;k++)
	if(labellist[k].InCircuit>=0){
		ic=labellist[k].InCircuit;

		if(circproplist[ic].CircType==1)
		{
			ncirc=circproplist[ic];
			ncirc.OrigCirc=ic;
			ncirc.Amps_im*=labellist[k].Turns;
			ncirc.Amps_re*=labellist[k].Turns;
			circproplist[NumCircProps]=ncirc;
			labellist[k].InCircuit=NumCircProps;
			NumCircProps++;
		}

		if(circproplist[ic].CircType==2)  // case where we are trying to find the current that produces the specified flux.
		{
			ncirc=circproplist[ic];
			ncirc.OrigCirc=ic;
			ncirc.Amps_re=labellist[k].Turns;  // total A*turn corresponding to a real 1A current
			ncirc.Amps_im=0;
			circproplist[NumCircProps]=ncirc;
			labellist[k].InCircuit=NumCircProps;
			NumCircProps++;
		}
	}

	// now, all "circuits" look like parallel circuits, so 
	for(k=0;k<NumCircProps;k++) 
		if(circproplist[k].CircType==1) circproplist[k].CircType=0;
    
	return TRUE;
}

// Size the linear system: the nodal unknowns, plus the flux- and
// current-constraint rows, plus -- in a transient analysis -- one row per
// circuit carrying the axial voltage gradient of any solid conductor in it
// (the static analogue of prob2big's "Case 2"). The voltage rows go LAST so
// that the flux/current-constraint indexing already baked into prob1big is
// untouched.
//
// Allocate one voltage row per circuit unconditionally: which circuits
// actually need one is only decided inside Static2D, long after the matrix
// has been Created, and the unused rows get a pinned diagonal there. Call
// this only after the series-circuit expansion, which is what fixes
// NumCircProps.
void CFemmeDocCore::SetNumNodesPlus()
{
	NumVoltageUnknowns = (DeltaT>0) ? NumCircProps : 0;
	NumNodesPlus = NumNodes + NumFluxConstraints + NumCurrentConstraints
	                        + NumVoltageUnknowns;
}

// Build an air gap element's ring mapping from its raw ring node lists.
//
// Everything angle-dependent is derived here; everything persistent is
// angle-invariant. Each of the n ring nodes is replicated once per
// periodic copy of the meshed sector, rotated by the copy's offset plus
// the ring's CURRENT angle (age.InnerAngle/OuterAngle), sorted by angular
// position, and paired off into the quad points the assembly consumes.
// Antiperiodic gaps alternate the sign carried per copy; the endpoint sign
// flips in prob1big's AGE assembly are downstream of this and unchanged.
//
// The sort is stable with strictly-increasing keys in practice (distinct
// ring positions), matching the original bubble sort's ordering exactly --
// which matters, because the transient mesh-reuse path must reproduce the
// fresh-mesh pairing bit for bit at equal angles.
void CFemmeDocCore::BuildAgeRing(CAirGapElement &age,
                                 const double *ix, const double *iy,
                                 const double *ox, const double *oy,
                                 double cx, double cy)
{
	int n = age.totalArcElements;
	if (n < 1) return;

	double dtta = age.totalArcLength / n;
	int n0 = (int)(360.0 / dtta + 0.5);
	int n1 = (int)(360.0 / age.totalArcLength + 0.5);

	struct RingPt { int node; double w; double sign; };
	std::vector<RingPt> innerRing(n0), outerRing(n0);
	int kk = 0;
	for (int jj = 0; jj < n1; jj++) {
		double dL = 1.0;
		if (age.BdryFormat == 1 && (jj % 2 != 0)) dL = -1.0;
		double iRotRad = (jj * age.totalArcLength + age.InnerAngle) * PI / 180.0;
		double oRotRad = (jj * age.totalArcLength + age.OuterAngle) * PI / 180.0;
		double cosI = cos(iRotRad), sinI = sin(iRotRad);
		double cosO = cos(oRotRad), sinO = sin(oRotRad);
		for (int ii = 0; ii < n; ii++) {
			double dx = ix[ii] - cx;
			double dy = iy[ii] - cy;
			double rx = cosI * dx - sinI * dy, ry = sinI * dx + cosI * dy;
			double ang = atan2(ry, rx) * 180.0 / PI;
			if (ang < 0) ang += 360.0;
			innerRing[kk].node = age.ringInner[ii];
			innerRing[kk].w = ang / dtta;
			innerRing[kk].sign = dL;

			dx = ox[ii] - cx;
			dy = oy[ii] - cy;
			rx = cosO * dx - sinO * dy; ry = sinO * dx + cosO * dy;
			ang = atan2(ry, rx) * 180.0 / PI;
			if (ang < 0) ang += 360.0;
			outerRing[kk].node = age.ringOuter[ii];
			outerRing[kk].w = ang / dtta;
			outerRing[kk].sign = dL;
			kk++;
		}
	}

	auto byPos = [](const RingPt &a, const RingPt &b){ return a.w < b.w; };
	std::stable_sort(innerRing.begin(), innerRing.end(), byPos);
	std::stable_sort(outerRing.begin(), outerRing.end(), byPos);

	age.InnerShift = innerRing[0].w;
	age.OuterShift = outerRing[0].w;

	if (age.node == NULL)
		age.node = (CQuadPoint *)calloc(n + 1, sizeof(CQuadPoint));
	for (int ii = 0; ii <= n; ii++) {
		int p1 = ii; if (p1 >= n0) p1 = 0;
		int p0 = p1 - 1; if (p0 < 0) p0 = n0 + p0;
		age.node[ii].n0 = innerRing[p0].node;
		age.node[ii].w0 = innerRing[p0].sign;
		age.node[ii].n1 = innerRing[p1].node;
		age.node[ii].w1 = innerRing[p1].sign;
		age.node[ii].n2 = outerRing[p0].node;
		age.node[ii].w2 = outerRing[p0].sign;
		age.node[ii].n3 = outerRing[p1].node;
		age.node[ii].w3 = outerRing[p1].sign;
	}
}

int CFemmeDocCore::LoadMesh()
{
	int i,j,k,q,n0,n1;
	char infile[4096];
	FILE *fp;
	char s[1024];
	double c[]={2.54,0.1,1.,100.,0.00254,1.e-04};

	//if there's a "previous solution" specified, 
	//and if this is an incremental or frozen permeability problem,
	//slurp of the mesh out of that file.
	//
	// A transient step (DeltaT>0) takes this path too. The previous hazard --
	// LoadPrev restoring the PREVIOUS step's air-gap pairing, coupling the
	// rotor at the wrong angle -- is resolved: the .ans now carries each
	// AGE's raw ring node lists in a trailing [AgeRings] section, and
	// LoadPrev re-pairs every gap at the angles the CURRENT .fem specifies.
	// If the lists are absent (an old .ans), LoadPrev returns FALSE and the
	// caller remeshes, which is the previous behavior.
	if ((PrevSoln.GetLength()>0) && ((PrevType>0)||(DeltaT>0)))
	{
		// clear out mesh files
		snprintf(infile, sizeof(infile),"%s.ele",PathName);	DeleteFile(infile);
		snprintf(infile, sizeof(infile),"%s.node",PathName);	DeleteFile(infile);
		snprintf(infile, sizeof(infile),"%s.pbc",PathName);	DeleteFile(infile);
		snprintf(infile, sizeof(infile),"%s.poly",PathName);	DeleteFile(infile);
		snprintf(infile, sizeof(infile),"%s.edge",PathName);	DeleteFile(infile);

		if (LoadPrev()==TRUE)
		{
			SetNumNodesPlus();
			return TRUE;
		}
		else return FALSE;
	}

	//read meshnodes;
	snprintf(infile, sizeof(infile),"%s.node",PathName);
	if((fp=femm_fopen(infile,"rt"))==NULL){
		return FALSE;
	}
	fgets(s,1024,fp);
	sscanf(s,"%i",&k); NumNodes=k;

	meshnode=(CNode *)calloc(k,sizeof(CNode));
	CNode node;
	for(i=0;i<k;i++){
		fscanf(fp,"%i",&j);
		fscanf(fp,"%lf",&node.x);
		fscanf(fp,"%lf",&node.y);
		fscanf(fp,"%i",&j);
		if(j>1) j=j-2; else j=-1;
		node.bc=j;

		// convert all lengths to centimeters (better conditioning this way...)
		node.x*=c[LengthUnits];
		node.y*=c[LengthUnits];
		
		meshnode[i]=node;
	}
	fclose(fp);
	MsgBox("Succefully read %s.node",PathName);

	//read in periodic boundary conditions;
	snprintf(infile, sizeof(infile),"%s.pbc",PathName);
	if((fp=femm_fopen(infile,"rt"))!=NULL){
	
		fgets(s,1024,fp);
		sscanf(s,"%i",&k); NumPBCs=k;
		
		if (k!=0) pbclist=(CCommonPoint *)calloc(k,sizeof(CCommonPoint));
		CCommonPoint pbc;
		for(i=0;i<k;i++){
			fgets(s,1024,fp);
			sscanf(s,"%i %i %i %i",&j,&pbc.x,&pbc.y,&pbc.t);
			pbclist[i]=pbc;
		}
		
		// read in air gap element info
		fgets(s,1024,fp); sscanf(s,"%i",&NumAGEs);
		if (NumAGEs!=0) agelist=(CAirGapElement *)calloc(NumAGEs,sizeof(CAirGapElement));
		CAirGapElement age;
		for(i=0;i<NumAGEs;i++){
			fgets(age.BdryName,80,fp);
			stripAgeName(age.BdryName);   // the .ans stores the name as "AGE" -- drop quotes/newline
			fgets(s,1024,fp);
			sscanf(s,"%i %lf %lf %lf %lf %lf %lf %lf %i %lf %lf",
				&age.BdryFormat,&age.InnerAngle,&age.OuterAngle,
				&age.ri,&age.ro,&age.totalArcLength,
				&age.agc.re,&age.agc.im,&age.totalArcElements,&age.InnerShift,&age.OuterShift);
			age.node=(CQuadPoint *)calloc(age.totalArcElements+1,sizeof(CQuadPoint));
			for(k=0;k<=age.totalArcElements;k++){
				fgets(s,1024,fp);
				sscanf(s,"%i %lf %i %lf %i %lf %i %lf",
					&age.node[k].n0, &age.node[k].w0,
					&age.node[k].n1, &age.node[k].w1,
					&age.node[k].n2, &age.node[k].w2,
					&age.node[k].n3, &age.node[k].w3);
			}
			agelist[i]=age;
		}

		fclose(fp);
	}
	else{
		NumPBCs=0; 
		NumAGEs=0;
	}

MsgBox("Succefully read %s.pbc",PathName);

	// read in elements;
	snprintf(infile, sizeof(infile),"%s.ele",PathName);
	if((fp=femm_fopen(infile,"rt"))==NULL){
		return FALSE;
	}
	fgets(s,1024,fp);
	sscanf(s,"%i",&k); NumEls=k;

	meshele=(CElement *)calloc(k,sizeof(CElement));
	CElement elm;

	int defaultLabel;
	for(i=0,defaultLabel=-1;i<NumBlockLabels;i++)
		if (labellist[i].IsDefault) defaultLabel=i;

	for(i=0;i<k;i++){
		fscanf(fp,"%i",&j);
		fscanf(fp,"%i",&elm.p[0]);
		fscanf(fp,"%i",&elm.p[1]);
		fscanf(fp,"%i",&elm.p[2]);
		fscanf(fp,"%i",&elm.lbl);
		elm.lbl--;
		if(elm.lbl<0) elm.lbl=defaultLabel;
		if(elm.lbl<0){
			CString msg;
			msg ="Material properties have not been defined for\n"; 
			msg+="all regions. Press the \"Run Mesh Generator\"\n";
			msg+="button to highlight the problem regions.";
			MsgBox(msg);
			fclose(fp);

			snprintf(infile, sizeof(infile),"%s.ele",PathName);	DeleteFile(infile);
			snprintf(infile, sizeof(infile),"%s.node",PathName);	DeleteFile(infile);
			snprintf(infile, sizeof(infile),"%s.pbc",PathName);	DeleteFile(infile);
			snprintf(infile, sizeof(infile),"%s.poly",PathName);	DeleteFile(infile);
			snprintf(infile, sizeof(infile),"%s.edge",PathName);	DeleteFile(infile);
			exit(1);
		}
		// look up block type out of the list of block labels
		elm.blk=labellist[elm.lbl].BlockType;

		meshele[i]=elm;
	}
	fclose(fp);
MsgBox("Succefully read %s.ele",PathName);
	// initialize edge bc's and element permeabilities;
	for(i=0;i<NumEls;i++)
		for(j=0;j<3;j++)
		{
			meshele[i].e[j] = -1;		
			meshele[i].mu1  = -1.;
			meshele[i].mu2  = -1.;
		}

	// read in edges to which boundary conditions are applied;

	// first, do a little bookkeeping so that element
	// associated with a give edge can be identified fast
	int *nmbr;
	int **mbr;

	nmbr=(int *)calloc(NumNodes,sizeof(int));

		// Make a list of how many elements that tells how
		// many elements to which each node belongs.
		for(i=0;i<NumEls;i++)
			for(j=0;j<3;j++)
				nmbr[meshele[i].p[j]]++;

		// mete out some memory to build a list of the
		// connectivity...
		mbr=(int **)calloc(NumNodes,sizeof(int *));
		for(i=0;i<NumNodes;i++){
			mbr[i]=(int *)calloc(nmbr[i],sizeof(int));
			nmbr[i]=0;
		}

		// fill up the connectivity information;
		for(i=0;i<NumEls;i++)
			for(j=0;j<3;j++)
			{
				k=meshele[i].p[j];
				mbr[k][nmbr[k]]=i;
				nmbr[k]++;
			}		

	snprintf(infile, sizeof(infile),"%s.edge",PathName);
	if((fp=femm_fopen(infile,"rt"))==NULL)
	{
		return FALSE;
	}
	fscanf(fp,"%i",&k);	// read in number of lines

	fscanf(fp,"%i",&j);	// read in boundarymarker flag;
	for(i=0;i<k;i++)
	{
		fscanf(fp,"%i",&j);
		fscanf(fp,"%i",&n0);
		fscanf(fp,"%i",&n1);
		fscanf(fp,"%i",&j);
		
		if(j<0){
			j = -(j+2);
			// search through elements to find one containing the line;
			// set corresponding edge equal to the bc number.
			for(q=0;q<nmbr[n0];q++)
			{
				elm=meshele[mbr[n0][q]];

				if ((elm.p[0] == n0) && (elm.p[1] == n1)) elm.e[0]=j;
				if ((elm.p[0] == n1) && (elm.p[1] == n0)) elm.e[0]=j;

				if ((elm.p[1] == n0) && (elm.p[2] == n1)) elm.e[1]=j;
				if ((elm.p[1] == n1) && (elm.p[2] == n0)) elm.e[1]=j;

				if ((elm.p[2] == n0) && (elm.p[0] == n1)) elm.e[2]=j;
				if ((elm.p[2] == n1) && (elm.p[0] == n0)) elm.e[2]=j;

				meshele[mbr[n0][q]]=elm;
			}
		}
		
	}
	fclose(fp);

	// free up the connectivity information
	free(nmbr);
	for(i=0;i<NumNodes;i++) free(mbr[i]);
	free(mbr);

	// clear out temporary files
	snprintf(infile, sizeof(infile),"%s.ele",PathName);	DeleteFile(infile);
	snprintf(infile, sizeof(infile),"%s.node",PathName);	DeleteFile(infile);
	snprintf(infile, sizeof(infile),"%s.pbc",PathName);	DeleteFile(infile);
	snprintf(infile, sizeof(infile),"%s.poly",PathName);	DeleteFile(infile);

	SetNumNodesPlus();
	return TRUE;
}

void CFemmeDocCore::GetFillFactor(int lbl)
{
	// Get the fill factor associated with a stranded and
	// current-carrying region.  For AC problems, also compute
	// the apparent conductivity and permeability for use in
	// post-processing the voltage.
	
	CMaterialProp* bp= &blockproplist[labellist[lbl].BlockType];
	CBlockLabel* bl= &labellist[lbl];
	double atot,awire,d,o,fill,dd,W,R,c1=0,c2=0;
	int i,wiretype;
	CComplex ufd,ofd;

	if ((abs(bl->Turns)>1) || (blockproplist[labellist[lbl].BlockType].LamType>2)) 
		bl->bIsWound=TRUE;
	else 
		bl->bIsWound=FALSE;

	if ((Frequency==0) || (blockproplist[labellist[lbl].BlockType].LamType<3))
	{
		bl->ProximityMu=1.;
		return;
	}

	// compute total area of associated block
	for(i=0,atot=0;i<NumEls;i++)
		  if(meshele[i].lbl==lbl) atot+=ElmArea(i);

	if (atot==0) return;

	// "non-physical" case where the wire has a zero conductivity
	if (bp->Cduct==0)
	{
		bl->ProximityMu=1;
		return;
	}

	wiretype=bp->LamType-3;
	// wiretype = 0 for magnet wire
	// wiretype = 1 for stranded but non-litz wire
	// wiretype = 2 for litz wire
	// wiretype = 3 for rectangular wire
	// wiretype = 4 for 10% CCA
	// wiretype = 5 for 15% CCA

	if(wiretype==3) // rectangular wire
	{
		W=2.*PI*Frequency;
		d=bp->WireD*0.001;
		fill=fabs(d*d*((double) bl->Turns)/atot);
		dd=d/sqrt(fill);			// foil pitch
		fill=d/dd;					// fill for purposes of equivalent foil analysis
		o=bp->Cduct*(d/dd)*1.e6;	// effective foil conductivity in S/m

		// effective permeability for the equivalent foil
		ufd=muo*tanh(sqrt(I*W*o*muo)*d/2.)/(sqrt(I*W*o*muo)*d/2.);
		bl->ProximityMu=(fill*ufd+(1.-fill)*muo)/muo;
		return;
	}

	// procedure for round wires;
	switch (wiretype)
	{
		// wiretype = 1 for stranded but non-litz wire
		case 1:
			R=bp->WireD*0.0005*sqrt((double) bp->NStrands);
			awire=PI*R*R*((double) bl->Turns);
		break;

		// magnet wire, litz wire, 10% CCA, 15%CCA
		default:
			R=bp->WireD*0.0005;
			awire=PI*R*R*((double) bp->NStrands)*((double) bl->Turns);
		break;
	}
	fill=fabs(awire/atot);

	// preliminary definitions
	o=bp->Cduct*1.e6;						// conductivity in S/m
	W=2.*PI*Frequency*o*muo*R*R/2.;			// non-dimensionalized frequency

	// fit for frequency-dependent permeability...
	switch (wiretype)
	{
		case 0: // magnet wire
		case 1: // plain stranded
		case 2: // litz
			c1=0.7756067409818643 + fill*(0.6873854335408803 + fill*(0.06841584481674128 -0.07143732702512284*fill));
			c2=1.5*fill/c1;
			break;

		case 4: // 10% CCA
			c1=0.7270741505617485 + 0.8902950067721367*fill + 0.11894736885885195*fill*fill - 0.12247276254503957*fill*fill*fill;
			c2=0.006784920229549677 + 1.8942880489198526*fill - 1.3631438759519217*fill*fill + 0.504431701685587*fill*fill*fill;
			break;

		case 5: // 15% CCA
			c1=0.7486913529860821 + 0.9042845510838825*fill + 0.1361040321433224*fill*fill - 0.10652380745682069*fill*fill*fill;
			c2=0.006790468527313965 + 1.8945509985370095*fill - 1.3643501010185972*fill*fill + 0.5036765577982594*fill*fill*fill;
			break;
	}

	ufd=c2*(tanh(sqrt(c1*I*W))/sqrt(c1*I*W))+(1.-c2); // relative frequency-dependent permeability
	bl->ProximityMu=ufd;

	
}
/*
void CFemmeDocCore::GetFillFactor(int lbl)
{
	// Get the fill factor associated with a stranded and
	// current-carrying region.  For AC problems, also compute
	// the apparent conductivity and permeability for use in
	// post-processing the voltage.
	
	CMaterialProp* bp= &blockproplist[labellist[lbl].BlockType];
	CBlockLabel* bl= &labellist[lbl];
	double atot,awire,r,FillFactor;
	int i,wiretype;
	CComplex ufd;
	double W=2.*PI*Frequency;

	if ((Frequency==0) || (blockproplist[labellist[lbl].BlockType].LamType<3))
	{
		bl->ProximityMu=0;
		return;
	}

	wiretype=bp->LamType-3;
	// wiretype = 0 for magnet wire
	// wiretype = 1 for stranded but non-litz wire
	// wiretype = 2 for litz wire
	// wiretype = 3 for rectangular wire
	r=bp->WireD*0.0005;
	
	for(i=0,atot=0;i<NumEls;i++)
		  if(meshele[i].lbl==lbl) atot+=ElmArea(i);

	awire=PI*r*r;
	if (wiretype==3) awire*=(4./PI); // if rectangular wire;
	awire*=((double) bp->NStrands);
	awire*=((double) bl->Turns);
	
	if (atot==0) return;
	FillFactor=fabs(awire/atot);

	double w,d,h,o,fill,dd;
	
	// if stranded but non-litz, use an effective wire radius that
	// gives the same cross-section as total stranded area
	if (wiretype==1) r*=sqrt((double) bp->NStrands);

	if (wiretype!=3){
		d=r*sqrt(3.);
		h=PI/sqrt(3.)*r;
		w=r*sqrt(PI/(2.*sqrt(3.)*FillFactor));
		dd=sqrt(3.)*w;
	}
	else{
		d=2.*r;
		h=2.*r;
		w=r/sqrt(FillFactor);
		dd=2.*w;
	}
	o=bp->Cduct*(h/w)*5.e5; // conductivity in S/m
	fill=d/dd; //fill for purposes of equivalent foil analysis

	// At this point, sanity-check the fill factor;
	if (fill>1)
	{
		CString mymsg;
		mymsg.Format("Block label at (%g,%g) has a fill factor",bl->x,bl->y);
		mymsg +=     "greater than the theoretical maximum.  Couldn't solve the problem.";
		MsgBox(mymsg);
		exit(5);
	}

	// effective permeability for the equivalent foil.  Note that this is
	// the same equation as effective permeability of a lamination...
	if (o!=0) ufd=muo*tanh(sqrt(I*W*o*muo)*d/2.)/(sqrt(I*W*o*muo)*d/2.);
	else ufd=0;
	
	// relative complex permeability
	bl->ProximityMu=(fill*ufd+(1.-fill)*muo)/muo;
}
*/

double CFemmeDocCore::ElmArea(int i)
{
	// returns element cross-section area in meter^2
	int j,n[3];
	double b0,b1,c0,c1;

	for(j=0;j<3;j++) n[j]=meshele[i].p[j];

	b0=meshnode[n[1]].y - meshnode[n[2]].y;
	b1=meshnode[n[2]].y - meshnode[n[0]].y;
	c0=meshnode[n[2]].x - meshnode[n[1]].x;
	c1=meshnode[n[0]].x - meshnode[n[2]].x;
	return 0.0001*(b0*c1-b1*c0)/2.;

}
