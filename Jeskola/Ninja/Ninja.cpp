#include <windef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <float.h>
#include <MachineInterface.h>
#include "dsplib.h"

double const SilentEnough = log(1.0 / 32768);

#define MAX_TAPS		8

CMachineParameter const paraLength = 
{ 
	pt_word,										// type
	"Length",
	"Length in length units",						// description
	1,												// MinValue	
	32768,											// MaxValue
	65535,											// NoValue
	MPF_STATE,										// Flags
	3
};

#define UNIT_TICK		0
#define UNIT_MS			1
#define UNIT_SAMPLE		2
#define UNIT_256		3

CMachineParameter const paraDryThru = 
{ 
	pt_switch,										// type
	"Dry thru",
	"Dry thru (1 = yes, 0 = no)",					// description
	-1,												// MinValue	
	-1,												// MaxValue
	SWITCH_NO,										// NoValue
	MPF_STATE,										// Flags
	SWITCH_ON
};

CMachineParameter const paraUnit =
{ 
	pt_byte, 										// type
	"Length unit",
	"Length unit (0 = tick (default), 1 = ms, 2 = sample, 3 = 256th of tick)",		
	0,												// MinValue	
	3,												// MaxValue
	0xff,											// NoValue
	MPF_STATE,										// Flags
	0
};

CMachineParameter const paraFeedback = 
{ 
	pt_byte,										// type
	"Feedback",
	"Feedback (00 = -100%, 40=0%, 80 = 100%)",		// description
	0,												// MinValue	
	128,											// MaxValue
	255,											// NoValue
	MPF_STATE,										// Flags
	0x70
};

CMachineParameter const paraFilterType = 
{ 
	pt_byte,										// type
	"f. type",
	"Filter Type (0 = lowpass, 1 = highpass, 2 = bandpass, 3 = bandreject)",// description
	0,												// MinValue	
	3,   										// MaxValue
	255,											// NoValue
	MPF_STATE,										// Flags
	1     
};

CMachineParameter const paraFilterCutoff = 
{ 
	pt_word,										// type
	"f. cutoff",
	"Filter Cutoff frequency in Hz",// description
	10,												// MinValue	
	20000,		   										// MaxValue
	255,											// NoValue
	MPF_STATE,										// Flags
	550    
};

CMachineParameter const paraFilterBandwidth = 
{ 
	pt_word,										// type
	"f. bandwidth",
	"Filter Bandwidth (for bandpass and bandreject filters)",								// description
	1,												// MinValue	
	10000,		   										// MaxValue
	255,											// NoValue
	MPF_STATE,										// Flags
	100    
};

CMachineParameter const paraWetOut = 
{ 
	pt_byte,										// type
	"Wet out",
	"Wet out (00 = 0%, FE = ~200%)",				// description
	0,												// MinValue	
	128,											// MaxValue
	0xfe,											// NoValue
	MPF_STATE,										// Flags
	0x60
};


CMachineParameter const *pParameters[] = 
{ 
	&paraDryThru,
	&paraLength,
	&paraUnit,
	&paraFeedback,
	&paraFilterType,
	&paraFilterCutoff,
	&paraFilterBandwidth,
	&paraWetOut,
};

CMachineAttribute const attrMaxDelay = 
{
	"Max Delay (ms)",
	1,
	100000,
	1000	
};

CMachineAttribute const *pAttributes[] = 
{
	&attrMaxDelay
};

#pragma pack(1)

class gvals
{
public:
	byte drythru;
};

class tvals
{
public:
	word length;
	byte unit;
	byte feedback;
	byte filtertype;
	word filtercutoff;
	word filterbw;
	byte wetout;
};

class avals
{
public:
	int maxdelay;
};

#pragma pack()

CMachineInfo const MacInfo = 
{
	MT_EFFECT,								// type
	MI_VERSION,
	0,										// flags
	1,										// min tracks
	MAX_TAPS,								// max tracks
	1,										// numGlobalParameters
	7,										// numTrackParameters
	pParameters,
	1,
	pAttributes,
#ifdef _DEBUG
	"Jeskola Ninja Delay (Debug build)",	// name
#else
	"Jeskola Ninja Delay",
#endif
	"Ninja",								// short name
	"Oskari Tammelin",						// author
	NULL
};

#define FILTER_LP		0
#define FILTER_HP		1
#define FILTER_BP		2
#define FILTER_BR		3

class CTrack
{
public:
	CBWState Filter;
	float *Buffer;
	int Length;
	int Pos;
	float Feedback;
	float WetOut;
	int Unit;

	int FilterType;
	int FilterCutoff;
	int FilterBandwidth;
};
 


class mi : public CMachineInterface
{
public:
	mi();
	virtual ~mi();

	virtual void Init(CMachineDataInput * const pi);
	virtual void Tick();
	virtual bool Work(float *psamples, int numsamples, int const mode);

	virtual void SetNumTracks(int const n);

	virtual void AttributesChanged();

	virtual char const *DescribeValue(int const param, int const value);

private:
	void InitTrack(int const i);
	void ResetTrack(int const i);

	void TickTrack(CTrack *pt, tvals *ptval);
	void WorkTrack(CTrack *pt, float *pin, float *pout, int numsamples, int const mode);

	void SetFilter(CTrack &t);

private:
	int MaxDelay;	// in samples
	int IdleCount;	// in samples
	int DelayTime;
	bool IdleMode;
	bool DryThru;

private:
	int numTracks;
	CTrack Tracks[MAX_TAPS];

	avals aval;
	gvals gval;
	tvals tval[MAX_TAPS];


};

DLL_EXPORTS

mi::mi()
{
	GlobalVals = &gval;
	TrackVals = tval;
	AttrVals = (int *)&aval;
}

mi::~mi()
{
	for (int c = 0; c < MAX_TAPS; c++)
	{
		delete[] Tracks[c].Buffer;
	}
}

char const *mi::DescribeValue(int const param, int const value)
{
	static char txt[16];

	switch(param)
	{
	case 1:		// length
		return NULL;
		break;
	case 2:		// unit
		switch(value)
		{
		case 0: return "tick";
		case 1: return "ms";
		case 2: return "sample";
		case 3: return "tick/256";
		}
		break;
	case 3:		// feedback
		sprintf(txt, "%.1f%%", (double)(value-64) * (100.0 / 64.0));
		break;
	case 4:
		switch(value)
		{
		case FILTER_LP: return "lowpass"; 
		case FILTER_HP: return "highpass";
		case FILTER_BP: return "bandpass";
		case FILTER_BR: return "bandreject";
		}
		break;
	case 5:
		sprintf(txt, "%dHz", value);
		break;
	case 6:
		sprintf(txt, "%dcps", value);
		break;
	case 7:		// wetout
		sprintf(txt, "%.1f%%", (double)value * (100.0 / 128.0));
		break;
	default:
		return NULL;
	}

	return txt;
}


void mi::SetFilter(CTrack &t)
{
	switch(t.FilterType)
	{
	case FILTER_LP:	DSP_BW_InitLowpass(t.Filter, (float)t.FilterCutoff); break;
	case FILTER_HP:	DSP_BW_InitHighpass(t.Filter, (float)t.FilterCutoff); break;
	case FILTER_BP:	DSP_BW_InitBandpass(t.Filter, (float)t.FilterCutoff, (float)t.FilterBandwidth); break;
	case FILTER_BR:	DSP_BW_InitBandreject(t.Filter, (float)t.FilterCutoff, (float)t.FilterBandwidth); break;
	}

}

void mi::Init(CMachineDataInput * const pi)
{
	numTracks = 1;
	DryThru = true;

	for (int c = 0; c < MAX_TAPS; c++)
	{
		Tracks[c].Buffer = NULL;
		Tracks[c].Unit = UNIT_TICK;
		Tracks[c].Length = pMasterInfo->SamplesPerTick * 3;
		Tracks[c].Pos = 0;
		Tracks[c].Feedback = 0.3f;
		Tracks[c].WetOut = 0;
		Tracks[c].FilterType = 1;
		Tracks[c].FilterCutoff = 1000;
		Tracks[c].FilterBandwidth = 1000;
		
		DSP_BW_Reset(Tracks[c].Filter);
		SetFilter(Tracks[c]);
		
	}

	Tracks[0].WetOut = 0.3f;	// enable first track

	IdleMode = true;
	IdleCount = 0;

#ifndef _MSC_VER
    DSP_Init(pMasterInfo->SamplesPerSec);
#endif
}

void mi::AttributesChanged()
{
	MaxDelay = (int)(pMasterInfo->SamplesPerSec * (aval.maxdelay / 1000.0));
	for (int c = 0; c < numTracks; c++)
		InitTrack(c);
}


void mi::SetNumTracks(int const n)
{
	if (numTracks < n)
	{
		for (int c = numTracks; c < n; c++)
			InitTrack(c);
	}
	else if (n < numTracks)
	{
		for (int c = n; c < numTracks; c++)
			ResetTrack(c);
	
	}
	numTracks = n;

}


void mi::InitTrack(int const i)
{
	delete[] Tracks[i].Buffer;
	Tracks[i].Buffer = new float [MaxDelay];
	memset(Tracks[i].Buffer, 0, MaxDelay*4);
	Tracks[i].Pos = 0;
	if (Tracks[i].Length > MaxDelay)
		Tracks[i].Length = MaxDelay;
}

void mi::ResetTrack(int const i)
{
	delete[] Tracks[i].Buffer;
	Tracks[i].Buffer = NULL;
}


void mi::TickTrack(CTrack *pt, tvals *ptval)
{
	bool recalcfilter = false;

	if (ptval->filtertype != paraFilterType.NoValue)
	{
		pt->FilterType = ptval->filtertype;
		recalcfilter = true;
	}

	if (ptval->filtercutoff != paraFilterCutoff.NoValue)
	{
		pt->FilterCutoff = ptval->filtercutoff;
		recalcfilter = true;
	}

	if (ptval->filterbw != paraFilterBandwidth.NoValue)
	{
		pt->FilterBandwidth = ptval->filterbw;
		recalcfilter = true;
	}

	if (recalcfilter)
		SetFilter(*pt);

	if (ptval->unit != paraUnit.NoValue)
		pt->Unit = ptval->unit;

	if (ptval->length != paraLength.NoValue)
	{
		switch(pt->Unit)
		{
		case UNIT_MS:
			pt->Length = (int)(pMasterInfo->SamplesPerSec * (ptval->length / 1000.0));
			if (pt->Length < 1)
				pt->Length = 1;
			break;
		case UNIT_SAMPLE:
			pt->Length = ptval->length;
			break;
		case UNIT_TICK:
			pt->Length = pMasterInfo->SamplesPerTick * ptval->length;
			break;
		case UNIT_256:
			pt->Length = (pMasterInfo->SamplesPerTick * ptval->length) >> 8;
			if (pt->Length < 1)
				pt->Length = 1;
			break;
		}

	}

	if (pt->Length > MaxDelay)
	{
		pt->Length = MaxDelay;
	}
	

	if (pt->Pos >= pt->Length)
		pt->Pos = 0;

	if (ptval->feedback != paraFeedback.NoValue)
	{
		pt->Feedback = (float)((ptval->feedback - 64) * (1.0 / 64.0)); 
	}

	if (ptval->wetout != paraWetOut.NoValue)
		pt->WetOut = (float)(ptval->wetout * (1.0 / 128.0));
	
}

void mi::Tick()
{
	for (int c = 0; c < numTracks; c++)
		TickTrack(Tracks + c, tval+c);


	// find max delay time slow we know when to stop wasting CPU time
	int maxdt = 0;
	for (int c = 0; c < numTracks; c++)
	{
		int dt = Tracks[c].Length + (int)(SilentEnough / log(fabs(Tracks[c].Feedback)) * Tracks[c].Length); 
		if (dt > maxdt)
			maxdt = dt;
	}
	
	DelayTime = maxdt;


	
	if (gval.drythru != SWITCH_NO)
		DryThru = gval.drythru != 0;
}



#pragma optimize ("a", on) 

static void DoWork(float *pin, float *pout, float *pbuf, int c, double const wetout, double const feedback, CBWState &s)
{
	do
	{

		double delay = *pbuf;
		
		double const o = s.a[0] * delay + s.a[1] * s.i[0] + s.a[2] * s.i[1] - s.a[3] * s.o[0] - s.a[4] * s.o[1];
		s.o[1] = s.o[0];
		s.o[0] = (float)o;
		s.i[1] = s.i[0];
		s.i[0] = (float)delay;

		*pbuf++ = (float)(feedback * o + *pin++);
		*pout++ += (float)(o * wetout);

	} while(--c);
}

static void DoWorkNoFB(float *pin, float *pout, float *pbuf, int c, double const wetout, CBWState &s)
{
	do
	{

		double delay = *pbuf;

		double const o = s.a[0] * delay + s.a[1] * s.i[0] + s.a[2] * s.i[1] - s.a[3] * s.o[0] - s.a[4] * s.o[1];
		s.o[1] = s.o[0];
		s.o[0] = (float)o;
		s.i[1] = s.i[0];
		s.i[0] = (float)delay;
		
		*pbuf++ = (float)(*pin++);
		*pout++ += (float)(o * wetout);

	} while(--c);
}

static void DoWorkNoInput(float *pout, float *pbuf, int c, double const wetout, double const feedback, CBWState &s)
{
	do
	{
		double delay = *pbuf;

		double const o = s.a[0] * delay + s.a[1] * s.i[0] + s.a[2] * s.i[1] - s.a[3] * s.o[0] - s.a[4] * s.o[1];
		s.o[1] = s.o[0];
		s.o[0] = (float)o;
		s.i[1] = s.i[0];
		s.i[0] = (float)delay;
		
		*pbuf++ = (float)(feedback * o);
		*pout++ += (float)(o * wetout);

	} while(--c);
}



static void DoWorkNoInputNoFB(float *pout, float *pbuf, int c, double const wetout, CBWState &s)
{

	do
	{
		double delay = *pbuf;

		double const o = s.a[0] * delay + s.a[1] * s.i[0] + s.a[2] * s.i[1] - s.a[3] * s.o[0] - s.a[4] * s.o[1];
		s.o[1] = s.o[0];
		s.o[0] = (float)o;
		s.i[1] = s.i[0];
		s.i[0] = (float)delay;
		
		*pbuf++ = 0;
		*pout++ += (float)(o * wetout);
	} while(--c);
}

static void DoWorkNoInputNoOutput(float *pbuf, int c, double const feedback, CBWState &s)
{
	do
	{
		double delay = *pbuf;

		double const o = s.a[0] * delay + s.a[1] * s.i[0] + s.a[2] * s.i[1] - s.a[3] * s.o[0] - s.a[4] * s.o[1];
		s.o[1] = s.o[0];
		s.o[0] = (float)o;
		s.i[1] = s.i[0];
		s.i[0] = (float)delay;

		*pbuf = (float)(o * feedback);
		pbuf++;
	} while(--c);
}

static void DoWorkNoOutput(float *pin, float *pbuf, int c, double const feedback, CBWState &s)
{
	do
	{

		double delay = *pbuf;

		double const o = s.a[0] * delay + s.a[1] * s.i[0] + s.a[2] * s.i[1] - s.a[3] * s.o[0] - s.a[4] * s.o[1];
		s.o[1] = s.o[0];
		s.o[0] = (float)o;
		s.i[1] = s.i[0];
		s.i[0] = (float)delay;
		
		double dry = *pin++;
		*pbuf++ = (float)(feedback * o + dry);

	} while(--c);
}

static void DoWorkNoOutputNoFB(float *psamples, float *pbuf, int c)
{
	memcpy(pbuf, psamples, c*4);
}


#pragma optimize ("", on)


void mi::WorkTrack(CTrack *pt, float *pin, float *pout, int numsamples, int const mode)
{
	do
	{
		int count = __min(numsamples, pt->Length - pt->Pos);

		if (count > 0)
		{
			if (mode == WM_NOIO)
			{
				if (pt->Feedback != 0)
					DoWorkNoInputNoOutput(pt->Buffer + pt->Pos, count, pt->Feedback, pt->Filter);
			}
			else if (mode == WM_WRITE)
			{
				if (pt->Feedback != 0)
					DoWorkNoInput(pout, pt->Buffer + pt->Pos, count, pt->WetOut, pt->Feedback, pt->Filter);
				else
					DoWorkNoInputNoFB(pout, pt->Buffer + pt->Pos, count, pt->WetOut, pt->Filter);
			}
			else if (mode == WM_READ)
			{
				if (pt->Feedback != 0)
					DoWorkNoOutput(pin, pt->Buffer + pt->Pos, count, pt->Feedback, pt->Filter);
				else
					DoWorkNoOutputNoFB(pin, pt->Buffer + pt->Pos, count);

			}
			else
			{
				if (pt->Feedback != 0)
					DoWork(pin, pout, pt->Buffer + pt->Pos, count, pt->WetOut, pt->Feedback, pt->Filter);
				else
					DoWorkNoFB(pin, pout, pt->Buffer + pt->Pos, count, pt->WetOut, pt->Filter);
			}
			
			pin += count;
			pout += count;
			numsamples -= count;
			pt->Pos += count;
		} 

		if (pt->Pos == pt->Length)
			pt->Pos = 0;

	} while(numsamples > 0);

}

bool mi::Work(float *psamples, int numsamples, int const mode)
{
	if (mode & WM_READ)
	{
		IdleMode = false;
		IdleCount = 0;
	}
	else
	{
		if (IdleMode)
		{
			return false;
		}
		else
		{
			IdleCount += numsamples;
			if (IdleCount >= DelayTime + MAX_BUFFER_LENGTH)
			{
				// clear all buffers
				for (int c = 0; c < numTracks; c++)
					memset(Tracks[c].Buffer, 0, Tracks[c].Length*4);
				IdleMode = true;
			}
		}
	}

	float *paux = pCB->GetAuxBuffer();

	if (mode & WM_READ)
		memcpy(paux, psamples, numsamples*4);

	if (!DryThru || !(mode & WM_READ))
		memset(psamples, 0, numsamples*4);

	for (int c = 0; c < numTracks; c++)
		WorkTrack(Tracks + c, paux, psamples, numsamples, mode);

	return true;
}


