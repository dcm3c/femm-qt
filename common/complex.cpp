// SPDX-License-Identifier: MIT
// Copyright (C) 2026 David Meeker and contributors

#include "StdAfx.h"
#include "math.h"
#include "stdio.h"
#include "complex.h"
#include <charconv>

CComplex::CComplex(double x)
{
	re=x; im=0.;
}

CComplex::CComplex(int x)
{
	re=(double) x;
	im=0.;
}

CComplex::CComplex()
{
	re=0.;	im=0.;
}

CComplex::CComplex(double x, double y)
{
	re=x; im=y;
}

CComplex CComplex::Sqrt()
{
	double w,z;
	CComplex y;

	if((re==0) && (im==0)) w=0;
	else if(fabs(re)>fabs(im)){
		z=im/re;
		w=sqrt(fabs(re))*sqrt( (1.+sqrt(1.+z*z))/2. );
	}
	else{
		z=re/im;
		w=sqrt(fabs(im))*sqrt( (fabs(z)+sqrt(1.+z*z))/2. );
	}

	if(w==0){
		y.re=0;
		y.im=0;
		return y;
	}

	if(re>=0){
		y.re=w;
		y.im=im/(2.*w);
		return y;
	}

	if(im>=0){
		y.re=fabs(im)/(2.*w);
		y.im=w;
		return y;
	}

	y.re=fabs(im)/(2.*w);
	y.im= (-w);
	return y;
}

CComplex CComplex::Conj()
{
	return CComplex(re,-im);
}

double CComplex::Abs()
{
	if ((re==0) && (im==0)) return 0.;

	if (fabs(re)>fabs(im))
		return fabs(re)*sqrt(1.+(im/re)*(im/re));
	else
		return fabs(im)*sqrt(1.+(re/im)*(re/im));
}

double CComplex::Arg()
{
	if ((re==0) && (im==0)) return 0.;

	return atan2(im,re);
}

CComplex CComplex::Inv()
{
	double c;
	CComplex z;

	if(fabs(re)>fabs(im))
	{
		c=im/re;
		z.re=1./(re*(1.+c*c));
		z.im=(-c)*z.re;
	}
	else{
		c=re/im;
		z.im=(-1.)/(im*(1.+c*c));
		z.re=(-c)*z.im;
	}

	return z;
}

double CComplex::Re()
{
	return re;
}

double CComplex::Im()
{
	return im;
}

void CComplex::Set(double x, double y)
{
	re=x; im=y;
}

char* CComplex::ToString(char *s)
{
	return ToString(s, 256);
}

// One component as the SHORTEST digit string that strtod reads back to the
// identical double (std::to_chars), rather than "%.17g". Losslessness was
// %.17g's only purpose here and to_chars keeps it, while being ~10x faster --
// the same swap common/fastio.h made for the .ans hot loops. (Size barely
// moves on computed values: 2-4%, not fastio.h's 30-40% for .ans input.)
//
// This matters beyond speed: ToString feeds the IPC wire (via appendComplex in
// libfemm/py_mag_postproc.cpp), and libfemm/py_state.cpp's format_complex --
// which formats every OTHER wire result -- moved to to_chars in 2026-08. The
// two must agree, or one command returns "0.1" while the next returns
// "0.10000000000000001" for the same value. The I / -I / I* STRUCTURE below is
// the contract and is unchanged; only the digits are.
static void cx_g(char *b, size_t n, double v)
{
	std::to_chars_result r = std::to_chars(b, b + n - 1, v);
	*(r.ec == std::errc() ? r.ptr : b) = 0;
}

char* CComplex::ToString(char *s, size_t n)
{
	char a[40], c[40];
	if (im==0) { cx_g(a,sizeof a,re); snprintf(s, n,"%s",a); }
	else if (im==1)
	{
		if (re==0) snprintf(s, n,"I");
		else { cx_g(a,sizeof a,re); snprintf(s, n,"%s+I",a); }
	}
	else if (im==-1)
	{
		if (re==0) snprintf(s, n,"-I");
		else { cx_g(a,sizeof a,re); snprintf(s, n,"%s-I",a); }
	}
	else if (im<0)
	{
		cx_g(c,sizeof c,fabs(im));
		if (re!=0) { cx_g(a,sizeof a,re); snprintf(s, n,"%s-I*%s",a,c); }
		else  snprintf(s, n,"-I*%s",c);
	}
	else if (im>0)
	{
		cx_g(c,sizeof c,im);
		if (re!=0) { cx_g(a,sizeof a,re); snprintf(s, n,"%s+I*%s",a,c); }
		else snprintf(s, n,"I*%s",c);
	}
	else s[0]=0;

	return s;
}

//******* Addition ***************************************************

CComplex CComplex::operator+( const CComplex& z )
{
	return CComplex(re+z.re,im+z.im);
};

CComplex CComplex::operator+( int z )
{
	return CComplex(re+((double) z),im);
};

CComplex CComplex::operator+( double z )
{
	return CComplex(re+z,im);
};

void CComplex::operator+=( const CComplex& z)
{
	re+=z.re;
	im+=z.im;
};

void CComplex::operator+=( double z )
{
	re+=z;
};

void CComplex::operator+=( int z )
{
	re+=(double) z;
};

CComplex operator+( int x, const CComplex& y )
{
	return CComplex( ((double) x) + y.re, y.im );
}

CComplex operator+( double x, const CComplex& y )
{
	return CComplex( x + y.re, y.im );
}

CComplex operator+( const CComplex& x, const CComplex& y )
{
	return CComplex( x.re + y.re, x.im + y.im );
}


//******* Subtraction ***************************************************
CComplex CComplex::operator-()
{
	return CComplex(-re,-im);
}

CComplex CComplex::operator-( const CComplex& z)
{
	return CComplex(re-z.re,im-z.im);
};

CComplex CComplex::operator-( int z )
{
	return CComplex(re-((double) z),im);
};

CComplex CComplex::operator-( double z )
{
	return CComplex(re-z,im);
};

void CComplex::operator-=( const CComplex& z)
{
	re-=z.re;
	im-=z.im;
};

void CComplex::operator-=( double z )
{
	re-=z;
};

void CComplex::operator-=( int z )
{
	re-=(double) z;
};

CComplex operator-( int x, const CComplex& y )
{
	return CComplex( ((double) x) - y.re, - y.im );
}

CComplex operator-( double x, const CComplex& y )
{
	return CComplex( x - y.re, - y.im );
}

CComplex operator-( const CComplex& x, const CComplex& y )
{
	return CComplex( x.re - y.re, x.im - y.im );
}

CComplex operator-( const CComplex& y )
{
	return CComplex( -y.re,-y.im );
}
//******* Multiplication ***************************************************

CComplex CComplex::operator*( const CComplex& z)
{
	return CComplex(re*z.re - im*z.im,re*z.im + im*z.re);
};

CComplex CComplex::operator*( int z )
{
	return CComplex( re*((double) z),im*((double) z) );
};

CComplex CComplex::operator*( double z )
{
	return CComplex(re*z,im*z);
};

void CComplex::operator*=( const CComplex& z)
{
	CComplex x(re*z.re - im*z.im,re*z.im + im*z.re);
	re=x.re; im=x.im;
};

void CComplex::operator*=( double z )
{
	re*=z; im*=z;
};

void CComplex::operator*=( int z )
{
	re*=(double) z;
	im*=(double) z;
};

CComplex operator*( int x, const CComplex& y )
{
	return CComplex( ((double) x) * y.re, ((double) x)*y.im );
}

CComplex operator*( double x, const CComplex& y )
{
	return CComplex( x*y.re, x*y.im );
}

CComplex operator*( const CComplex& x, const CComplex& y )
{
	return CComplex( x.re*y.re-x.im*y.im, x.re*y.im+x.im*y.re );
}

//******* Division ***************************************************

CComplex CComplex::operator/( const CComplex& z)
{
	double c;
	CComplex y;

	if(fabs(z.re)>fabs(z.im))
	{
		c=z.im/z.re;
		y.re=1./(z.re*(1.+c*c));
		y.im=(-c)*y.re;
	}
	else{
		c=z.re/z.im;
		y.im=(-1.)/(z.im*(1.+c*c));
		y.re=(-c)*y.im;
	}

	return *this * y;
};


CComplex CComplex::operator/( int z )
{
	return CComplex(re/((double) z),im/((double) z));
};

CComplex CComplex::operator/( double z )
{
	return CComplex(re/z,im/z);
};

void CComplex::operator/=( const CComplex& z)
{
	*this=*this/z;
};

void CComplex::operator/=( double z )
{
	re/=z;
	im/=z;
};

void CComplex::operator/=( int z )
{
	re/=(double) z;
	im/=(double) z;
};

CComplex operator/( int x, const CComplex& z )
{
	double c;
	CComplex y;

	if(fabs(z.re)>fabs(z.im))
	{
		c=z.im/z.re;
		y.re=1./(z.re*(1.+c*c));
		y.im=(-c)*y.re;
	}
	else{
		c=z.re/z.im;
		y.im=(-1.)/(z.im*(1.+c*c));
		y.re=(-c)*y.im;
	}
	
	y.re*=(double) x;
	y.im*=(double) x;

	return y;
}

CComplex operator/( double x, const CComplex& z )
{
	double c;
	CComplex y;

	if(fabs(z.re)>fabs(z.im))
	{
		c=z.im/z.re;
		y.re=1./(z.re*(1.+c*c));
		y.im=(-c)*y.re;
	}
	else{
		c=z.re/z.im;
		y.im=(-1.)/(z.im*(1.+c*c));
		y.re=(-c)*y.im;
	}
	
	y.re*= x;
	y.im*= x;

	return y;
}

CComplex operator/( const CComplex& x, const CComplex& z )
{
	double c;
	CComplex y;

	if(fabs(z.re)>fabs(z.im))
	{
		c=z.im/z.re;
		y.re=1./(z.re*(1.+c*c));
		y.im=(-c)*y.re;
	}
	else{
		c=z.re/z.im;
		y.im=(-1.)/(z.im*(1.+c*c));
		y.re=(-c)*y.im;
	}

	return x*y;
}

//****** Equals definitions ********************************

void CComplex::operator=(double z)
{
	re=z;
	im=0;
}

void CComplex::operator=(int z)
{
	re=(double) z;
	im=0;

}

//***** Tests ***********************************************
bool CComplex::operator==( const CComplex& z){
	if ((z.im==im) && (z.re==re)) return TRUE;
	return FALSE;
}

bool CComplex::operator==(double z){
	if ((z==re) && (im==0)) return TRUE;
	return FALSE;
}

bool CComplex::operator==(int z){
	if ((re==(double) z) && (im==0)) return TRUE;
	return FALSE;
}

bool CComplex::operator!=( const CComplex& z){
	if ((z.re==re) && (z.im==im)) return FALSE;
	return TRUE;
}

bool CComplex::operator!=(double z){
	if ((re!=z) || (im!=0)) return TRUE;
	return FALSE;
}

bool CComplex::operator!=(int z){
	if ((re!=(double) z) || (im!=0)) return TRUE;
	return FALSE;
}

//***** Useful functions ************************************

CComplex conj( const CComplex& x)
{
	return CComplex(x.re,-x.im);
}

CComplex exp( const CComplex& x)
{
	CComplex y;
	
	y.re=cos(x.im)*exp(x.re);
	y.im=sin(x.im)*exp(x.re);
	
	return y;
}

CComplex sqrt( const CComplex& x)
{
	double w,z;
	CComplex y;

	if((x.re==0) && (x.im==0)) w=0;
	else if(fabs(x.re)>fabs(x.im)){
		z=x.im/x.re;
		w=sqrt(fabs(x.re))*sqrt( (1.+sqrt(1.+z*z))/2. );
	}
	else{
		z=x.re/x.im;
		w=sqrt(fabs(x.im))*sqrt( (fabs(z)+sqrt(1.+z*z))/2. );
	}

	if(w==0){
		y.re=0;
		y.im=0;
		return y;
	}

	if(x.re>=0){
		y.re=w;
		y.im=x.im/(2.*w);
		return y;
	}

	if(x.im>=0){
		y.re=fabs(x.im)/(2.*w);
		y.im=w;
		return y;
	}

	y.re=fabs(x.im)/(2.*w);
	y.im= (-w);

	return y;
}

CComplex tanh( const CComplex& x)
{
	CComplex y;

	if (x.re>0){
		y=(1-exp(-2*x))/(1+exp(-2*x));
	}
	else{
		y=(exp(2*x)-1)/(exp(2*x)+1);
	}

	return y;
}

CComplex sinh( const CComplex& x)
{
	return (exp(x)-exp(-x))/2;
}

CComplex cosh( const CComplex& x)
{
	return (exp(x)+exp(-x))/2;
}


CComplex cos( const CComplex& x)
{
	return (exp(I*x)+exp(-I*x))/2;
}

CComplex acos( const CComplex& x)
{
	return PI/2. - arg(I*x + sqrt(1 - x*x)) + I*log(abs(I*x + sqrt(1 - x*x)));
}

CComplex sin( const CComplex& x)
{
	return (exp(I*x)-exp(-I*x))/(2*I);
}

CComplex asin( const CComplex& x)
{
	return arg(I*x + sqrt(1 - x*x)) - I*log(abs(I*x + sqrt(1 - x*x)));
}

CComplex tan( const CComplex& x)
{
	return sin(x)/cos(x);
}

CComplex atan( const CComplex& x)
{
	return (arg(1+I*x) - arg(1-I*x) - I*(log(abs(1+I*x)/abs(1-I*x))))/ 2;
}

double abs( const CComplex& x)
{
	if ((x.re==0) && (x.im==0)) return 0.;

	if (fabs(x.re)>fabs(x.im))
		return fabs(x.re)*sqrt(1.+(x.im/x.re)*(x.im/x.re));
	else
		return fabs(x.im)*sqrt(1.+(x.re/x.im)*(x.re/x.im));
}

double arg( const CComplex& x)
{
	if ((x.re==0) && (x.im==0)) return 0.;

	return atan2(x.im,x.re);
}

CComplex log( const CComplex& x)
{
	CComplex y;

	y.im=arg(x);
	y.re=log(abs(x));

	return y;
}

// True if y is a real integer small enough to raise by repeated multiplication.
// The 100 cutoff matches CPython's complex_pow (and our MicroPython patch): it
// bounds the rounding that accumulates over the repeated multiplies. NaN fails
// the range test, so it correctly falls through to the polar form.
static bool IsSmallIntegerExponent(double y, int &n)
{
	if (!(y >= -100. && y <= 100.)) return false;
	n = (int) y;
	return (double) n == y;
}

// An integral exponent is raised by repeated multiplication (the pow(x,int)
// overload below) rather than exp(y*log(x)). The polar form has to evaluate
// cos()/sin() of a multiple of pi, and pi is not representable in binary, so it
// leaves dirt behind: pow(-3, 2.) came out as 9.0000000000000018 - 2.2e-15i
// rather than exactly 9, giving a real quantity a phantom imaginary part. This
// matters because the expression evaluators (a "B^2" in a material property)
// call these overloads, never pow(x,int) directly.
CComplex pow( const CComplex& x, double y)
{
	int n;
	if (IsSmallIntegerExponent(y, n)) return pow(x, n);

	return exp(y*log(x));
}

CComplex pow( const CComplex& x, int y)
{
	if (y==0) return CComplex(1,0);

	int i;
	CComplex z;

	if (y>0){
		z=x;
		for(i=1;i<y;i++) z*=x;
	}
	else{
		z=1/x;
		CComplex w=z;
		for(i=1;i<(-y);i++) z*=w;
	}

	return z;
}

CComplex pow( const CComplex& x, const CComplex& y)
{
	int n;
	if (y.im == 0 && IsSmallIntegerExponent(y.re, n)) return pow(x, n);

	return exp(y*log(x));
}

double Re( const CComplex& a)
{
	return a.re;
}

double Im( const CComplex& a)
{
	return a.im;
}

