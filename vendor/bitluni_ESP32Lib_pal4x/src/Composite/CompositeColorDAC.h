/*
	Author: Martin-Laclaustra 2020
	License: 
	Creative Commons Attribution ShareAlike 4.0
	https://creativecommons.org/licenses/by-sa/4.0/
	
	For further details check out: 
		https://github.com/bitluni
*/

/*
	CONNECTION
	
	A) voltageDivider = false; B) voltageDivider = true
	
	   55 shades                  145 shades
	
	ESP32        TV           ESP32                       TV     
	-----+                     -----+    ____ 100 ohm
	    G|-                        G|---|____|+          
	pin25|--------- Comp       pin25|---|____|+--------- Comp    
	pin26|-                    pin26|-        150 ohm
	     |                          |
	     |                          |
	-----+                     -----+                              
	
	Connect pin 25 or 26
*/
#pragma once
#include "Composite.h"
#include "CompositeColorCalibration.h"
#include "../Graphics/GraphicsX8CA8Swapped.h"
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <Preferences.h>
#endif


class CompositeColorDAC : public Composite, public GraphicsX8CA8Swapped
{
  public:
	CompositeColorDAC() //DAC based modes only work with I2S0
		: Composite(0)
	{
		calibration = CompositeColorCalibration::defaults(true);
		applyCalibrationParameters(false);
		vBlankLineBuffer[0] = 0;
		vBlankLineBuffer[1] = 0;
		normalFrontLineBuffer = 0;
		equalizingLineBuffer = 0;
		vSyncLineBuffer = 0;
		normalBackLineBuffer = 0;
	}

	CompositeColorCalibration calibration;

	const CompositeColorCalibration &getCalibration() const
	{
		return calibration;
	}

	void setCalibration(const CompositeColorCalibration &profile)
	{
		calibration = profile;
		calibration.migrateLegacyCarrierLevel();
		calibration.sanitize();
		calibrationConfigured = true;
		applyCalibrationParameters(false);
	}

	void resetCalibration(bool useVoltageDivider)
	{
		calibration = CompositeColorCalibration::defaults(useVoltageDivider);
		calibrationConfigured = true;
		applyCalibrationParameters(false);
	}

	void applyCalibrationParameters(bool markConfigured = true)
	{
		calibration.sanitize();
		if (markConfigured) calibrationConfigured = true;
		levelSync = calibration.syncLevel;
		levelBlanking = calibration.blankingLevel;
		levelBlack = calibration.blackLevel;
		levelWhite = calibration.whiteLevel;
		levelLowClipping = calibration.clipMin;
		levelHighClipping = calibration.clipMax;
		amplitudeBurst = calibration.burstAmplitude;
		lineBufferCount = calibration.lineBufferCount;
		applyPictureCalibration(calibration);
		if (pal4xEnabled)
			rebuildPAL4xTables();
	}

	void setCalibrationAutoLoad(bool enabled)
	{
		autoLoadCalibration = enabled;
	}

	bool loadCalibration()
	{
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
		Preferences preferences;
		calibrationStorageChecked = true;
		if (!preferences.begin("cit_video", true)) return false;
		size_t storedSize = preferences.getBytesLength("profile");
		if (storedSize != sizeof(CompositeColorCalibration))
		{
			preferences.end();
			return false;
		}
		CompositeColorCalibration stored;
		memset(&stored, 0, sizeof(stored));
		bool read = preferences.getBytes("profile", &stored, sizeof(stored)) == sizeof(stored);
		preferences.end();
		if (!read || !stored.hasCompatibleHeader()) return false;
		setCalibration(stored);
		calibrationLoadedFromStorage = true;
		return true;
#else
		calibrationStorageChecked = true;
		return false;
#endif
	}

	bool saveCalibration()
	{
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
		calibration.sanitize();
		Preferences preferences;
		if (!preferences.begin("cit_video", false)) return false;
		bool written = preferences.putBytes("profile", &calibration, sizeof(calibration)) == sizeof(calibration);
		preferences.end();
		if (written)
		{
			calibrationStorageChecked = true;
			calibrationLoadedFromStorage = true;
		}
		return written;
#else
		return false;
#endif
	}

	bool clearSavedCalibration()
	{
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
		Preferences preferences;
		if (!preferences.begin("cit_video", false)) return false;
		bool removed = preferences.remove("profile");
		preferences.end();
		calibrationLoadedFromStorage = false;
		return removed;
#else
		return false;
#endif
	}

	bool hasStoredCalibration() const
	{
		return calibrationLoadedFromStorage;
	}

	uint32_t calibrationSignature() const
	{
		return calibration.signature();
	}

	static bool requiresSignalRestart(const CompositeColorCalibration &before,
	                                  const CompositeColorCalibration &after)
	{
		return before.syncLevel != after.syncLevel ||
		       before.blankingLevel != after.blankingLevel ||
		       before.burstAmplitude != after.burstAmplitude ||
		       before.burstPhaseDegrees != after.burstPhaseDegrees ||
		       before.activeStartSamples != after.activeStartSamples ||
		       before.hFrontAdjust != after.hFrontAdjust ||
		       before.hSyncAdjust != after.hSyncAdjust ||
		       before.hBackAdjust != after.hBackAdjust ||
		       before.burstStartAdjust != after.burstStartAdjust ||
		       before.burstLengthAdjust != after.burstLengthAdjust ||
		       before.vFrontAdjust != after.vFrontAdjust ||
		       before.vPreEqualizingAdjust != after.vPreEqualizingAdjust ||
		       before.vSyncAdjust != after.vSyncAdjust ||
		       before.vPostEqualizingAdjust != after.vPostEqualizingAdjust ||
		       before.vBackAdjust != after.vBackAdjust ||
		       before.pixelClockPpm != after.pixelClockPpm ||
		       before.colorClockPpm != after.colorClockPpm;
	}

	bool init(const ModeComposite &mode, const int outputPin = 25, const bool voltageDivider = false)
	{
		int pinMap[16] = {
			-1, -1, 
			-1, -1, -1, -1, -1,
			-1, -1, -1, -1, -1,
			-1, -1, -1, -1
		};
		this->outputPin = outputPin;
		this->voltageDivider = voltageDivider;
		prepareCalibration(voltageDivider);
		ModeComposite adjustedMode = calibratedMode(mode);
		pal4xEnabled = supportsPAL4x(adjustedMode);
		pal4xLowMemoryMode = pal4xEnabled && adjustedMode.hRes == 400 &&
			adjustedMode.activeLineCount == 192;
		setIndexedColorMode(pal4xEnabled);
		defaultBufferValue = pal4xEnabled
			? ((int)indexedColor(0, 0, 0) << 8)
			: ((int)levelBlack << 8);
		firstPixelOffset = adjustedMode.hSync + adjustedMode.hBack + calibration.activeStartSamples;
		colorClock0x1000Periods = adjustedMode.colorClock > 0
			? (uint32_t)((0x1000ULL * adjustedMode.pixelClock) / adjustedMode.colorClock)
			: 0;
		bufferVDiv = adjustedMode.vDiv;
		bufferInterlaced = adjustedMode.interlaced;
		bufferPhaseAlternating = adjustedMode.phaseAlternating;
		return initDAC(adjustedMode, pinMap, 16, -1);
	}

	bool init(const ModeComposite &mode, const PinConfigComposite &pinConfig)
	{
		int pinMap[16] = {
			-1, -1, 
			-1, -1, -1, -1, -1,
			-1, -1, -1, -1, -1,
			-1, -1, -1, -1
		};
		prepareCalibration(false);
		ModeComposite adjustedMode = calibratedMode(mode);
		pal4xEnabled = supportsPAL4x(adjustedMode);
		pal4xLowMemoryMode = pal4xEnabled && adjustedMode.hRes == 400 &&
			adjustedMode.activeLineCount == 192;
		setIndexedColorMode(pal4xEnabled);
		defaultBufferValue = pal4xEnabled
			? ((int)indexedColor(0, 0, 0) << 8)
			: ((int)levelBlack << 8);
		firstPixelOffset = adjustedMode.hSync + adjustedMode.hBack + calibration.activeStartSamples;
		colorClock0x1000Periods = adjustedMode.colorClock > 0
			? (uint32_t)((0x1000ULL * adjustedMode.pixelClock) / adjustedMode.colorClock)
			: 0;
		bufferVDiv = adjustedMode.vDiv;
		bufferInterlaced = adjustedMode.interlaced;
		bufferPhaseAlternating = adjustedMode.phaseAlternating;
		return initDAC(adjustedMode, pinMap, 16, -1);
	}

	ModeComposite calibratedMode(const ModeComposite &source) const
	{
		int hFront = source.hFront + calibration.hFrontAdjust;
		int hSync = source.hSync + calibration.hSyncAdjust;
		int hBack = source.hBack + calibration.hBackAdjust;
		if (hFront < 2) hFront = 2;
		if (hSync < 4) hSync = 4;
		if (hFront > hSync) hFront = hSync;
		if (hBack < 4) hBack = 4;
		if (((hFront + hSync + hBack + source.hRes) & 1) != 0) ++hBack;

		int burstStart = source.burstStart + calibration.burstStartAdjust;
		int burstLength = source.burstLength + calibration.burstLengthAdjust;
		if (burstStart < 0) burstStart = 0;
		if (burstStart >= hBack) burstStart = hBack - 1;
		if (burstLength < 0) burstLength = 0;
		if (burstStart + burstLength > hBack) burstLength = hBack - burstStart;

		int vFront = source.vFront + calibration.vFrontAdjust;
		int vPre = source.vPreEqHL + calibration.vPreEqualizingAdjust;
		int vSync = source.vSyncHL + calibration.vSyncAdjust;
		int vPost = source.vPostEqHL + calibration.vPostEqualizingAdjust;
		int vBack = source.vBack + calibration.vBackAdjust;
		if (vFront < 0) vFront = 0;
		if (vPre < 0) vPre = 0;
		if (vSync < 1) vSync = 1;
		if (vPost < 0) vPost = 0;
		if (vBack < 1) vBack = 1;
		if (!source.interlaced && ((vPre + vSync + vPost) & 1) != 0) ++vPost;

		uint32_t pixelClock = adjustedClock(source.pixelClock, calibration.pixelClockPpm);
		uint32_t colorClock = source.colorClock == 0
			? 0
			: adjustedClock(source.colorClock, calibration.colorClockPpm);

		return ModeComposite(
			hFront, hSync, hBack, source.hRes,
			vFront, vPre, vSync, vPost, vBack, source.vActive,
			source.vOPreRegHL, source.vOPostRegHL,
			source.vEPreRegHL, source.vEPostRegHL,
			source.vDiv, pixelClock, burstStart, burstLength,
			colorClock, source.phaseAlternating, source.aspect);
	}

	bool initDAC(const ModeComposite &mode, const int *pinMap, const int bitCount, const int clockPin)
	{
		this->mode = mode;
		if (pal4xEnabled)
			configurePAL4xTiming();
		int xres = pal4xLowMemoryMode ? 376 : mode.hRes;
		int yres = mode.vRes / mode.vDiv;
		propagateResolution(xres, yres);
		if(!frameBuffers[0] || !backBuffer)
			return false;
		for(int y = 0; y < yres; ++y)
			if(!frameBuffers[0][y])
				return false;
		totalLines = pal4xEnabled ? PAL4X_LINE_COUNT : mode.linesPerFrame;
		allocateLineBuffers();
		if(!dmaBufferDescriptors || !vBlankLineBuffer[0] || !vBlankLineBuffer[1] ||
		   (!pal4xEnabled && (!equalizingLineBuffer || !vSyncLineBuffer)))
			return false;
		currentLine = 0;
		vSyncPassed = false;
		interruptStaticChild = pal4xEnabled ? &CompositeColorDAC::interruptPAL4x : 0;
		long outputClock = pal4xEnabled ? pal4xSampleRate : mode.pixelClock;
		if (!initParallelOutputMode(pinMap, outputClock, bitCount, clockPin))
			return false;
		enableDAC(outputPin==25?1:2);
		startTX();
		return true;
	}

	virtual int bytesPerSample() const
	{
		return 2;
	}

	virtual float pixelAspect() const
	{
		return 1;
	}

	virtual void dotFast(int x, int y, Color color) override
	{
		writePixelFast(x, y, color);
	}

	virtual void dot(int x, int y, Color color) override
	{
		if ((unsigned int)x < (unsigned int)xres &&
		    (unsigned int)y < (unsigned int)yres)
			writePixelFast(x, y, color);
	}

	void fillRect(int x, int y, int w, int h, Color color)
	{
		if (!pal4xEnabled)
		{
			GraphicsX8CA8Swapped::fillRect(x, y, w, h, color);
			return;
		}
		if (!backBuffer || w <= 0 || h <= 0)
			return;
		if (x < 0) { w += x; x = 0; }
		if (y < 0) { h += y; y = 0; }
		if (x + w > xres) w = xres - x;
		if (y + h > yres) h = yres - y;
		if (w <= 0 || h <= 0)
			return;

		BufferUnit pixel = encodedPixel(color);
		for (int rowIndex = y; rowIndex < y + h; ++rowIndex)
			fillIndexedSpan(backBuffer[graphics_swy(rowIndex)], x, x + w, pixel);
	}

	virtual void xLine(int x0, int x1, int y, Color color) override
	{
		if (!pal4xEnabled)
		{
			GraphicsX8CA8Swapped::xLine(x0, x1, y, color);
			return;
		}
		if (!backBuffer || y < 0 || y >= yres)
			return;
		if (x0 > x1) { int swap = x0; x0 = x1; x1 = swap; }
		if (x0 < 0) x0 = 0;
		if (x1 > xres) x1 = xres;
		if (x0 >= x1)
			return;
		fillIndexedSpan(backBuffer[graphics_swy(y)], x0, x1, encodedPixel(color));
	}

	void rect(int x, int y, int w, int h, Color color)
	{
		if (!pal4xEnabled)
		{
			GraphicsX8CA8Swapped::rect(x, y, w, h, color);
			return;
		}
		if (w <= 0 || h <= 0)
			return;
		fillRect(x, y, w, 1, color);
		fillRect(x, y + h - 1, w, 1, color);
		fillRect(x, y + 1, 1, h - 2, color);
		fillRect(x + w - 1, y + 1, 1, h - 2, color);
	}

	virtual void clear(Color color = 0) override
	{
		if (pal4xEnabled)
			fillRect(0, 0, xres, yres, color);
		else
			GraphicsX8CA8Swapped::clear(color);
	}

	virtual void propagateResolution(const int xres, const int yres)
	{
		setResolution(xres, yres);
	}

	int outputPin = 25;
	bool voltageDivider = false;

	virtual BufferUnit **allocateFrameBuffer()
	{
		uint8_t initial = pal4xEnabled ? indexedColor(0, 0, 0) : levelBlanking;
		return (BufferUnit **)DMABufferDescriptor::allocateDMABufferArray(
			yres, xres * bytesPerSample(), true, 0x01000100UL * initial);
	}

	virtual void allocateLineBuffers()
	{
		if (pal4xEnabled)
			allocatePAL4xLineBuffers();
		else
			allocateLineBuffers((void **)frameBuffers[0]);
	}

	//void *hSyncLineBuffer[2];

	void *vBlankLineBuffer[2];

	//Vertical sync
	//4 possible Half Lines (HL):
	// NormalFront (NF), NormalBack (NB), Equalizing (EQ), Sync (SY)
	void *normalFrontLineBuffer;
	void *equalizingLineBuffer;
	void *vSyncLineBuffer;
	void *normalBackLineBuffer;

	//complete ring of buffer descriptors for one frame
	virtual void allocateLineBuffers(void **frameBuffer)
	{
		//lenght of each line
		int samples = mode.hFront + mode.hSync + mode.hBack + mode.hRes;
		int bytes = samples * bytesPerSample();
		int samplesHL = samples/2;
		int bytesHL = bytes/2;
		int samplesHSync = mode.hFront + mode.hSync + mode.hBack;
		int bytesHSync = samplesHSync * bytesPerSample();

		//create and fill the buffers with their default values

		//create the buffers
		//1 blank prototype line for vFront and vBack
		vBlankLineBuffer[0] = DMABufferDescriptor::allocateBuffer(bytes, true, 0x01000100*levelBlanking);
		if(!vBlankLineBuffer[0])
			return;
		if(mode.phaseAlternating)
		{
			vBlankLineBuffer[1] = DMABufferDescriptor::allocateBuffer(bytes, true, 0x01000100*levelBlanking);
			if(!vBlankLineBuffer[1])
				return;
		} else {
			vBlankLineBuffer[1] = vBlankLineBuffer[0];
		}
		//1 prototype for each HL type in vSync
		equalizingLineBuffer = DMABufferDescriptor::allocateBuffer(bytesHL, true, 0x01000100*levelBlanking);
		vSyncLineBuffer = DMABufferDescriptor::allocateBuffer(bytesHL, true, 0x01000100*levelBlanking);
		if(!equalizingLineBuffer || !vSyncLineBuffer)
			return;
		normalFrontLineBuffer = vBlankLineBuffer[0];
		normalBackLineBuffer = (void*)&(((uint8_t*)vBlankLineBuffer[0])[bytesHL]);
		////1 prototype for hSync
		//hSyncLineBuffer[0] = DMABufferDescriptor::allocateBuffer(bytesHSync, true, 0x01000100*levelBlanking);
		//if(mode.phaseAlternating)
		//{
			//hSyncLineBuffer[1] = DMABufferDescriptor::allocateBuffer(bytesHSync, true, 0x01000100*levelBlanking);
		//} else {
			//hSyncLineBuffer[1] = hSyncLineBuffer[0];
		//}
		//n lines as buffer for active lines
		//already allocated in allocateFrameBuffer

		//fill the buffers with their default values
		//(bytesPerSample() == 2)(actually only MSByte is used)
		for (int i = 0; i < samples; i++)
		{
			//hsync signal
			if (i >= mode.hFront && i < (mode.hFront + mode.hSync))
			{
				//blank line
				((unsigned short *)vBlankLineBuffer[0])[i ^ 1] = levelSync << 8;
				if(mode.phaseAlternating)
					((unsigned short *)vBlankLineBuffer[1])[i ^ 1] = levelSync << 8;
				////hsync
				//((unsigned short *)hSyncLineBuffer[0])[i ^ 1] = levelSync << 8;
				//if(mode.phaseAlternating)
					//((unsigned short *)hSyncLineBuffer[1])[i ^ 1] = levelSync << 8;
			}
			//color burst // pixel counting starts at the hsync pulse beginning
			if ( mode.colorClock > 0 &&
			     i >= (mode.hFront + mode.hSync + mode.burstStart) &&
			     i < (mode.hFront + mode.hSync + mode.burstStart + mode.burstLength)
			   )
			{
				if(mode.phaseAlternating==false)
				{
					//blank line
					((unsigned short *)vBlankLineBuffer[0])[i ^ 1] =
					   (unsigned short)clampDACLevel(levelBlanking + sin(((double)(i - mode.hFront)/((double)mode.pixelClock/(double)mode.colorClock))*(2*PI) + PI + burstPhaseRadians())*amplitudeBurst
									   ) << 8;
					////hsync
					//((unsigned short *)hSyncLineBuffer[0])[i ^ 1] =
					   //(unsigned short)(levelBlanking + sin(((double)(i - mode.hFront)/((double)mode.pixelClock/(double)mode.colorClock))*(2*PI) + PI)*amplitudeBurst
									   //) << 8;
				} else {
					//blank line
					((unsigned short *)vBlankLineBuffer[0])[i ^ 1] =
					   (unsigned short)clampDACLevel(levelBlanking + sin(((double)(i - mode.hFront)/((double)mode.pixelClock/(double)mode.colorClock))*(2*PI) + PI*3/4 + burstPhaseRadians())*amplitudeBurst
									   ) << 8;
					((unsigned short *)vBlankLineBuffer[1])[i ^ 1] =
					   (unsigned short)clampDACLevel(levelBlanking + sin(((double)(i - mode.hFront)/((double)mode.pixelClock/(double)mode.colorClock))*(2*PI) - PI*3/4 + burstPhaseRadians())*amplitudeBurst
									   ) << 8;
					////hsync
					//((unsigned short *)hSyncLineBuffer[0])[i ^ 1] =
					   //(unsigned short)(levelBlanking + sin(((double)(i - mode.hFront)/((double)mode.pixelClock/(double)mode.colorClock))*(2*PI) + PI*3/4 - PI*3/4)*amplitudeBurst
									   //) << 8;
					//((unsigned short *)hSyncLineBuffer[1])[i ^ 1] =
					   //(unsigned short)(levelBlanking + sin(((double)(i - mode.hFront)/((double)mode.pixelClock/(double)mode.colorClock))*(2*PI) - PI*3/4 - PI*3/4)*amplitudeBurst
									   //) << 8;
				}
			}
			//equalizing signal
			if (i >= mode.hFront && i < (mode.hFront + mode.hSync/2))
			{
				//equalizing
				((unsigned short *)equalizingLineBuffer)[i ^ 1] = levelSync << 8;
			}
			//vertical sync signal
			if (i >= mode.hFront && i < (mode.hFront + (samplesHL - mode.hSync)))
			{
				//vsync // hFront should never be bigger than hSync or this overflows
				((unsigned short *)vSyncLineBuffer)[i ^ 1] = levelSync << 8;
			}
		}


		//allocate DMA buffer descriptors for the whole frame
		dmaBufferDescriptorCount = mode.linesPerFrame * 2;
		dmaBufferDescriptors = DMABufferDescriptor::allocateDescriptors(dmaBufferDescriptorCount);
		if(!dmaBufferDescriptors)
		{
			dmaBufferDescriptorCount = 0;
			return;
		}
		//link all buffer descriptors in a ring
		for (int i = 0; i < dmaBufferDescriptorCount; i++)
			dmaBufferDescriptors[i].next(dmaBufferDescriptors[(i + 1) % dmaBufferDescriptorCount]);

		//assign the buffers accross the DMA buffer descriptors
		//CONVENTION: the frame starts after the last non-sync line of previous frame
		int d = 0;
		//pre-line
		int consumelines = mode.vOPreRegHL;
		while(consumelines>=2)
		{
			dmaBufferDescriptors[d++].setBuffer(normalFrontLineBuffer, bytesHL);
			dmaBufferDescriptors[d++].setBuffer(normalBackLineBuffer, bytesHL);
			consumelines-=2;
		}
		//NF
		if(consumelines == 1)
		{
			dmaBufferDescriptors[d++].setBuffer(normalFrontLineBuffer, bytesHL);
		}
		//EQ
		consumelines = mode.vPreEqHL;
		for (int i = 0; i < consumelines; i++)
			dmaBufferDescriptors[d++].setBuffer(equalizingLineBuffer, bytesHL);
		//SY
		consumelines = mode.vSyncHL;
		for (int i = 0; i < consumelines; i++)
			dmaBufferDescriptors[d++].setBuffer(vSyncLineBuffer, bytesHL);
		//EQ
		consumelines = mode.vPostEqHL;
		for (int i = 0; i < consumelines; i++)
			dmaBufferDescriptors[d++].setBuffer(equalizingLineBuffer, bytesHL);
		//NB
		consumelines = mode.vOPostRegHL;
		if((consumelines & 1) == 1)
		{
			dmaBufferDescriptors[d++].setBuffer(normalBackLineBuffer, bytesHL);
			consumelines--;
		}
		//post-line
		while(consumelines>=2)
		{
			dmaBufferDescriptors[d++].setBuffer(normalFrontLineBuffer, bytesHL);
			dmaBufferDescriptors[d++].setBuffer(normalBackLineBuffer, bytesHL);
			consumelines-=2;
		}

		for (int i = 0; i < mode.vBack; i++)
		{
			dmaBufferDescriptors[d++].setBuffer(normalFrontLineBuffer, bytesHL);
			dmaBufferDescriptors[d++].setBuffer(normalBackLineBuffer, bytesHL);
		}
		for (int i = 0; i < mode.vActive; i++)
		{
			dmaBufferDescriptors[d].setBuffer(vBlankLineBuffer[(d/2)&1], bytesHSync);
			d++;
			dmaBufferDescriptors[d++].setBuffer(frameBuffer[(i*(mode.interlaced?2:1) - (mode.interlaced?1:0)) / mode.vDiv], mode.hRes * bytesPerSample());
		}
		for (int i = 0; i < mode.vFront; i++)
		{
			dmaBufferDescriptors[d++].setBuffer(normalFrontLineBuffer, bytesHL);
			dmaBufferDescriptors[d++].setBuffer(normalBackLineBuffer, bytesHL);
		}

		// here d should be linesPerFrame*2 if mode is progressive
		// and linesPerFrame*2 / 2 if mode is interlaced
		if(mode.interlaced)
		{

		//pre-line
		int consumelines = mode.vEPreRegHL;
		while(consumelines>=2)
		{
			dmaBufferDescriptors[d++].setBuffer(normalFrontLineBuffer, bytesHL);
			dmaBufferDescriptors[d++].setBuffer(normalBackLineBuffer, bytesHL);
			consumelines-=2;
		}
		//NF
		if(consumelines == 1)
		{
			dmaBufferDescriptors[d++].setBuffer(normalFrontLineBuffer, bytesHL);
		}
		//EQ
		consumelines = mode.vPreEqHL;
		for (int i = 0; i < consumelines; i++)
			dmaBufferDescriptors[d++].setBuffer(equalizingLineBuffer, bytesHL);
		//SY
		consumelines = mode.vSyncHL;
		for (int i = 0; i < consumelines; i++)
			dmaBufferDescriptors[d++].setBuffer(vSyncLineBuffer, bytesHL);
		//EQ
		consumelines = mode.vPostEqHL;
		for (int i = 0; i < consumelines; i++)
			dmaBufferDescriptors[d++].setBuffer(equalizingLineBuffer, bytesHL);
		//NB
		consumelines = mode.vEPostRegHL;
		if((consumelines & 1) == 1)
		{
			dmaBufferDescriptors[d++].setBuffer(normalBackLineBuffer, bytesHL);
			consumelines--;
		}
		//post-line
		while(consumelines>=2)
		{
			dmaBufferDescriptors[d++].setBuffer(normalFrontLineBuffer, bytesHL);
			dmaBufferDescriptors[d++].setBuffer(normalBackLineBuffer, bytesHL);
			consumelines-=2;
		}

		for (int i = 0; i < mode.vBack; i++)
		{
			dmaBufferDescriptors[d].setBuffer(vBlankLineBuffer[(d/2)&1], bytesHL);
			d++;
			dmaBufferDescriptors[d++].setBuffer(normalBackLineBuffer, bytesHL);
		}
		for (int i = 0; i < mode.vActive; i++)
		{
			dmaBufferDescriptors[d].setBuffer(vBlankLineBuffer[(d/2)&1], bytesHSync);
			d++;
			dmaBufferDescriptors[d++].setBuffer(frameBuffer[(i*2) / mode.vDiv], mode.hRes * bytesPerSample());
		}
		for (int i = 0; i < mode.vFront; i++)
		{
			dmaBufferDescriptors[d++].setBuffer(normalFrontLineBuffer, bytesHL);
			dmaBufferDescriptors[d++].setBuffer(normalBackLineBuffer, bytesHL);
		}

		}
	}

	virtual void show(bool vSync = false)
	{
		if (!frameBufferCount)
			return;
		if (vSync)
		{
			//TODO read the I2S docs to find out
		}
		Graphics::show(vSync);
	}

	bool usesPAL4xEncoder() const
	{
		return pal4xEnabled;
	}

	bool usesPAL4xLowMemoryEncoder() const
	{
		return pal4xLowMemoryMode;
	}

	bool setIndexedPaletteOverride(uint8_t index, uint8_t red,
	                              uint8_t green, uint8_t blue)
	{
		if (!indexedPaletteOverrides)
		{
			indexedPaletteOverrides =
				(uint32_t *)calloc(256, sizeof(uint32_t));
			if (!indexedPaletteOverrides) return false;
		}
		indexedPaletteOverrides[index] = 0x01000000UL |
			(uint32_t)red |
			((uint32_t)green << 8) |
			((uint32_t)blue << 16);
		return true;
	}

	void clearIndexedPaletteOverrides()
	{
		if (indexedPaletteOverrides)
		{
			free(indexedPaletteOverrides);
			indexedPaletteOverrides = 0;
		}
		if (pal4xEnabled)
			rebuildPAL4xTables();
	}

	void applyIndexedPaletteOverrides()
	{
		if (pal4xEnabled)
			rebuildPAL4xTables();
	}

  private:
	void writePixelFast(int x, int y, Color color)
	{
		backBuffer[graphics_swy(y)][graphics_swx(x)] =
			(BufferUnit)((coltobuf(color & graphics_colormask(), x, y) & 0xff) << 8);
	}

	BufferUnit encodedPixel(Color color)
	{
		return (BufferUnit)((coltobuf(color & graphics_colormask(), 0, 0) & 0xff) << 8);
	}

	static void fillIndexedSpan(BufferUnit *row, int x0, int x1, BufferUnit pixel)
	{
		if (x0 & 1)
		{
			row[x0 ^ 1] = pixel;
			++x0;
		}

		uint32_t packed = (uint32_t)pixel | ((uint32_t)pixel << 16);
		uint32_t *pairs = (uint32_t *)(row + x0);
		while (x0 + 1 < x1)
		{
			*pairs++ = packed;
			x0 += 2;
		}
		if (x0 < x1)
			row[x0 ^ 1] = pixel;
	}

	static const int PAL4X_LINE_SAMPLES = 1136;
	static const int PAL4X_LINE_COUNT = 312;
	static const int PAL4X_ACTIVE_LINES = 288;
	static const int PAL4X_ACTIVE_FIRST_LINE = 8;
	static const int PAL4X_SYNC_FIRST_LINE = 304;
	static const int PAL4X_PICTURE_SAMPLES = 940;

	bool calibrationConfigured = false;
	bool calibrationStorageChecked = false;
	bool calibrationLoadedFromStorage = false;
	bool autoLoadCalibration = true;
	bool pal4xEnabled = false;
	bool pal4xLowMemoryMode = false;
	long pal4xSampleRate = 17734476L;
	int pal4xHSyncSamples = 84;
	int pal4xBurstStart = 100;
	int pal4xBurstSamples = 44;
	int pal4xPictureStart = 184;
	volatile int pal4xNextLine = 0;
	uint8_t pal4xBufferState[2] = {255, 255};
	bool pal4xBufferHadPicture[2] = {false, false};
	uint16_t pal4xPalette[2][256][4];
	uint16_t pal4xBurst[2][4];
	uint32_t *indexedPaletteOverrides = 0;

	bool supportsPAL4x(const ModeComposite &candidate) const;
	void configurePAL4xTiming();
	void rebuildPAL4xTables();
	void allocatePAL4xLineBuffers();
	static void IRAM_ATTR interruptPAL4x(void *arg);
	void IRAM_ATTR renderPAL4xLine(uint16_t *line, int lineNumber, int descriptorIndex);
	void IRAM_ATTR renderPAL4xNormalBase(uint16_t *line, int lineParity);
	void IRAM_ATTR renderPAL4xSync(uint16_t *line, int lineNumber);
	void IRAM_ATTR clearPAL4xPicture(uint16_t *line);
	void IRAM_ATTR blitPAL4xPicture(uint16_t *line, int sourceY, int lineParity);

	void prepareCalibration(bool useVoltageDivider)
	{
		if (!calibrationConfigured && autoLoadCalibration && !calibrationStorageChecked)
			loadCalibration();
		if (!calibrationConfigured)
			calibration = CompositeColorCalibration::defaults(useVoltageDivider);
		applyCalibrationParameters(false);
	}

	static uint32_t adjustedClock(uint32_t baseClock, int32_t ppm)
	{
		int64_t adjusted = (int64_t)baseClock + ((int64_t)baseClock * ppm) / 1000000LL;
		if (adjusted < 1) adjusted = 1;
		if (adjusted > 0xffffffffLL) adjusted = 0xffffffffLL;
		return (uint32_t)adjusted;
	}

	double burstPhaseRadians() const
	{
		return ((double)calibration.burstPhaseDegrees * PI) / 180.0;
	}

	int clampDACLevel(int value) const
	{
		if (value < levelLowClipping) return levelLowClipping;
		if (value > levelHighClipping) return levelHighClipping;
		return value;
	}

  protected:
	virtual bool useInterrupt()
	{
		return pal4xEnabled;
	}

	virtual void interrupt()
	{
	}
};
