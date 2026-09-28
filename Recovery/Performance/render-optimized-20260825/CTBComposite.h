/*
	Author: Martin-Laclaustra 2020
	License: 
	Creative Commons Attribution ShareAlike 4.0
	https://creativecommons.org/licenses/by-sa/4.0/
	
	For further details check out: 
		https://github.com/bitluni
*/
#pragma once

#include "../integertrigonometry.h"
#include "../Colors/InterfaceColors_ColorR8G8B8A8.h"
#include "../../Composite/CompositeColorCalibration.h"
#include <math.h>

class CTBComposite
{
	public:
	CTBComposite()
	{
		for (int i = 0; i < 256; ++i)
			gammaLUT[i] = (uint8_t)i;
		rebuildIndexedChannelLUTs();
	}

	int levelHighClipping = 255;
	int levelWhite = 207;
	int amplitudeBurst = 31;
	int levelBlack = 62;
	int levelBlanking = 62;
	int levelLowClipping = 14;
	int levelSync = 0;

	//int levelHighClipping = 95;
	//int levelWhite = 77;
	//int amplitudeBurst = 11;
	//int levelBlack = 23;
	//int levelBlanking = 23;
	//int levelLowClipping = 5;
	//int levelSync = 0;

	int firstPixelOffset = 0; // falling edge of hSync
	uint32_t colorClock0x1000Periods = 1; // pixels per 0x1000 color cycles
	int bufferVDiv = 1;
	bool bufferInterlaced = false;
	bool bufferPhaseAlternating = false;

	int pictureBrightness = 0;
	int pictureContrast = 100;
	int pictureSaturation = 100;
	int pictureHueDegrees = 0;
	int pictureRedGain = 100;
	int pictureGreenGain = 100;
	int pictureBlueGain = 100;
	int pictureGammaX100 = 100;
	int activePhaseDegrees = 0;
	uint8_t gammaLUT[256];
	bool indexedColorMode = false;
	uint8_t indexedRedLUT[256];
	uint8_t indexedGreenLUT[256];
	uint8_t indexedBlueLUT[256];
	uint32_t indexedCacheInput[4] = {0, 0, 0, 0};
	uint8_t indexedCacheOutput[4] = {0, 0, 0, 0};
	uint8_t indexedCacheValidMask = 0;

	void setIndexedColorMode(bool enabled)
	{
		indexedColorMode = enabled;
		indexedCacheValidMask = 0;
	}

	static uint8_t indexedColor(int r, int g, int b)
	{
		int maximum = r > g ? (r > b ? r : b) : (g > b ? g : b);
		int minimum = r < g ? (r < b ? r : b) : (g < b ? g : b);
		if (maximum - minimum <= 6)
		{
			int gray = (19595L * r + 38470L * g + 7471L * b + 0x8000L) >> 16;
			return (uint8_t)((gray * 31 + 127) / 255);
		}

		// 32 neutral grays plus a 7x8x4 colour cube. Neutral entries are
		// deliberately separate so luma-only UI pixels never carry chroma.
		int ri = (r * 6 + 127) / 255;
		int gi = (g * 7 + 127) / 255;
		int bi = (b * 3 + 127) / 255;
		return (uint8_t)(32 + ((ri * 8 + gi) * 4 + bi));
	}

	static void indexedRGB(uint8_t index, int &r, int &g, int &b)
	{
		if (index < 32)
		{
			int gray = (index * 255 + 15) / 31;
			r = gray;
			g = gray;
			b = gray;
			return;
		}

		int cube = index - 32;
		int bi = cube & 3;
		int gi = (cube >> 2) & 7;
		int ri = cube >> 5;
		r = (ri * 255 + 3) / 6;
		g = (gi * 255 + 3) / 7;
		b = (bi * 255 + 1) / 3;
	}

	void applyPictureCalibration(const CompositeColorCalibration &profile)
	{
		pictureBrightness = profile.brightness;
		pictureContrast = profile.contrast;
		pictureSaturation = profile.saturation;
		pictureHueDegrees = profile.hueDegrees;
		pictureRedGain = profile.redGain;
		pictureGreenGain = profile.greenGain;
		pictureBlueGain = profile.blueGain;
		pictureGammaX100 = profile.gammaX100;
		activePhaseDegrees = profile.activePhaseDegrees;

		if (pictureGammaX100 == 100)
		{
			for (int i = 0; i < 256; ++i)
				gammaLUT[i] = (uint8_t)i;
		}
		else
		{
			const float exponent = 100.0f / (float)pictureGammaX100;
			for (int i = 0; i < 256; ++i)
			{
				float normalized = (float)i / 255.0f;
				int corrected = (int)(powf(normalized, exponent) * 255.0f + 0.5f);
				gammaLUT[i] = (uint8_t)(corrected < 0 ? 0 : (corrected > 255 ? 255 : corrected));
			}
		}

		rebuildIndexedChannelLUTs();
		indexedCacheValidMask = 0;
	}

	int calibratedChannel(uint8_t value, int gain) const
	{
		int adjusted = ((int)value * gain + 50) / 100;
		if (adjusted < 0) adjusted = 0;
		if (adjusted > 255) adjusted = 255;
		return gammaLUT[adjusted];
	}

	void rebuildIndexedChannelLUTs()
	{
		for (int i = 0; i < 256; ++i)
		{
			indexedRedLUT[i] = (uint8_t)calibratedChannel((uint8_t)i, pictureRedGain);
			indexedGreenLUT[i] = (uint8_t)calibratedChannel((uint8_t)i, pictureGreenGain);
			indexedBlueLUT[i] = (uint8_t)calibratedChannel((uint8_t)i, pictureBlueGain);
		}
	}

	int coltobuf(int val, int x, int y)
	{
		if (indexedColorMode)
		{
			uint32_t rgb = (uint32_t)val & 0x00ffffffUL;
			uint8_t cacheSlot = (uint8_t)((rgb ^ (rgb >> 7) ^ (rgb >> 15)) & 3);
			uint8_t cacheBit = (uint8_t)(1U << cacheSlot);
			if ((indexedCacheValidMask & cacheBit) &&
			    indexedCacheInput[cacheSlot] == rgb)
				return indexedCacheOutput[cacheSlot];

			int r = indexedRedLUT[ColorR8G8B8A8::static_R(val)];
			int g = indexedGreenLUT[ColorR8G8B8A8::static_G(val)];
			int b = indexedBlueLUT[ColorR8G8B8A8::static_B(val)];
			uint8_t encoded = indexedColor(r, g, b);
			indexedCacheInput[cacheSlot] = rgb;
			indexedCacheOutput[cacheSlot] = encoded;
			indexedCacheValidMask |= cacheBit;
			return encoded;
		}

		int32_t r = calibratedChannel(ColorR8G8B8A8::static_R(val), pictureRedGain);
		int32_t g = calibratedChannel(ColorR8G8B8A8::static_G(val), pictureGreenGain);
		int32_t b = calibratedChannel(ColorR8G8B8A8::static_B(val), pictureBlueGain);

		int32_t sourceLuma = (19595L * r + 38470L * g + 7471L * b + 0x8000L) >> 16;
		int32_t luma = 128 + ((sourceLuma - 128) * pictureContrast) / 100 + pictureBrightness;
		if (luma < 0) luma = 0;
		if (luma > 255) luma = 255;

		int32_t blueDifference = b - sourceLuma;
		int32_t redDifference = r - sourceLuma;
		int32_t b_y = (32251L * blueDifference + (blueDifference >= 0 ? 0x8000L : -0x8000L)) / 0x10000L;
		int32_t r_y = (57494L * redDifference + (redDifference >= 0 ? 0x8000L : -0x8000L)) / 0x10000L;
		int32_t absB = b_y < 0 ? -b_y : b_y;
		int32_t absR = r_y < 0 ? -r_y : r_y;
		int32_t chroma = (absB > absR)
			? (15 * ((absB << 1) + absR)) >> 5
			: (15 * (absB + (absR << 1))) >> 5;
		chroma = (chroma * pictureSaturation + 50) / 100;

		int32_t huePhase = (integeratan2aprox(r_y, b_y) >> 8) + (pictureHueDegrees * 256) / 360;
		int32_t positionPhase = (activePhaseDegrees * 256) / 360;
		if (colorClock0x1000Periods > 0)
		{
			int64_t cycle = (((int64_t)x + firstPixelOffset) << 12) % colorClock0x1000Periods;
			if (cycle < 0) cycle += colorClock0x1000Periods;
			positionPhase += (int32_t)((cycle << 8) / colorClock0x1000Periods);
		}

		int32_t encodedLuma = luma;
		if (!bufferPhaseAlternating && colorClock0x1000Periods > 0)
		{
			// NTSC keeps its conventional 7.5 IRE setup pedestal.
			encodedLuma = (15154L * luma + 313344L + 8192L) >> 14;
			if (encodedLuma > 255) encodedLuma = 255;
		}

		const int32_t lumaSpan = levelWhite - levelBlack;
		int64_t mappedLuma = (int64_t)encodedLuma * lumaSpan;
		mappedLuma += mappedLuma >= 0 ? 127 : -127;
		int32_t output = levelBlack + (int32_t)(mappedLuma / 255);
		if (output > levelHighClipping) output = levelHighClipping;
		if (output < levelLowClipping) output = levelLowClipping;

		if (colorClock0x1000Periods > 0 && chroma > 0 && amplitudeBurst > 0)
		{
			int32_t carrierPhase = huePhase + positionPhase;
			int32_t standardScale = 118;
			if (bufferPhaseAlternating)
			{
				carrierPhase = ((y & 1) ? huePhase : -huePhase) + positionPhase;
				standardScale = 128;
			}

			// Keep active chroma referenced to the burst instead of scaling it by
			// the full luma range. Rail-clipped carrier samples leak into luma as
			// the black zigzag columns visible on low-rate composite modes.
			int64_t amplitudeNumerator =
				(int64_t)chroma * amplitudeBurst * standardScale;
			int32_t chromaAmplitude =
				(int32_t)((amplitudeNumerator + 8192) / 16384);
			int32_t modulationHeadroom = output - levelLowClipping;
			int32_t upperHeadroom = levelHighClipping - output;
			if (upperHeadroom < modulationHeadroom) modulationHeadroom = upperHeadroom;
			if (modulationHeadroom < 0) modulationHeadroom = 0;
			if (chromaAmplitude > modulationHeadroom) chromaAmplitude = modulationHeadroom;

			int64_t modulationNumerator =
				(int64_t)chromaAmplitude * integersinaprox(carrierPhase);
			modulationNumerator += modulationNumerator >= 0 ? 63 : -63;
			int32_t modulation = (int32_t)(modulationNumerator / 127);
			if (lumaSpan < 0) modulation = -modulation;
			output += modulation;
		}

		if (output > levelHighClipping) output = levelHighClipping;
		if (output < levelLowClipping) output = levelLowClipping;
		return output;
	}
	int buftocol(int val) const
	{
		if (!indexedColorMode)
			return 0;
		int r, g, b;
		indexedRGB((uint8_t)val, r, g, b);
		return ColorR8G8B8A8::static_RGBA(r, g, b);
	}
};
