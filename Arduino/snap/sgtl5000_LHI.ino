#include "control_sgtl5000.h"
#include "Wire.h"

#define SGTL5000_I2C_ADDR  0x0A  // CTRL_ADR0_CS pin low (normal configuration)

#define CHIP_ID        0x0000
// 15:8 PARTID    0xA0 - 8 bit identifier for SGTL5000
// 7:0  REVID   0x00 - revision number for SGTL5000.

#define CHIP_DIG_POWER      0x0002
// 6  ADC_POWERUP 1=Enable, 0=disable the ADC block, both digital & analog, 
// 5  DAC_POWERUP 1=Enable, 0=disable the DAC block, both analog and digital
// 4  DAP_POWERUP 1=Enable, 0=disable the DAP block
// 1  I2S_OUT_POWERUP 1=Enable, 0=disable the I2S data output
// 0  I2S_IN_POWERUP  1=Enable, 0=disable the I2S data input

#define CHIP_CLK_CTRL     0x0004
// 5:4  RATE_MODE Sets the sample rate mode. MCLK_FREQ is still specified
//      relative to the rate in SYS_FS
//        0x0 = SYS_FS specifies the rate
//        0x1 = Rate is 1/2 of the SYS_FS rate
//        0x2 = Rate is 1/4 of the SYS_FS rate
//        0x3 = Rate is 1/6 of the SYS_FS rate
// 3:2  SYS_FS    Sets the internal system sample rate (default=2)
//        0x0 = 32 kHz
//        0x1 = 44.1 kHz
//        0x2 = 48 kHz
//        0x3 = 96 kHz
// 1:0  MCLK_FREQ Identifies incoming SYS_MCLK frequency and if the PLL should be used
//        0x0 = 256*Fs
//        0x1 = 384*Fs
//        0x2 = 512*Fs
//        0x3 = Use PLL
//        The 0x3 (Use PLL) setting must be used if the SYS_MCLK is not
//        a standard multiple of Fs (256, 384, or 512). This setting can
//        also be used if SYS_MCLK is a standard multiple of Fs.
//        Before this field is set to 0x3 (Use PLL), the PLL must be
//        powered up by setting CHIP_ANA_POWER->PLL_POWERUP and
//        CHIP_ANA_POWER->VCOAMP_POWERUP.  Also, the PLL dividers must
//        be calculated based on the external MCLK rate and
//        CHIP_PLL_CTRL register must be set (see CHIP_PLL_CTRL register
//        description details on how to calculate the divisors).

#define CHIP_I2S_CTRL     0x0006
// 8  SCLKFREQ  Sets frequency of I2S_SCLK when in master mode (MS=1). When in slave
//      mode (MS=0), this field must be set appropriately to match SCLK input
//      rate.
//        0x0 = 64Fs
//        0x1 = 32Fs - Not supported for RJ mode (I2S_MODE = 1)
// 7  MS    Configures master or slave of I2S_LRCLK and I2S_SCLK.
//        0x0 = Slave: I2S_LRCLK an I2S_SCLK are inputs
//        0x1 = Master: I2S_LRCLK and I2S_SCLK are outputs
//        NOTE: If the PLL is used (CHIP_CLK_CTRL->MCLK_FREQ==0x3),
//        the SGTL5000 must be a master of the I2S port (MS==1)
// 6  SCLK_INV  Sets the edge that data (input and output) is clocked in on for I2S_SCLK
//        0x0 = data is valid on rising edge of I2S_SCLK
//        0x1 = data is valid on falling edge of I2S_SCLK
// 5:4  DLEN    I2S data length (default=1)
//        0x0 = 32 bits (only valid when SCLKFREQ=0),
//          not valid for Right Justified Mode
//        0x1 = 24 bits (only valid when SCLKFREQ=0)
//        0x2 = 20 bits
//        0x3 = 16 bits
// 3:2  I2S_MODE  Sets the mode for the I2S port
//        0x0 = I2S mode or Left Justified (Use LRALIGN to select)
//        0x1 = Right Justified Mode
//        0x2 = PCM Format A/B
//        0x3 = RESERVED
// 1  LRALIGN   I2S_LRCLK Alignment to data word. Not used for Right Justified mode
//        0x0 = Data word starts 1 I2S_SCLK delay after I2S_LRCLK
//          transition (I2S format, PCM format A)
//        0x1 = Data word starts after I2S_LRCLK transition (left
//          justified format, PCM format B)
// 0  LRPOL   I2S_LRCLK Polarity when data is presented.
//        0x0 = I2S_LRCLK = 0 - Left, 1 - Right
//        1x0 = I2S_LRCLK = 0 - Right, 1 - Left
//        The left subframe should be presented first regardless of
//        the setting of LRPOL.

#define CHIP_SSS_CTRL     0x000A
// 14 DAP_MIX_LRSWAP  DAP Mixer Input Swap
//        0x0 = Normal Operation
//        0x1 = Left and Right channels for the DAP MIXER Input are swapped.
// 13 DAP_LRSWAP  DAP Mixer Input Swap
//        0x0 = Normal Operation
//        0x1 = Left and Right channels for the DAP Input are swapped
// 12 DAC_LRSWAP  DAC Input Swap
//        0x0 = Normal Operation
//        0x1 = Left and Right channels for the DAC are swapped
// 10 I2S_LRSWAP  I2S_DOUT Swap
//        0x0 = Normal Operation
//        0x1 = Left and Right channels for the I2S_DOUT are swapped
// 9:8  DAP_MIX_SELECT  Select data source for DAP mixer
//        0x0 = ADC
//        0x1 = I2S_IN
//        0x2 = Reserved
//        0x3 = Reserved
// 7:6  DAP_SELECT  Select data source for DAP
//        0x0 = ADC
//        0x1 = I2S_IN
//        0x2 = Reserved
//        0x3 = Reserved
// 5:4  DAC_SELECT  Select data source for DAC (default=1)
//        0x0 = ADC
//        0x1 = I2S_IN
//        0x2 = Reserved
//        0x3 = DAP
// 1:0  I2S_SELECT  Select data source for I2S_DOUT
//        0x0 = ADC
//        0x1 = I2S_IN
//        0x2 = Reserved
//        0x3 = DAP

#define CHIP_ADCDAC_CTRL    0x000E
// 13 VOL_BUSY_DAC_RIGHT Volume Busy DAC Right
//        0x0 = Ready
//        0x1 = Busy - This indicates the channel has not reached its
//          programmed volume/mute level
// 12 VOL_BUSY_DAC_LEFT  Volume Busy DAC Left
//        0x0 = Ready
//        0x1 = Busy - This indicates the channel has not reached its
//          programmed volume/mute level
// 9  VOL_RAMP_EN Volume Ramp Enable (default=1)
//        0x0 = Disables volume ramp. New volume settings take immediate
//          effect without a ramp
//        0x1 = Enables volume ramp
//        This field affects DAC_VOL. The volume ramp effects both
//        volume settings and mute When set to 1 a soft mute is enabled.
// 8  VOL_EXPO_RAMP Exponential Volume Ramp Enable
//        0x0 = Linear ramp over top 4 volume octaves
//        0x1 = Exponential ramp over full volume range
//        This bit onlay takes effect if VOL_RAMP_EN is 1.
// 3  DAC_MUTE_RIGHT  DAC Right Mute (default=1)
//        0x0 = Unmute
//        0x1 = Muted
//        If VOL_RAMP_EN = 1, this is a soft mute.
// 2  DAC_MUTE_LEFT DAC Left Mute (default=1)
//        0x0 = Unmute
//        0x1 = Muted
//        If VOL_RAMP_EN = 1, this is a soft mute.
// 1  ADC_HPF_FREEZE  ADC High Pass Filter Freeze
//        0x0 = Normal operation
//        0x1 = Freeze the ADC high-pass filter offset register.  The
//        offset continues to be subtracted from the ADC data stream.
// 0  ADC_HPF_BYPASS  ADC High Pass Filter Bypass
//        0x0 = Normal operation
//        0x1 = Bypassed and offset not updated

#define CHIP_DAC_VOL      0x0010
// 15:8 DAC_VOL_RIGHT DAC Right Channel Volume.  Set the Right channel DAC volume
//      with 0.5017 dB steps from 0 to -90 dB
//        0x3B and less = Reserved
//        0x3C = 0 dB
//        0x3D = -0.5 dB
//        0xF0 = -90 dB
//        0xFC and greater = Muted
//        If VOL_RAMP_EN = 1, there is an automatic ramp to the
//        new volume setting.
// 7:0  DAC_VOL_LEFT  DAC Left Channel Volume.  Set the Left channel DAC volume
//      with 0.5017 dB steps from 0 to -90 dB
//        0x3B and less = Reserved
//        0x3C = 0 dB
//        0x3D = -0.5 dB
//        0xF0 = -90 dB
//        0xFC and greater = Muted
//        If VOL_RAMP_EN = 1, there is an automatic ramp to the
//        new volume setting.

#define CHIP_PAD_STRENGTH   0x0014
// 9:8  I2S_LRCLK I2S LRCLK Pad Drive Strength (default=1)
//        Sets drive strength for output pads per the table below.
//         VDDIO    1.8 V     2.5 V     3.3 V
//        0x0 = Disable
//        0x1 =     1.66 mA   2.87 mA   4.02 mA
//        0x2 =     3.33 mA   5.74 mA   8.03 mA
//        0x3 =     4.99 mA   8.61 mA   12.05 mA
// 7:6  I2S_SCLK  I2S SCLK Pad Drive Strength (default=1)
// 5:4  I2S_DOUT  I2S DOUT Pad Drive Strength (default=1)
// 3:2  CTRL_DATA I2C DATA Pad Drive Strength (default=3)
// 1:0  CTRL_CLK  I2C CLK Pad Drive Strength (default=3)
//        (all use same table as I2S_LRCLK)

#define CHIP_ANA_ADC_CTRL   0x0020
// 8  ADC_VOL_M6DB  ADC Volume Range Reduction
//        This bit shifts both right and left analog ADC volume
//        range down by 6.0 dB.
//        0x0 = No change in ADC range
//        0x1 = ADC range reduced by 6.0 dB
// 7:4  ADC_VOL_RIGHT ADC Right Channel Volume
//        Right channel analog ADC volume control in 1.5 dB steps.
//        0x0 = 0 dB
//        0x1 = +1.5 dB
//        ...
//        0xF = +22.5 dB
//        This range is -6.0 dB to +16.5 dB if ADC_VOL_M6DB is set to 1.
// 3:0  ADC_VOL_LEFT  ADC Left Channel Volume
//        (same scale as ADC_VOL_RIGHT)

#define CHIP_ANA_HP_CTRL    0x0022
// 14:8 HP_VOL_RIGHT  Headphone Right Channel Volume  (default 0x18)
//        Right channel headphone volume control with 0.5 dB steps.
//        0x00 = +12 dB
//        0x01 = +11.5 dB
//        0x18 = 0 dB
//        ...
//        0x7F = -51.5 dB
// 6:0  HP_VOL_LEFT Headphone Left Channel Volume  (default 0x18)
//        (same scale as HP_VOL_RIGHT)

#define CHIP_ANA_CTRL     0x0024
// 15:9 Reserved
// 8  MUTE_LO   LINEOUT Mute, 0 = Unmute, 1 = Mute  (default 1)
// 7  Reserved
// 6  SELECT_HP Select the headphone input, 0 = DAC, 1 = LINEIN
// 5  EN_ZCD_HP Enable the headphone zero cross detector (ZCD)
//        0x0 = HP ZCD disabled
//        0x1 = HP ZCD enabled
// 4  MUTE_HP   Mute the headphone outputs, 0 = Unmute, 1 = Mute (default)
// 3  Reserved
// 2  SELECT_ADC  Select the ADC input, 0 = Microphone, 1 = LINEIN
// 1  EN_ZCD_ADC  Enable the ADC analog zero cross detector (ZCD)
//        0x0 = ADC ZCD disabled
//        0x1 = ADC ZCD enabled
// 0  MUTE_ADC  Mute the ADC analog volume, 0 = Unmute, 1 = Mute (default)

#define CHIP_LINREG_CTRL    0x0026
// 6  VDDC_MAN_ASSN Determines chargepump source when VDDC_ASSN_OVRD is set.
//        0x0 = VDDA
//        0x1 = VDDIO
// 5  VDDC_ASSN_OVRD  Charge pump Source Assignment Override
//        0x0 = Charge pump source is automatically assigned based
//          on higher of VDDA and VDDIO
//        0x1 = the source of charge pump is manually assigned by
//          VDDC_MAN_ASSN If VDDIO and VDDA are both the same
//          and greater than 3.1 V, VDDC_ASSN_OVRD and
//          VDDC_MAN_ASSN should be used to manually assign
//          VDDIO as the source for charge pump.
// 3:0  D_PROGRAMMING Sets the VDDD linear regulator output voltage in 50 mV steps.
//      Must clear the LINREG_SIMPLE_POWERUP and STARTUP_POWERUP bits
//      in the 0x0030 (CHIP_ANA_POWER) register after power-up, for
//      this setting to produce the proper VDDD voltage.
//        0x0 = 1.60
//        0xF = 0.85

#define CHIP_REF_CTRL     0x0028 // bandgap reference bias voltage and currents
// 8:4  VAG_VAL   Analog Ground Voltage Control
//        These bits control the analog ground voltage in 25 mV steps.
//        This should usually be set to VDDA/2 or lower for best
//        performance (maximum output swing at minimum THD). This VAG
//        reference is also used for the DAC and ADC voltage reference.
//        So changing this voltage scales the output swing of the DAC
//        and the output signal of the ADC.
//        0x00 = 0.800 V
//        0x1F = 1.575 V
// 3:1  BIAS_CTRL Bias control
//        These bits adjust the bias currents for all of the analog
//        blocks. By lowering the bias current a lower quiescent power
//        is achieved. It should be noted that this mode can affect
//        performance by 3-4 dB.
//        0x0 = Nominal
//        0x1-0x3=+12.5%
//        0x4=-12.5%
//        0x5=-25%
//        0x6=-37.5%
//        0x7=-50%
// 0  SMALL_POP VAG Ramp Control
//        Setting this bit slows down the VAG ramp from ~200 to ~400 ms
//        to reduce the startup pop, but increases the turn on/off time.
//        0x0 = Normal VAG ramp
//        0x1 = Slow down VAG ramp

#define CHIP_MIC_CTRL     0x002A // microphone gain & internal microphone bias
// 9:8  BIAS_RESISTOR MIC Bias Output Impedance Adjustment
//        Controls an adjustable output impedance for the microphone bias.
//        If this is set to zero the micbias block is powered off and
//        the output is highZ.
//        0x0 = Powered off
//        0x1 = 2.0 kohm
//        0x2 = 4.0 kohm
//        0x3 = 8.0 kohm
// 6:4  BIAS_VOLT MIC Bias Voltage Adjustment
//        Controls an adjustable bias voltage for the microphone bias
//        amp in 250 mV steps. This bias voltage setting should be no
//        more than VDDA-200 mV for adequate power supply rejection.
//        0x0 = 1.25 V
//        ...
//        0x7 = 3.00 V
// 1:0  GAIN    MIC Amplifier Gain
//        Sets the microphone amplifier gain. At 0 dB setting the THD
//        can be slightly higher than other paths- typically around
//        ~65 dB. At other gain settings the THD are better.
//        0x0 = 0 dB
//        0x1 = +20 dB
//        0x2 = +30 dB
//        0x3 = +40 dB

#define CHIP_LINE_OUT_CTRL    0x002C
// 11:8 OUT_CURRENT Controls the output bias current for the LINEOUT amplifiers.  The
//      nominal recommended setting for a 10 kohm load with 1.0 nF load cap
//      is 0x3. There are only 5 valid settings.
//        0x0=0.18 mA
//        0x1=0.27 mA
//        0x3=0.36 mA
//        0x7=0.45 mA
//        0xF=0.54 mA
// 5:0  LO_VAGCNTRL LINEOUT Amplifier Analog Ground Voltage
//        Controls the analog ground voltage for the LINEOUT amplifiers
//        in 25 mV steps. This should usually be set to VDDIO/2.
//        0x00 = 0.800 V
//        ...
//        0x1F = 1.575 V
//        ...
//        0x23 = 1.675 V
//        0x24-0x3F are invalid

#define CHIP_LINE_OUT_VOL   0x002E
// 12:8 LO_VOL_RIGHT  LINEOUT Right Channel Volume (default=4)
//        Controls the right channel LINEOUT volume in 0.5 dB steps.
//        Higher codes have more attenuation.
// 4:0  LO_VOL_LEFT LINEOUT Left Channel Output Level (default=4)
//        Used to normalize the output level of the left line output
//        to full scale based on the values used to set
//        LINE_OUT_CTRL->LO_VAGCNTRL and CHIP_REF_CTRL->VAG_VAL.
//        In general this field should be set to:
//        40*log((VAG_VAL)/(LO_VAGCNTRL)) + 15
//        Suggested values based on typical VDDIO and VDDA voltages.
//          VDDA  VAG_VAL VDDIO  LO_VAGCNTRL LO_VOL_*
//          1.8 V    0.9   3.3 V     1.55      0x06
//          1.8 V    0.9   1.8 V      0.9      0x0F
//          3.3 V   1.55   1.8 V      0.9      0x19
//          3.3 V   1.55   3.3 V     1.55      0x0F
//        After setting to the nominal voltage, this field can be used
//        to adjust the output level in +/-0.5 dB increments by using
//        values higher or lower than the nominal setting.

#define CHIP_ANA_POWER      0x0030 // power down controls for the analog blocks.
    // The only other power-down controls are BIAS_RESISTOR in the MIC_CTRL register
    //  and the EN_ZCD control bits in ANA_CTRL.
// 14 DAC_MONO  While DAC_POWERUP is set, this allows the DAC to be put into left only
//        mono operation for power savings. 0=mono, 1=stereo (default)
// 13 LINREG_SIMPLE_POWERUP  Power up the simple (low power) digital supply regulator.
//        After reset, this bit can be cleared IF VDDD is driven
//        externally OR the primary digital linreg is enabled with
//        LINREG_D_POWERUP
// 12 STARTUP_POWERUP Power up the circuitry needed during the power up ramp and reset.
//        After reset this bit can be cleared if VDDD is coming from
//        an external source.
// 11 VDDC_CHRGPMP_POWERUP Power up the VDDC charge pump block. If neither VDDA or VDDIO
//        is 3.0 V or larger this bit should be cleared before analog
//        blocks are powered up.
// 10 PLL_POWERUP PLL Power Up, 0 = Power down, 1 = Power up
//        When cleared, the PLL is turned off. This must be set before
//        CHIP_CLK_CTRL->MCLK_FREQ is programmed to 0x3. The
//        CHIP_PLL_CTRL register must be configured correctly before
//        setting this bit.
// 9  LINREG_D_POWERUP Power up the primary VDDD linear regulator, 0 = Power down, 1 = Power up
// 8  VCOAMP_POWERUP  Power up the PLL VCO amplifier, 0 = Power down, 1 = Power up
// 7  VAG_POWERUP Power up the VAG reference buffer.
//        Setting this bit starts the power up ramp for the headphone
//        and LINEOUT. The headphone (and/or LINEOUT) powerup should
//        be set BEFORE clearing this bit. When this bit is cleared
//        the power-down ramp is started. The headphone (and/or LINEOUT)
//        powerup should stay set until the VAG is fully ramped down
//        (200 to 400 ms after clearing this bit).
//        0x0 = Power down, 0x1 = Power up
// 6  ADC_MONO  While ADC_POWERUP is set, this allows the ADC to be put into left only
//        mono operation for power savings. This mode is useful when
//        only using the microphone input.
//        0x0 = Mono (left only), 0x1 = Stereo
// 5  REFTOP_POWERUP  Power up the reference bias currents
//        0x0 = Power down, 0x1 = Power up
//        This bit can be cleared when the part is a sleep state
//        to minimize analog power.
// 4  HEADPHONE_POWERUP Power up the headphone amplifiers
//        0x0 = Power down, 0x1 = Power up
// 3  DAC_POWERUP Power up the DACs
//        0x0 = Power down, 0x1 = Power up
// 2  CAPLESS_HEADPHONE_POWERUP Power up the capless headphone mode
//        0x0 = Power down, 0x1 = Power up
// 1  ADC_POWERUP Power up the ADCs
//        0x0 = Power down, 0x1 = Power up
// 0  LINEOUT_POWERUP Power up the LINEOUT amplifiers
//        0x0 = Power down, 0x1 = Power up

#define CHIP_PLL_CTRL     0x0032
// 15:11 INT_DIVISOR
// 10:0 FRAC_DIVISOR

#define CHIP_CLK_TOP_CTRL   0x0034
// 11 ENABLE_INT_OSC  Setting this bit enables an internal oscillator to be used for the
//        zero cross detectors, the short detect recovery, and the
//        charge pump. This allows the I2S clock to be shut off while
//        still operating an analog signal path. This bit can be kept
//        on when the I2S clock is enabled, but the I2S clock is more
//        accurate so it is preferred to clear this bit when I2S is present.
// 3  INPUT_FREQ_DIV2 SYS_MCLK divider before PLL input
//        0x0 = pass through
//        0x1 = SYS_MCLK is divided by 2 before entering PLL
//        This must be set when the input clock is above 17 Mhz. This
//        has no effect when the PLL is powered down.

#define CHIP_ANA_STATUS     0x0036
// 9  LRSHORT_STS This bit is high whenever a short is detected on the left or right
//        channel headphone drivers.
// 8  CSHORT_STS  This bit is high whenever a short is detected on the capless headphone
//        common/center channel driver.
// 4  PLL_IS_LOCKED This bit goes high after the PLL is locked.

#define CHIP_ANA_TEST1      0x0038 //  intended only for debug.
#define CHIP_ANA_TEST2      0x003A //  intended only for debug.

#define CHIP_SHORT_CTRL     0x003C
#define DAP_AVC_CTRL        0x0124 //audio volume control
// 0  Disable

  bool audio_enable(int fs_mode)
{
  if(fs_mode>3) fs_mode=3;
//  muted = true;
//  Serial.print("audio ID = ");
//  delay(5);
//  unsigned int n = read(CHIP_ID);
//  Serial.println(n, HEX);

  chipWrite(CHIP_ANA_POWER, 0x4060);  // VDDD is externally driven with 1.8V
  chipWrite(CHIP_LINREG_CTRL, 0x006C);  // VDDA & VDDIO both over 3.1V
  
  chipWrite(CHIP_REF_CTRL, 0x01F0); // VAG=1.575, normal ramp, no bias current;
  //chipWrite(CHIP_REF_CTRL, 0x01F2); // VAG=1.575, normal ramp, +12.5% bias current
    
  chipWrite(CHIP_MIC_CTRL, 0x00); // Mic bias off; Mic Amplifier gain 0 dB

  chipWrite(CHIP_LINE_OUT_CTRL, 0x0F22); // LO_VAGCNTRL=1.65V, OUT_CURRENT=0.54mA
  chipWrite(CHIP_SHORT_CTRL, 0x4446);  // allow up to 125mA
  chipWrite(CHIP_ANA_CTRL, 0x0137);  // enable zero cross detectors
  
  audio_power_up();
  
  delay(400);
  //chipWrite(CHIP_LINE_OUT_VOL, 0x1D1D); // default approx 1.3 volts peak-to-peak
  chipWrite(CHIP_LINE_OUT_VOL, 0x1919); // default approx 1.3 volts peak-to-peak
  chipWrite(CHIP_CLK_CTRL, fs_mode<<2);  // 256*Fs fs_mode = 0:32 kHz; 1:44.1 kHz; 2:48 kHz; 3:96 kHz
  chipWrite(CHIP_I2S_CTRL, 0x0130); // SCLK=32*Fs, 16bit, I2S format
  // default signal routing is ok?
  
  //chipWrite(CHIP_SSS_CTRL, 0x0010); // ADC->I2S, I2S->DAC
  chipWrite(CHIP_SSS_CTRL, 0x0000); // ADC->I2S, ADC->DAC

    chipWrite(CHIP_ADCDAC_CTRL, 0x0008); // DAC mute right; DAC left unmute; ADC HPF normal operation
  
  
  chipWrite(CHIP_DAC_VOL, 0xFF3C); // dac mute right; left 0 dB
  chipWrite(CHIP_ANA_HP_CTRL, 0x7F7F); // set headphone volume (lowest level)
  //chipWrite(CHIP_ANA_CTRL, 0x0036);  // enable zero cross detectors; line input

  chipWrite(DAP_AVC_CTRL, 0x0000); //no automatic volume control
  //chipWrite(CHIP_ANA_CTRL, 0x0114);  // lineout mute, headphone mute, no zero cross detectors, line input selected
  chipWrite(CHIP_ANA_CTRL, 0x0014);  // lineout unmute, headphone mute, no zero cross detectors, line input selected
  chipWrite(CHIP_MIC_CTRL, 0x0000); //microphone off
  chipWrite(CHIP_ANA_ADC_CTRL, 0x0000); // 0 dB gain
  //chipWrite(CHIP_ANA_ADC_CTRL, 0x0100); // -6 dB gain
  
  
   return true;
}

void audio_freeze_adc_hp(){
   chipWrite(CHIP_ADCDAC_CTRL, 0x000A); // DAC mute right; DAC left unmute; ADC high pass filter frozen; ADC HPF normal operation
}

void audio_bypass_adc_hp(){
  chipWrite(CHIP_ADCDAC_CTRL, 0x000B); // DAC mute right; DAC left unmute; ADC high pass filter frozen; ADC HPF bypassed (so does not matter it is frozen); get offset of about 370 units

}

void audio_power_down(void){
  chipWrite(CHIP_ANA_POWER, 0x0000); // analog power down: everything
  chipWrite(CHIP_DIG_POWER, 0x0000); // digital power down: everything
}

void audio_power_up(void){
  chipWrite(CHIP_ANA_POWER, 0x00A2); // power up: adc Stereo = E2; Mono (Left): A2
// chipWrite(CHIP_ANA_POWER, 0x00AB); // power up: DAC; ADC; Mono (Left): Lineout amplifier; AB
 
  chipWrite(CHIP_DIG_POWER, 0x0043); // power up only analag ADC and I2S; disable DAC and DAP
//  chipWrite(CHIP_DIG_POWER, 0x0063); // power up analag ADC, DAC, I2SIN, I2SOUT; disable DAP and IS2OUT
}

bool chipWrite(unsigned int reg, unsigned int val)
{
//  if (reg == CHIP_ANA_CTRL) ana_ctrl = val;
  Wire.beginTransmission(SGTL5000_I2C_ADDR);
  Wire.write(reg >> 8);
  Wire.write(reg);
  Wire.write(val >> 8);
  Wire.write(val);
  if (Wire.endTransmission() == 0) return true;
  return false;
}

  //------------------------------modify I2S-------------------------------------------
// attempt to generate dividers programmatically
// always better to check
void I2S_dividers(uint32_t *iscl, uint32_t fsamp, uint32_t nbits)
{
    int64_t i1 = 1;
    int64_t i2 = 1;
    int64_t i3 = iscl[2]+1;
    int fcpu=F_CPU;
    if(F_CPU<=96000000) fcpu=96000000;
    float A=fcpu/2.0f/i3/(2.0f*nbits*fsamp);
    float mn=1.0; 
    for(int ii=1;ii<64;ii++) 
    { float xx;
      xx=A*ii-(int32_t)(A*ii); 
      if(xx<mn && A*ii<256.0) { mn=xx; i1=ii; i2=A*ii;} //select first candidate
    }
    iscl[0] = (int) (i1-1);
    iscl[1] = (int) (i2-1);
    iscl[2] = (int) (i3-1);
}

void I2S_modification(uint32_t fsamp, uint16_t nbits)
{ uint32_t iscl[3];
  if(nbits==16)
    iscl[2]=3;
  else if(nbits==32)
    iscl[2]=1;
  I2S_dividers(iscl, fsamp ,nbits);
  int fcpu=F_CPU;
  if(F_CPU<=96000000) fcpu=96000000;
  float fs = (fcpu * (iscl[0]+1.0f)) / (iscl[1]+1l) / 2 / (iscl[2]+1l) / (2l*nbits);
  #if DEBUG >0
    Serial.printf("%d %d: %d %d %d %d %d %d\n\r",
        F_CPU, fcpu, fsamp, (int)fs, nbits,iscl[0]+1,iscl[1]+1,iscl[2]+1);
  #endif

  // stop I2S
  I2S0_RCSR &= ~(I2S_RCSR_RE | I2S_RCSR_BCE);

  // modify sampling frequency
  I2S0_MDR = I2S_MDR_FRACT(iscl[0]) | I2S_MDR_DIVIDE(iscl[1]);

  // configure transmitter
  I2S0_TCR2 = I2S_TCR2_SYNC(0) | I2S_TCR2_BCP | I2S_TCR2_MSEL(1)
    | I2S_TCR2_BCD | I2S_TCR2_DIV(iscl[2]);

  // configure receiver (sync'd to transmitter clocks)
  I2S0_RCR2 = I2S_RCR2_SYNC(1) | I2S_TCR2_BCP | I2S_RCR2_MSEL(1)
    | I2S_RCR2_BCD | I2S_RCR2_DIV(iscl[2]);

  //restart I2S
  I2S0_RCSR |= I2S_RCSR_RE | I2S_RCSR_BCE;
}


