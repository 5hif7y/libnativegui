/***************************************************************************\
graph library
Copyright (C) 1992-2003  Alexander Wagner

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

e-mail: Alexander.Wagner@ndsu.nodak.edu
\***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "mypow.h"
#include "mygraph.h"
#include "contour.h"
#include "fftn.h"
#include "analyse.h"

/* getgraph3d() (and getcenter()) are not part of this library any more: the
 * calls were commented out when the sources were restructured, which left
 * the functions below reading uninitialised pointers and sizes. Until they
 * are wired to a data source again they report it and return "no data"
 * (0 / NULL / zeroed outputs) instead of crashing. GetStructFactFromData,
 * my_calc_center, get_minmax and the S* helpers take their data as
 * arguments and work. */
static void no_graph_data(const char* fn)
{
  fprintf(stderr, "analyse: %s: no data source (getgraph3d is not available)\n", fn);
}

void savegrdata(grstr graph,char *name,int no,int rough)
{
  double *gp=NULL;
  FILE *gfile;
  int x,y,xmax=0,ymax=0;
  /*
  getgraph3d(graph,Scalar,&gp,&xmax,&ymax,no,rough,1,1);
  */
  if (!gp) { no_graph_data("savegrdata"); return; }
  gfile=fopen(name,"w");
  for (x=0; x<xmax; x++)
    {
      for (y=0; y<ymax; y++)
	fprintf(gfile,"%e  ",gp[x*ymax+y]);
      fprintf(gfile,"\n");
    }
  fclose(gfile);
  free(gp);
}

double calc_vol(grstr graph, int nogr, double cut)
{
  int i,x_size=0,y_size=0;
  double *g=NULL;
  register  double gt,vol=0,n;
  /*
  getgraph3d(graph,Scalar,&g,&x_size,&y_size,nogr,1,1,1);
  */
  if (!g) { no_graph_data("calc_vol"); return 0; }
  n=0;
  for (i=0; i<x_size*y_size; i++)
    {
      gt=g[i];
      if (gt>cut) vol+=gt;
    }
  return vol;
}

int my_calc_center(double *g,double *center,int x_size,int y_size)
{
  int x,y;
  register  double n=0,gt;

  center[0]=center[1]=0;
  for (x=0; x<x_size; x++)
    for (y=0; y<y_size; y++)
      {
	gt=g[x*y_size+y];
	if (gt>0)
	  {
	    center[0]+=gt*x;
	    center[1]+=gt*y;
	    n+=gt;
	  }
      }
  if (n!=0)
    { 
      center[0]/=n;
      center[1]/=n;
      return 0;
    }
  else return 1;
}



int calc_ABtet(grstr graph,double *a, double *b, double *tet)
{
  int x,y,i,x_size,y_size;
  register  double n=0,gt;
  double center[2],T[2][2] = {{0.,0.},{0.,0.}},l[2],tmp,*g=NULL;
  /*
  getgraph3d(graph,Scalar,&g,&x_size,&y_size,0,1,1,1);
  */
  x_size=y_size=0;
  if (!g) { no_graph_data("calc_ABtet"); *a=0;*b=0;*tet=0; return 0; }
  if (!my_calc_center(g,center,x_size,y_size))
    {
      for (x=0; x<x_size; x++)
	for (y=0; y<y_size; y++)
	  for (i=0; i<9; i++)
	    {
	      gt=g[x*y_size+y];
	      if (gt>0)
		{
		  T[0][0]+=gt*(x-center[0])*(x-center[0]);
		  T[0][1]+=gt*(x-center[0])*(y-center[1]);
		  T[1][0]+=gt*(x-center[0])*(y-center[1]);
		  T[1][1]+=gt*(y-center[1])*(y-center[1]);
		  n+=gt;
		}
	    }
      T[0][0]/=n;
      T[0][1]/=n;
      T[1][0]/=n;
      T[1][1]/=n;
#ifdef DEBUG
      printf("    / %e \\\n"
	     "c = |              |\n"
	     "    \\ %e /\n\n",center[0],center[1]);
      printf("    / %e  %e \\\n"
	     "T = |                            |\n"
	     "    \\ %e  %e /\n\n",T[0][0],T[0][1],T[1][0],T[1][1]);
#endif
      l[0]=(T[0][0]+T[1][1])/2
	-sqrt(pow2((T[0][0]-T[1][1])/2)
	      +T[0][1]*T[1][0]);
      l[1]=(T[0][0]+T[1][1])/2
	+sqrt(pow2((T[0][0]-T[1][1])/2)
	      +T[0][1]*T[1][0]);
      if ((l[0]-l[1])*(T[0][0]-T[1][1])<0) 
	{ tmp=l[0];l[0]=l[1];l[1]=tmp;}
#ifdef DEBUG
      double R[2][2];
      R[0][0]=-1/sqrt(pow2((T[1][1]-l[0]))+pow2(T[0][1]))*(T[1][1]-l[0]);
      R[0][1]=1/sqrt(pow2((T[1][1]-l[0]))+pow2(T[0][1]))*T[0][1];
      R[1][0]=-1/sqrt(pow2((T[0][0]-l[1]))+pow2(T[0][1]))*T[0][1];
      R[1][1]=1/sqrt(pow2((T[0][0]-l[1]))+pow2(T[0][1]))*(T[0][0]-l[1]);
      printf("    / %e  %e \\\n"
	     "R = |                            |\n"
	 "    \\ %e  %e /\n\n",R[0][0],R[0][1],R[1][0],R[1][1]);
#endif

      if (T[0][1]==0) *tet=0;
      else *tet=asin(T[0][1]/sqrt(pow2(T[1][1]-l[0])+pow2(T[0][1])));
      l[0]=sqrt(l[0]);
      l[1]=sqrt(l[1]);
      if (l[0]>l[1]) {*a=l[0];*b=l[1];}
      else {*a=l[1];*b=l[0];}
      return 1;
    }
  else 
    {  /* No Bubble */
      *a=0;*b=0;*tet=0;
      return 0;
    }
}

double calc_D(grstr graph,double *tet)
{
  double a,b;

  if (calc_ABtet(graph,&a,&b,tet)) return (a-b)/(a+b);
  else return 0;
}


int checkcontours(grstr graph,double cut)
{
  int l,*length,nox=0,noy=0,boundary=0;
  double *cont,*gp=NULL;

  /*
  getgraph3d(graph,Scalar,&gp,&nox,&noy,0,1,1,1);
  */
  if (!gp) { no_graph_data("checkcontours"); return 0; }
  contour(gp,nox,noy,
	  cut,&cont,&length,0,NULL,&boundary);
  l=length[0];
  free(cont);
  free(length);
  free(gp);
  return l;
}

void data_from_contour(double *cont, int anz,
		       double *a, double *b, double *tet)
{ 
  int i,ai,bi;
  double d,center[2];

  /*
  getcenter(center);
  */
  (void)i;(void)ai;(void)bi;(void)d;(void)center;(void)cont;(void)anz;
  no_graph_data("data_from_contour (getcenter)");
  *a=0;*b=0;*tet=0;
  return;
  ai=0;
  *a=*b=sqrt(pow2(cont[0]-center[0])+pow2(cont[1]-center[1]));
  for (i=0;i<anz;i++)
    {
      d=sqrt(pow2(cont[i*2]-center[0])+pow2(cont[i*2+1]-center[1]));
      if (*a<d) {ai=i; *a=d;}
      if (*b>d) {bi=i; *b=d;}
    }
  *tet=fmod(atan2(cont[ai*2+1]-center[1],cont[ai*2]-center[0]),90);
}

double pressure_difference(grstr graph)
{
 double center[2],*gp=NULL,res;
 int ci[2],nox=0,noy=0;
 /*
 getgraph3d(graph,Scalar,&gp,&nox,&noy,8,1,1,1);
 getcenter(&(center[0]));
 */
 if (!gp) { no_graph_data("pressure_difference"); return 0; }
 center[0]=center[1]=0;
 ci[0]=center[0];ci[1]=center[1];
 res=gp[ci[0]*noy+ci[1]]
    -gp[3*noy+3];
 free(gp);

 return res;
}

double get_min(grstr graph,int nograph)
{
 int i,nox=0,noy=0;
 double min,*gp=NULL;
 /*
 getgraph3d(graph,Scalar,&gp,&nox,&noy,nograph,1,1,1);
 */
 if (!gp) { no_graph_data("get_min"); return 0; }
 min=gp[0];
 for (i=1;i<nox*noy;i++)
   if (min>gp[i]) min=gp[i];
 free(gp); 
 return min;
}

double get_max(grstr graph,int nograph)
{
 int i,nox=0,noy=0;
 double max,*gp=NULL;
 /*
 getgraph3d(graph,Scalar,&gp,&nox,&noy,nograph,1,1,1);
 */
 if (!gp) { no_graph_data("get_max"); return 0; }
 max=gp[0];
 for (i=1;i<nox*noy;i++)
   if (max<gp[i]) max=gp[i];
 free(gp);
 return max;
}

void get_minmax(double *gp,int anz,double *min, double *max)
{
 int i;
 
 *min=*max=gp[0];
 for (i=1;i<anz;i++){
   if (*max<gp[i]) *max=gp[i];
   if (*min>gp[i]) *min=gp[i];
 }
}

double get_vecmax(grstr graph,int nograph)
{
 int i,nox=0,noy=0;
 double max,*gp=NULL;
 register double t;
 /*
 getgraph3d(graph,Vector,&gp,&nox,&noy,nograph,1,1,1);
 */
 if (!gp) { no_graph_data("get_vecmax"); return 0; }
 max=0;
 for (i=0;i<2*nox*noy;i+=2){
   t=pow2(gp[i])+pow2(gp[i+1]);
   if (max<t) max=t;
 }
 free(gp);
 return sqrt(max);
}

double get_vecav(grstr graph,int nograph)
{
 int i,nox=0,noy=0;
 double av,*gp=NULL;
 /*
 getgraph3d(graph,Vector,&gp,&nox,&noy,nograph,1,1,1);
 */
 if (!gp) { no_graph_data("get_vecav"); return 0; }
 av=0;
 for (i=0;i<2*nox*noy;i+=2){
   av+=sqrt(pow2(gp[i])+pow2(gp[i+1]));
 }
 free(gp);
 return av/nox/noy;
}

double get_av(grstr graph,int nograph)
{
 int i,nox=0,noy=0;
 double sum,*gp=NULL;
 /*
 getgraph3d(graph,Scalar,&gp,&nox,&noy,nograph,1,1,1);
 */
 if (!gp) { no_graph_data("get_av2"); return 0; }
 sum=0;
 for (i=0;i<nox*noy;i++)
   sum+=gp[i];
 free(gp);
 return sum/nox/noy;
}

double get_av2(grstr graph,int nograph)
{
 int i,nox=0,noy=0;
 double sum,*gp=NULL;
 /*
 getgraph3d(graph,Scalar,&gp,&nox,&noy,nograph,1,1,1);
 */
 if (!gp) { no_graph_data("get_av"); return 0; }
 sum=0;
 for (i=0;i<nox*noy;i++)
   sum+=pow2(gp[i]);
 free(gp);
 return sum/nox/noy;
}

double get_areafrac(grstr graph,int nograph,double cut)
{
  long sum;
  int i,nox=0,noy=0;
  double *gp=NULL;
  /*
  getgraph3d(graph,Scalar,&gp,&nox,&noy,nograph,1,1,1);
  */
  if (!gp) { no_graph_data("get_areafrac"); return 0; }
  sum=0;
  for (i=0;i<nox*noy;i++)
    if (gp[i]>cut) sum++;
  free(gp);
  return 1.*sum/nox/noy;
}


/* Data analysis part. Calculates different k-modes.  */

double S0(double data[],int nn[])
{
  int i;
  double Ssum=0;

  for  (i=0; i<nn[0]*nn[1];i++)
    Ssum+=data[i]; 
  return Ssum;
}

double S1(double data[],int nn[])
{
  int x,y;
  double Ssum=0;
 
  for  (x=0; x<nn[0]; x++)
    for (y=0; y<nn[1]; y++)
      Ssum+= sqrt(pow2(x-nn[0]/2)+pow2(y-nn[1]/2))*data[x*nn[1]+y]; 
  return Ssum;
}


void Sk(double data[],int nn[],double kav,double vec[2])
{
  int x,y;
  double kk;

  vec[0]=vec[1]=0;
  for  (x=0; x<nn[0]; x++)
    for (y=0; y<nn[1]; y++)
      {
	kk=sqrt(pow2(x-nn[0]/2)+pow2(y-nn[1]/2));
	if (kk!=0){
	  vec[0]+=(1-kav/kk)*fabs(x-nn[0]/2) *data[x*nn[1]+y];
	  vec[1]+=(1-kav/kk)*fabs(y-nn[1]/2) *data[x*nn[1]+y];
	}
      }
}

void Sk2(double data[],int nn[],double kav,double mat[3])
{
  int x,y;
  double kk;

  mat[0]=mat[1]=mat[2]=mat[3]=0;
  for  (x=0; x<nn[0]; x++)
    for (y=0; y<nn[1]; y++)
      {
	kk=sqrt(pow2(x-nn[0]/2)+pow2(y-nn[1]/2));
	if (kk!=0){
	  mat[0]+=pow2((1-kav/kk)   *(x-nn[0]/2)      )*data[x*nn[1]+y];
	  mat[1]+=pow2((1-kav/kk))*(x-nn[0]/2)*(y-nn[1]/2)   *data[x*nn[1]+y];
	  mat[2]+=pow2((1-kav/kk)               *(y-nn[1]/2))*data[x*nn[1]+y];
	}
      }
}

void Sd2(double data[],int nn[],double mat[3])
{
  int x,y;

  mat[0]=mat[1]=mat[2]=0;


  for  (x=0;x<nn[0]; x++)
    for (y=0;y<nn[1]; y++){
      mat[0]+=pow2(2./nn[0]*(x-nn[0]/2))*data[x*nn[1]+y];
      mat[1]+=    2./nn[0]*(x-nn[0]/2)*
	          2./nn[1]*(y-nn[1]/2)   *data[x*nn[1]+y];
      mat[2]+=pow2(2./nn[1]*(y-nn[1]/2))*data[x*nn[1]+y];
    }
}
void Sd2sin(double data[],int nn[],double mat[3])
{
  int x,y;

  mat[0]=mat[1]=mat[2]=0;


  for  (x=0;x<nn[0]; x++)
    for (y=0;y<nn[1]; y++){
/*
      mat[0]+=pow2(sin(2*M_PI/nn[0]*x))*data[x*nn[1]+y];
      mat[1]+=    sin(2*M_PI/nn[0]*x)*
	          sin(2*M_PI/nn[1]*y)   *data[x*nn[1]+y];
      mat[2]+=pow2(sin(2*M_PI/nn[1]*y))*data[x*nn[1]+y];
*/
      mat[0]+=pow2(sin(2*M_PI/nn[0]*(x-nn[0]/2)))*data[x*nn[1]+y];
      mat[1]+=    sin(2*M_PI/nn[0]*(x-nn[0]/2))*
	          sin(2*M_PI/nn[1]*(y-nn[1]/2))   *data[x*nn[1]+y];
      mat[2]+=pow2(sin(2*M_PI/nn[1]*(y-nn[1]/2)))*data[x*nn[1]+y];

    }
}

double Sd0(double data[],int nn[])
{
  int i;
  double sum=0;

  for  (i=0;i<nn[0]*nn[1]; i++) sum+=data[i];
  return sum;
}




int drop_data(grstr graph,double cut,double *a,double *b, double *tet)
{
  int l,*length,nox=0,noy=0,boundary=0;
  double *cont,*gp=NULL;
  /*
  getgraph3d(graph,Scalar,&gp,&nox,&noy,0,1,1,1);
  */
  if (!gp) { no_graph_data("drop_data"); *a=0;*b=0;*tet=0; return 0; }
  contour(gp,nox,noy,
	  cut,&cont,&length,0,NULL,&boundary);
  l=length[0];

  free(gp);
  if (l>0)  
    {
      data_from_contour(cont,length[1],a,b,tet);
      free(cont);
      free(length);
      return 1;
    }
  else 
    {
      free(cont);
      free(length);
      return 0;
    }
}

void get_fftdata(grstr graph,int nogr,double *d0, double *kav, 
		 double d1[2],double d2[3])
{ 
  int x,y,nn[2],m,ct,tvar,nox=0,noy=0;
  double *field,*field_base,*gp=NULL;
  double average;

  /*
  getgraph3d(graph,Scalar,&gp,&nox,&noy,nogr,1,1,1);
  */
  if (!gp) {
    no_graph_data("get_fftdata");
    *d0=*kav=0; d1[0]=d1[1]=0; d2[0]=d2[1]=d2[2]=0;
    return;
  }
  nn[0]=nox;
  nn[1]=noy;

/* Checking that size=2^n ... need to tell that something is wrong...*/
  tvar=nn[0];
  for (m=1,ct=0;m<16;ct+=tvar&1,tvar>>=1,m++);
  if (ct!=1) {
    printf("get_fftdata failed because xdim=%i is not a power of two\n",
	   nn[0]);
    return;
  }
  tvar=nn[1];
  for (m=1,ct=0;m<16;ct+=tvar&1,tvar>>=1,m++);
  if (ct!=1) {
    printf("get_fftdata failed because ydim=%i is not a power of two\n",
	   nn[1]);
    return;
  }

  field_base=(double *) malloc((nn[0]*nn[1]*2+1)*sizeof(double));
  field=field_base+1; /* field_base[1..]: fourn's 1-based view */
  average=0;
  for (x=0;x<nn[0];x++)
    for (y=0;y<nn[1]; y++)
      average+=gp[x*noy+y];
  average/=nn[0]*nn[1];
  for (x=0;x<nn[0];x++)
    for (y=0;y<nn[1]; y++){
      field[2*(x*nn[1]+y)]=gp[x*noy+y]-average;
      field[2*(x*nn[1]+y)+1]=0;
    }
  { int nn1[3]; nn1[0]=0; nn1[1]=nn[0]; nn1[2]=nn[1]; fourn(field_base, nn1, 2, 1); }
  for (x=0;x<nn[0];x++)
    for (y=0;y<nn[1]; y++)
      gp[x*noy+y]=
/*	pow2(field[2*(x*nn[1]+y)])
	  +pow2(field[2*(x*nn[1]+y)+1]);
*/
	pow2(field[2*(((x+nn[0]/2)%nn[0])*nn[1]+
			  ((y+nn[1]/2)%nn[1]))])
	    +pow2(field[2*(((x+nn[0]/2)%nn[0])*nn[1]+
			  ((y+nn[1]/2)%nn[1]))+1]);
  free(field_base);
  *d0= S0(gp,nn);
  *kav=S1(gp,nn)/ *d0;
  Sk(gp,nn,*kav,d1);
  Sd2(gp,nn,d2);
  free(gp);
}

void get_Struct_data(grstr graph,int nogr,double *S_0, double *S_1, 
		     double *S_2, double *S_3, int *Slen, int *size)
{ 
  int x,y,nn[2],nnm,m,ct,tvar,nox,noy,i;
  int *N;
  double *field,*field_base,*S,*gp=NULL;
  double average;
  /*
  getgraph3d(graph,Scalar,&gp,&nox,&noy,nogr,1,1,1);
  */
  nox=noy=0;
  if (!gp) {
    no_graph_data("get_Struct_data");
    *S_0=*S_1=*S_2=*S_3=0; *Slen=0; *size=0;
    return;
  }
  nn[0]=nox;
  nn[1]=noy;
  
  
  /* Checking that size=2^n ... need to tell that something is wrong...*/
  tvar=nn[0];
  for (m=1,ct=0;m<16;ct+=tvar&1,tvar>>=1,m++);
  if (ct!=1) {
    printf("get_Struct_data failed because xdim=%i is not a power of two\n",
	   nn[0]);
    return;
  }
  tvar=nn[1];
  for (m=1,ct=0;m<16;ct+=tvar&1,tvar>>=1,m++);
  if (ct!=1) {
    printf("get_Struct_data failed because ydim=%i is not a power of two\n",
	   nn[1]);
    return;
  }
  if (nn[0]>nn[1]) nnm=nn[1]; else nnm=nn[0];
  *size=nnm;

  field_base=(double *) malloc((nn[0]*nn[1]*2+1)*sizeof(double));
  field=field_base+1; /* field_base[1..]: fourn's 1-based view */
  average=0;
  for (x=0;x<nn[0];x++)
    for (y=0;y<nn[1]; y++)
      average+=gp[x*noy+y];
  average/=nn[0]*nn[1];
  for (x=0;x<nn[0];x++)
    for (y=0;y<nn[1]; y++){
      field[2*(x*nn[1]+y)]=gp[x*noy+y]-average;
      field[2*(x*nn[1]+y)+1]=0;
    }
  { int nn1[3]; nn1[0]=0; nn1[1]=nn[0]; nn1[2]=nn[1]; fourn(field_base, nn1, 2, 1); }
  for (x=0;x<nn[0];x++)
    for (y=0;y<nn[1]; y++)
      gp[x*noy+y]=
	/*	pow2(field[2*(x*nn[1]+y)])
		+pow2(field[2*(x*nn[1]+y)+1]);
		*/
	pow2(field[2*(((x+nn[0]/2)%nn[0])*nn[1]+
		     ((y+nn[1]/2)%nn[1]))])
       +pow2(field[2*(((x+nn[0]/2)%nn[0])*nn[1]+
		     ((y+nn[1]/2)%nn[1]))+1]);
  free(field_base);
  *Slen=nnm/2+1;
  S    = (double *) malloc(*Slen*sizeof(double));
  N    = (int    *) malloc(*Slen*sizeof(int ));
  for (i=0;i<*Slen;i++) {S[i]=0;N[i]=0;}
  for (x=0;x<nn[0];x++)
    for (y=0;y<nn[1]; y++){
      i=ceil(sqrt(pow2((x-nn[0]/2)*nnm/nn[0])+
		   pow2((y-nn[1]/2)*nnm/nn[1]))-0.5);
      if (i<*Slen){
	S[i]+=gp[x*noy+y];
	N[i]++;
      }
    }
  free(gp);
  *S_0=*S_1=*S_2=*S_3=0;
  for (i=0;i<*Slen;i++){
    if (N[i]>0) S[i]/=N[i];
    *S_0+=S[i];
    *S_1+=i*S[i];
    *S_2+=i*i*S[i];
    *S_3+=i*i*i*S[i];
  }
}

double* get_structfact(grstr graph,int nogr,int *Slen)
{ 
  int x,y,nn[2],nnm,m,ct,tvar,nox,noy,i;
  int *N;
  double *field,*field_base,*S,*gp=NULL;
  double average;

  /*
  getgraph3d(graph,Scalar,&gp,&nox,&noy,nogr,1,1,1);
  */
  nox=noy=0;
  if (!gp) { no_graph_data("get_structfact"); *Slen=0; return NULL; }
  nn[0]=nox;
  nn[1]=noy;
  
  
  /* Checking that size=2^n ... need to tell that something is wrong...*/
  tvar=nn[0];
  for (m=1,ct=0;m<16;ct+=tvar&1,tvar>>=1,m++);
  if (ct!=1) {
    printf("get_Struct_data failed because xdim=%i is not a power of two\n",
	   nn[0]);
    return NULL;
  }
  tvar=nn[1];
  for (m=1,ct=0;m<16;ct+=tvar&1,tvar>>=1,m++);
  if (ct!=1) {
    printf("get_Struct_data failed because ydim=%i is not a power of two\n",
	   nn[1]);
    return NULL;
  }
  if (nn[0]>nn[1]) nnm=nn[1]; else nnm=nn[0];
  
  field_base=(double *) malloc((nn[0]*nn[1]*2+1)*sizeof(double));
  field=field_base+1; /* field_base[1..]: fourn's 1-based view */
  average=0;
  for (x=0;x<nn[0];x++)
    for (y=0;y<nn[1]; y++)
      average+=gp[x*noy+y];
  average/=nn[0]*nn[1];
  for (x=0;x<nn[0];x++)
    for (y=0;y<nn[1]; y++){
      field[2*(x*nn[1]+y)]=gp[x*noy+y]-average;
      field[2*(x*nn[1]+y)+1]=0;
    }
  { int nn1[3]; nn1[0]=0; nn1[1]=nn[0]; nn1[2]=nn[1]; fourn(field_base, nn1, 2, 1); }
  for (x=0;x<nn[0];x++)
    for (y=0;y<nn[1]; y++)
      gp[x*noy+y]=
	/*	pow2(field[2*(x*nn[1]+y)])
		+pow2(field[2*(x*nn[1]+y)+1]);
		*/
	pow2(field[2*(((x+nn[0]/2)%nn[0])*nn[1]+
		     ((y+nn[1]/2)%nn[1]))])
       +pow2(field[2*(((x+nn[0]/2)%nn[0])*nn[1]+
		     ((y+nn[1]/2)%nn[1]))+1]);
  free(field_base);
  *Slen=nnm/2+1;
  S    = (double *) malloc(*Slen*sizeof(double));
  N    = (int    *) malloc(*Slen*sizeof(int ));
  for (i=0;i<*Slen;i++) {S[i]=0;N[i]=0;}
  for (x=0;x<nn[0];x++)
    for (y=0;y<nn[1]; y++){
      i=ceil(sqrt(pow2((x-nn[0]/2)*nnm/nn[0])+
		   pow2((y-nn[1]/2)*nnm/nn[1]))-0.5);
      if (i<*Slen){
	S[i]+=gp[x*noy+y];
	N[i]++;
      }
    }
  free(gp);
  for (i=0;i<*Slen;i++){
    if (N[i]>0) S[i]/=N[i];
  }
  free(N);
  return S;
}

double* GetStructFactFromData(double *gp,int nox,int noy,int *Slen)
{
  int x,y,nn[2],nnm,m,ct,tvar,i;
  int *N;
  double *field,*field_base,*S;
  double average;
  
  nn[0]=nox;
  nn[1]=noy;
  
  
  /* Checking that size=2^n ... need to tell that something is wrong...*/
  tvar=nn[0];
  for (m=1,ct=0;m<16;ct+=tvar&1,tvar>>=1,m++);
  if (ct!=1) {
    printf("get_Struct_data failed because xdim=%i is not a power of two\n",
	   nn[0]);
    return NULL;
  }
  tvar=nn[1];
  for (m=1,ct=0;m<16;ct+=tvar&1,tvar>>=1,m++);
  if (ct!=1) {
    printf("get_Struct_data failed because ydim=%i is not a power of two\n",
	   nn[1]);
    return NULL;
  }
  if (nn[0]>nn[1]) nnm=nn[1]; else nnm=nn[0];
  
  field_base=(double *) malloc((nn[0]*nn[1]*2+1)*sizeof(double));
  field=field_base+1; /* field_base[1..]: fourn's 1-based view */
  average=0;
  for (x=0;x<nn[0];x++)
    for (y=0;y<nn[1]; y++)
      average+=gp[x*noy+y];
  average/=nn[0]*nn[1];
  for (x=0;x<nn[0];x++)
    for (y=0;y<nn[1]; y++){
      field[2*(x*nn[1]+y)]=gp[x*noy+y]-average;
      field[2*(x*nn[1]+y)+1]=0;
    }
  { int nn1[3]; nn1[0]=0; nn1[1]=nn[0]; nn1[2]=nn[1]; fourn(field_base, nn1, 2, 1); }
  for (x=0;x<nn[0];x++)
    for (y=0;y<nn[1]; y++)
      gp[x*noy+y]=
	/*	pow2(field[2*(x*nn[1]+y)])
		+pow2(field[2*(x*nn[1]+y)+1]);
		*/
	pow2(field[2*(((x+nn[0]/2)%nn[0])*nn[1]+
		     ((y+nn[1]/2)%nn[1]))])
       +pow2(field[2*(((x+nn[0]/2)%nn[0])*nn[1]+
		     ((y+nn[1]/2)%nn[1]))+1]);
  free(field_base);
  *Slen=nnm/2+1;
  S    = (double *) malloc(*Slen*sizeof(double));
  N    = (int    *) malloc(*Slen*sizeof(int ));
  for (i=0;i<*Slen;i++) {S[i]=0;N[i]=0;}
  for (x=0;x<nn[0];x++)
    for (y=0;y<nn[1]; y++){
      i=ceil(sqrt(pow2((x-nn[0]/2)*nnm/nn[0])+
		   pow2((y-nn[1]/2)*nnm/nn[1]))-0.5);
      if (i<*Slen){
	S[i]+=gp[x*noy+y];
	N[i]++;
      }
    }
  for (i=0;i<*Slen;i++){
    if (N[i]>0) S[i]/=N[i];
  }
  free(N);
  return S;
}

