/*
	Runtime calibration profile for CompositeColorDAC.

	The profile stores picture controls, DAC levels and small timing offsets.  It
	intentionally leaves the active resolution and PAL/NTSC standard untouched so
	a malformed saved profile cannot change framebuffer dimensions.
*/
#pragma once

#include <stdint.h>
#include <string.h>

struct CompositeColorCalibration
{
	static const uint32_t MAGIC = 0x314C4143UL; // CAL1
	static const uint16_t LEGACY_VERSION = 1;
	static const uint16_t LOW_BURST_VERSION = 2;
	static const uint16_t VERSION = 3;
	static const uint32_t FLAG_VOLTAGE_DIVIDER = 0x00000001UL;

	uint32_t magic;
	uint16_t version;
	uint16_t size;
	uint32_t flags;

	// Picture controls. Percent values use 100 as neutral.
	int16_t brightness;
	int16_t contrast;
	int16_t saturation;
	int16_t hueDegrees;
	int16_t redGain;
	int16_t greenGain;
	int16_t blueGain;
	int16_t gammaX100;

	// Raw 8-bit DAC codes.
	uint8_t syncLevel;
	uint8_t blankingLevel;
	uint8_t blackLevel;
	uint8_t whiteLevel;
	uint8_t clipMin;
	uint8_t clipMax;
	uint8_t burstAmplitude;
	uint8_t lineBufferCount;

	// Chroma phase and active-picture alignment.
	int16_t burstPhaseDegrees;
	int16_t activePhaseDegrees;
	int16_t activeStartSamples;

	// Bounded offsets from the selected ModeComposite timing.
	int16_t hFrontAdjust;
	int16_t hSyncAdjust;
	int16_t hBackAdjust;
	int16_t burstStartAdjust;
	int16_t burstLengthAdjust;
	int16_t vFrontAdjust;
	int16_t vPreEqualizingAdjust;
	int16_t vSyncAdjust;
	int16_t vPostEqualizingAdjust;
	int16_t vBackAdjust;
	int32_t pixelClockPpm;
	int32_t colorClockPpm;

	static int32_t clampValue(int32_t value, int32_t minimum, int32_t maximum)
	{
		return value < minimum ? minimum : (value > maximum ? maximum : value);
	}

	static CompositeColorCalibration defaults(bool voltageDivider)
	{
		CompositeColorCalibration profile;
		memset(&profile, 0, sizeof(profile));
		profile.magic = MAGIC;
		profile.version = VERSION;
		profile.size = sizeof(profile);
		profile.flags = voltageDivider ? FLAG_VOLTAGE_DIVIDER : 0;
		profile.contrast = 100;
		profile.saturation = 100;
		profile.redGain = 100;
		profile.greenGain = 100;
		profile.blueGain = 100;
		profile.gammaX100 = 100;
		profile.lineBufferCount = 4;

		if (voltageDivider)
		{
			// bitluni's documented 100/150 ohm divider profile.
			profile.syncLevel = 0;
			profile.blankingLevel = 62;
			profile.blackLevel = 62;
			profile.whiteLevel = 207;
			profile.clipMin = 14;
			profile.clipMax = 255;
			profile.burstAmplitude = 31;
		}
		else
		{
			// Direct DAC connection profile from the original library comments.
			profile.syncLevel = 0;
			profile.blankingLevel = 23;
			profile.blackLevel = 23;
			profile.whiteLevel = 77;
			profile.clipMin = 5;
			profile.clipMax = 95;
			profile.burstAmplitude = 11;
		}
		return profile;
	}

	bool hasValidHeader() const
	{
		return magic == MAGIC && version == VERSION && size == sizeof(CompositeColorCalibration);
	}

	bool hasCompatibleHeader() const
	{
		return magic == MAGIC &&
		       (version == VERSION || version == LOW_BURST_VERSION ||
		        version == LEGACY_VERSION) &&
		       size == sizeof(CompositeColorCalibration);
	}

	void migrateLegacyCarrierLevel()
	{
		if (magic != MAGIC ||
		    (version != LEGACY_VERSION && version != LOW_BURST_VERSION) ||
		    size != sizeof(CompositeColorCalibration)) return;
		if (version == LOW_BURST_VERSION)
		{
			if ((flags & FLAG_VOLTAGE_DIVIDER) != 0)
				burstAmplitude = (uint8_t)(((uint16_t)burstAmplitude * 31 + 9) / 18);
			else
				burstAmplitude = (uint8_t)(((uint16_t)burstAmplitude * 11 + 3) / 7);
		}
		version = VERSION;
	}

	void sanitize()
	{
		magic = MAGIC;
		version = VERSION;
		size = sizeof(CompositeColorCalibration);
		brightness = clampValue(brightness, -128, 127);
		contrast = clampValue(contrast, 0, 300);
		saturation = clampValue(saturation, 0, 300);
		hueDegrees = clampValue(hueDegrees, -180, 180);
		redGain = clampValue(redGain, 0, 200);
		greenGain = clampValue(greenGain, 0, 200);
		blueGain = clampValue(blueGain, 0, 200);
		gammaX100 = clampValue(gammaX100, 50, 250);
		burstAmplitude = clampValue(burstAmplitude, 0, 127);
		lineBufferCount = clampValue(lineBufferCount, 1, 16);
		burstPhaseDegrees = clampValue(burstPhaseDegrees, -180, 180);
		activePhaseDegrees = clampValue(activePhaseDegrees, -180, 180);
		activeStartSamples = clampValue(activeStartSamples, -24, 24);
		hFrontAdjust = clampValue(hFrontAdjust, -24, 24);
		hSyncAdjust = clampValue(hSyncAdjust, -24, 24);
		hBackAdjust = clampValue(hBackAdjust, -24, 24);
		burstStartAdjust = clampValue(burstStartAdjust, -24, 24);
		burstLengthAdjust = clampValue(burstLengthAdjust, -24, 24);
		vFrontAdjust = clampValue(vFrontAdjust, -4, 4);
		vPreEqualizingAdjust = clampValue(vPreEqualizingAdjust, -4, 4);
		vSyncAdjust = clampValue(vSyncAdjust, -4, 4);
		vPostEqualizingAdjust = clampValue(vPostEqualizingAdjust, -4, 4);
		vBackAdjust = clampValue(vBackAdjust, -8, 8);
		pixelClockPpm = clampValue(pixelClockPpm, -50000, 50000);
		colorClockPpm = clampValue(colorClockPpm, -10000, 10000);
		if (clipMin > clipMax)
		{
			uint8_t temporary = clipMin;
			clipMin = clipMax;
			clipMax = temporary;
		}
	}

	uint32_t signature() const
	{
		uint32_t hash = 2166136261UL;
		#define COMPOSITE_CAL_MIX(field) do { \
			uint64_t value = (uint64_t)(int64_t)(field); \
			for (size_t byteIndex = 0; byteIndex < sizeof(field); ++byteIndex) { \
				hash ^= (uint8_t)(value >> (byteIndex * 8)); \
				hash *= 16777619UL; \
			} \
		} while (0)
		COMPOSITE_CAL_MIX(magic);
		COMPOSITE_CAL_MIX(version);
		COMPOSITE_CAL_MIX(size);
		COMPOSITE_CAL_MIX(flags);
		COMPOSITE_CAL_MIX(brightness);
		COMPOSITE_CAL_MIX(contrast);
		COMPOSITE_CAL_MIX(saturation);
		COMPOSITE_CAL_MIX(hueDegrees);
		COMPOSITE_CAL_MIX(redGain);
		COMPOSITE_CAL_MIX(greenGain);
		COMPOSITE_CAL_MIX(blueGain);
		COMPOSITE_CAL_MIX(gammaX100);
		COMPOSITE_CAL_MIX(syncLevel);
		COMPOSITE_CAL_MIX(blankingLevel);
		COMPOSITE_CAL_MIX(blackLevel);
		COMPOSITE_CAL_MIX(whiteLevel);
		COMPOSITE_CAL_MIX(clipMin);
		COMPOSITE_CAL_MIX(clipMax);
		COMPOSITE_CAL_MIX(burstAmplitude);
		COMPOSITE_CAL_MIX(lineBufferCount);
		COMPOSITE_CAL_MIX(burstPhaseDegrees);
		COMPOSITE_CAL_MIX(activePhaseDegrees);
		COMPOSITE_CAL_MIX(activeStartSamples);
		COMPOSITE_CAL_MIX(hFrontAdjust);
		COMPOSITE_CAL_MIX(hSyncAdjust);
		COMPOSITE_CAL_MIX(hBackAdjust);
		COMPOSITE_CAL_MIX(burstStartAdjust);
		COMPOSITE_CAL_MIX(burstLengthAdjust);
		COMPOSITE_CAL_MIX(vFrontAdjust);
		COMPOSITE_CAL_MIX(vPreEqualizingAdjust);
		COMPOSITE_CAL_MIX(vSyncAdjust);
		COMPOSITE_CAL_MIX(vPostEqualizingAdjust);
		COMPOSITE_CAL_MIX(vBackAdjust);
		COMPOSITE_CAL_MIX(pixelClockPpm);
		COMPOSITE_CAL_MIX(colorClockPpm);
		#undef COMPOSITE_CAL_MIX
		return hash;
	}
};
