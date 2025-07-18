// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $HDRTYPE && defined $HDRENABLED && !$HDRTYPE && $HDRENABLED
// defined $PIXELFOGTYPE && defined $WRITEWATERFOGTODESTALPHA && ( $PIXELFOGTYPE != 1 ) && $WRITEWATERFOGTODESTALPHA
// defined $LIGHTING_PREVIEW && defined $HDRTYPE && $LIGHTING_PREVIEW && $HDRTYPE != 0
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPTINT && $LIGHTING_PREVIEW && $FASTPATHENVMAPTINT
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPCONTRAST && $LIGHTING_PREVIEW && $FASTPATHENVMAPCONTRAST
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH
// ($FLASHLIGHT || $FLASHLIGHTSHADOWS) && $LIGHTING_PREVIEW

#pragma once
#include "shaderlib/cshader.h"
class blurfilter_ps20b_Static_Index
{
	unsigned int m_nCONVERT_TO_SRGB : 2;
	unsigned int m_nAPPROX_SRGB_ADAPTER : 2;
#ifdef _DEBUG
	bool m_bAPPROX_SRGB_ADAPTER : 1;
#endif	// _DEBUG
public:
	void SetCONVERT_TO_SRGB( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCONVERT_TO_SRGB = i;
	}

	void SetAPPROX_SRGB_ADAPTER( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nAPPROX_SRGB_ADAPTER = i;
#ifdef _DEBUG
		m_bAPPROX_SRGB_ADAPTER = true;
#endif	// _DEBUG
	}

	blurfilter_ps20b_Static_Index(  )
	{
		m_nCONVERT_TO_SRGB = g_pHardwareConfig->NeedsShaderSRGBConversion();
		m_nAPPROX_SRGB_ADAPTER = 0;
#ifdef _DEBUG
		m_bAPPROX_SRGB_ADAPTER = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bAPPROX_SRGB_ADAPTER );
		return ( 1 * m_nCONVERT_TO_SRGB ) + ( 2 * m_nAPPROX_SRGB_ADAPTER ) + 0;
	}
};

#define shaderStaticTest_blurfilter_ps20b psh_forgot_to_set_static_APPROX_SRGB_ADAPTER


class blurfilter_ps20b_Dynamic_Index
{
public:
	blurfilter_ps20b_Dynamic_Index(  )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_blurfilter_ps20b 1

