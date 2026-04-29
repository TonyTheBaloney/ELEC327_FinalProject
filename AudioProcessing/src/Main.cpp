/**
 * DaisySeed Guitar Pedal
 *
 * Hardware:
 *   - 4x Analog Potentiometers  (wiper → ADC pin, modify effect parameters)
 *   - 1x Toggle Switch (TOP)    → ON = pots write to current effect state
 *   - 1x Toggle Switch (BOTTOM) → ON = audio passthrough (true bypass)
 *   - 1x Momentary Push Button  → cycles through effect presets
 *
 * Pot wiring:
 *   Wiper    → ADC pin  (seed::A0–A3)
 *   One end  → 3V3
 *   Other end→ GND
 *
 * Effect Presets (cycled by push button):
 *   0 – Overdrive  (pot0=drive, pot1=tone,     pot2=level,    pot3=spare)
 *   1 – Chorus     (pot0=rate,  pot1=depth,    pot2=mix,      pot3=spare)
 *   2 – Reverb     (pot0=room,  pot1=damping,  pot2=wet,      pot3=spare)
 *   3 – Phaser     (pot0=rate,  pot1=depth,    pot2=feedback, pot3=spare)
 *   4 - NeuralSeed (pot0=input, pot1=mix,      pot2=level,    pot3=reserved)
 */

#include "daisy_seed.h"
#include "daisysp.h"
#include "Effects/reverbsc.h"
#include "Dynamics/compressor.h"
#include <math.h>
#include "NeuralSeedModelData.h"
#include <RTNeural/RTNeural.h>

using namespace daisy;
using namespace daisysp;

// ──────────────────────────────────────────────
// Pin Assignments
// Use integer indices for GetPin(); ADC pins use seed::A0-style Pin directly.
// ──────────────────────────────────────────────

// Potentiometer wipers — passed directly to AdcChannelConfig::InitSingle
static const Pin POT0_PIN = seed::A1;
static const Pin POT1_PIN = seed::A3;
static const Pin POT2_PIN = seed::A5;
static const Pin POT3_PIN = seed::A7;

// Digital switch pin indices (uint8_t) for hw.GetPin()
static constexpr uint8_t PIN_TOGGLE_EDIT = 1;        // D1 -> top toggle
static constexpr uint8_t PIN_TOGGLE_PASSTHROUGH = 2; // D2 -> bottom toggle
static constexpr uint8_t PIN_BTN_EFFECT_CYCLE = 3;   // D3 -> push button

// ──────────────────────────────────────────────
// ADC channel indices (must match Init order)
// ──────────────────────────────────────────────
static constexpr int ADC_POT0 = 0;
static constexpr int ADC_POT1 = 1;
static constexpr int ADC_POT2 = 2;
static constexpr int ADC_POT3 = 3;
static constexpr int NUM_ADC = 4;
static constexpr int NUM_POTS = 4;

// ──────────────────────────────────────────────
// Constants
// ──────────────────────────────────────────────

static constexpr int NUM_EFFECTS = 6;
static constexpr float SAMPLE_RATE = 48000.f;
static constexpr float ADC_DEADBAND = 0.005f; // ~0.5% — suppresses pot noise
static constexpr float SWITCH_UPDATE_RATE_HZ = 1000.f;

// EQ band geometry — fixed corners/center, only gains move with pots
static constexpr float EQ_BASS_FC = 200.f;    // Hz, low-shelf corner
static constexpr float EQ_MID_FC = 800.f;     // Hz, peak center (guitar formant region)
static constexpr float EQ_MID_Q = 0.7f;       // ~1 octave wide, musical default
static constexpr float EQ_TREBLE_FC = 4000.f; // Hz, high-shelf corner
static constexpr float EQ_GAIN_DB = 12.f;
static constexpr float TWO_PI = 6.28318530717958647692f;
static constexpr float HALF_PI = 1.57079632679489661923f;

// Lead preset (preset 3) tuning
static constexpr float LEAD_BOOST_MAX = 2.f; // pot0 max boost (~+6 dB); was 3.f / 5.f earlier
static constexpr float LEAD_AMP_DRIVE = 0.2f;
static constexpr float LEAD_CAB_FC = 5000.f;
static constexpr float LEAD_CAB_Q = 0.7f;
static constexpr float LEAD_DELAY_TIME_S = 0.4f;
static constexpr float LEAD_DELAY_FB = 0.4f;
static constexpr float LEAD_REVERB_FB = 0.7f;
static constexpr float LEAD_REVERB_LP = 9000.f;
// static constexpr float LEAD_OUTPUT_TRIM = 0.6f;  // retired: pot0 (volume) now does this knob-driven
static constexpr float NOISE_GATE_MAX_THRESH = 0.02f;

// Ambient preset (preset 2) tuning
static constexpr float AMBIENT_DELAY_MIN_S = 0.1f;
static constexpr float AMBIENT_DELAY_MAX_S = 0.8f;
static constexpr float AMBIENT_DELAY_FB = 0.5f;
static constexpr float AMBIENT_DELAY_MIX = 0.5f;
static constexpr float AMBIENT_REVERB_FB = 0.88f;
static constexpr float AMBIENT_REVERB_LP = 12000.f;
static constexpr float AMBIENT_CHORUS_RATE = 0.6f;
static constexpr float AMBIENT_CHORUS_DEPTH = 0.7f;

// High-gain 808-style preset (preset 4) tuning
static constexpr float HG808_GAIN_MAX = 8.f; // pot1 input boost ceiling (~+18 dB; was 10/5 earlier)
static constexpr float HG808_HP_FC = 100.f;  // tighten lows before clipper
static constexpr float HG808_HP_Q = 0.7f;
static constexpr float HG808_PEAK_FC = 700.f; // TS-808 signature mid hump
static constexpr float HG808_PEAK_Q = 1.0f;
static constexpr float HG808_PEAK_GAIN_DB = 6.f;
// static constexpr float HG808_NOISE_MAX = 0.1f; // retired: noise generator removed (didn't sound good)

// ──────────────────────────────────────────────
// Effect Parameter State
// ──────────────────────────────────────────────

enum Effect : uint8_t
{
    EFFECT_EQ = 0, // 3-band EQ + level; flat at centered knobs == bypass
    // EFFECT_BYPASS = 0,     // earlier: clean signal with level trim only
    // EFFECT_OVERDRIVE = 0,  // earlier: drive/tone/level
    EFFECT_FUNK, // Compressor -> AutoWah -> AMP -> Reverb (formerly EFFECT_CHORUS)
    // EFFECT_CHORUS,         // earlier preset 1
    EFFECT_AMBIENT, // Chorus -> Delay -> Reverb (formerly EFFECT_REVERB)
    // EFFECT_REVERB,         // earlier preset 2 (simple knob-set reverb)
    EFFECT_LEAD,
    // EFFECT_PHASER          // earlier preset 3
    EFFECT_HIGAIN, // High-gain 808-style: gain -> HP -> mid hump -> +noise -> drive -> volume
    EFFECT_NEURALSEED
};

struct EffectState
{
    float params[NUM_POTS]; // normalised 0–1, one per pot
};

static EffectState effectStates[NUM_EFFECTS] = {
    {{0.5f, 0.5f, 0.5f, 0.5f}},  // EQ:   level=unity, bass/mid/treble flat (= bypass)
    {{0.5f, 0.7f, 0.5f, 0.25f}}, // Funk: unity vol, deep wah, half-blend comp, subtle verb
    {{0.5f, 0.6f, 0.5f, 0.4f}},  // Ambient: unity vol, med-long delay, moderate verb, half chorus
    {{0.3f, 0.5f, 0.4f, 0.3f}},  // Lead: 0.6x volume, mid gain, light depth, mild gate
    {{0.3f, 0.4f, 0.5f, 0.7f}},  // HiGain 808: 0.6x vol, mod gain, mid drive, full mid-hump tone
    {{0.5f, 1.0f, 1.0f, 0.5f}}   // NeuralSeed: audible default, full wet/full level
};

// ──────────────────────────────────────────────
// Global hardware & DSP objects
// ──────────────────────────────────────────────

DaisySeed hw;

Switch toggleEdit;
Switch togglePassthrough;
Switch btnEffectCycle;

// Overdrive overdrive;   // preset 0 is now EQ; no DSP object needed
Chorus chorus;   // shared by Ambient (preset 2)
ReverbSc reverb; // shared by Funk (preset 1) and Reverb (preset 2)
// Phaser     phaser;    // retired with preset 3 redesign (Lead chain)

// Funk chain (preset 1): Compressor -> AutoWah -> AMP -> Reverb
Compressor compressor; // LGPL, Source, Dynamics, compressor.h
Autowah autowah;       // the DaisySP provides an envelope wah.
Overdrive amp;         // light saturation block ("AMP")

// Minimal RBJ-cookbook biquad in Direct-Form II Transposed.
// Designed once per knob change; runs ~5 mul/add per sample.
struct Biquad
{
    // Filter coefficients (a0 normalized to 1).
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
    // State (DF-II Transposed).
    float z1 = 0.f, z2 = 0.f;

    void Reset() { z1 = z2 = 0.f; }

    inline float Process(float x)
    {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    // Low-shelf: boost/cut everything below fc by gain_db. RBJ Audio EQ Cookbook.
    void SetLowShelf(float sr, float fc, float gain_db)
    {
        const float A = powf(10.f, gain_db / 40.f);
        const float w0 = TWO_PI * fc / sr;
        const float cosw0 = cosf(w0);
        const float sinw0 = sinf(w0);
        const float S = 1.f; // shelf slope (1 = max steepness w/o overshoot)
        const float alpha = sinw0 * 0.5f * sqrtf((A + 1.f / A) * (1.f / S - 1.f) + 2.f);
        const float beta = 2.f * sqrtf(A) * alpha;
        const float a0 = (A + 1.f) + (A - 1.f) * cosw0 + beta;
        const float inv = 1.f / a0;
        b0 = A * ((A + 1.f) - (A - 1.f) * cosw0 + beta) * inv;
        b1 = 2.f * A * ((A - 1.f) - (A + 1.f) * cosw0) * inv;
        b2 = A * ((A + 1.f) - (A - 1.f) * cosw0 - beta) * inv;
        a1 = -2.f * ((A - 1.f) + (A + 1.f) * cosw0) * inv;
        a2 = ((A + 1.f) + (A - 1.f) * cosw0 - beta) * inv;
    }

    // High-shelf: boost/cut everything above fc by gain_db.
    void SetHighShelf(float sr, float fc, float gain_db)
    {
        const float A = powf(10.f, gain_db / 40.f);
        const float w0 = TWO_PI * fc / sr;
        const float cosw0 = cosf(w0);
        const float sinw0 = sinf(w0);
        const float S = 1.f;
        const float alpha = sinw0 * 0.5f * sqrtf((A + 1.f / A) * (1.f / S - 1.f) + 2.f);
        const float beta = 2.f * sqrtf(A) * alpha;
        const float a0 = (A + 1.f) - (A - 1.f) * cosw0 + beta;
        const float inv = 1.f / a0;
        b0 = A * ((A + 1.f) + (A - 1.f) * cosw0 + beta) * inv;
        b1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cosw0) * inv;
        b2 = A * ((A + 1.f) + (A - 1.f) * cosw0 - beta) * inv;
        a1 = 2.f * ((A - 1.f) - (A + 1.f) * cosw0) * inv;
        a2 = ((A + 1.f) - (A - 1.f) * cosw0 - beta) * inv;
    }

    // Peaking EQ: boost/cut a band centered at fc with width set by Q.
    void SetPeak(float sr, float fc, float Q, float gain_db)
    {
        const float A = powf(10.f, gain_db / 40.f);
        const float w0 = TWO_PI * fc / sr;
        const float cosw0 = cosf(w0);
        const float sinw0 = sinf(w0);
        const float alpha = sinw0 / (2.f * Q);
        const float a0 = 1.f + alpha / A;
        const float inv = 1.f / a0;
        b0 = (1.f + alpha * A) * inv;
        b1 = (-2.f * cosw0) * inv;
        b2 = (1.f - alpha * A) * inv;
        a1 = (-2.f * cosw0) * inv;
        a2 = (1.f - alpha / A) * inv;
    }

    void SetLowPass(float sr, float fc, float Q)
    {
        const float w0 = TWO_PI * fc / sr;
        const float cw = cosf(w0);
        const float sw = sinf(w0);
        const float al = sw / (2.f * Q);
        const float a0v = 1.f + al;
        const float inv = 1.f / a0v;
        b0 = ((1.f - cw) * 0.5f) * inv;
        b1 = (1.f - cw) * inv;
        b2 = ((1.f - cw) * 0.5f) * inv;
        a1 = (-2.f * cw) * inv;
        a2 = (1.f - al) * inv;
    }

    void SetHighPass(float sr, float fc, float Q)
    {
        const float w0 = TWO_PI * fc / sr;
        const float cw = cosf(w0);
        const float sw = sinf(w0);
        const float al = sw / (2.f * Q);
        const float a0v = 1.f + al;
        const float inv = 1.f / a0v;
        b0 = ((1.f + cw) * 0.5f) * inv;
        b1 = -(1.f + cw) * inv;
        b2 = ((1.f + cw) * 0.5f) * inv;
        a1 = (-2.f * cw) * inv;
        a2 = (1.f - al) * inv;
    }
};

// Three-band EQ for preset 0 (mono signal path).
Biquad eqBass;
Biquad eqMid;
Biquad eqTreble;

// CAB simulator low-pass for preset 3 (lead chain).
Biquad cabLP;

// Custom noise gate for preset 3 (DaisySP doesn't ship one).
struct NoiseGate
{
    float env = 0.f;
    float gain = 1.f;
    float threshold = 0.f;
    float env_coef = 0.f;
    float att_coef = 0.f;
    float rel_coef = 0.f;

    void Init(float sr)
    {
        env_coef = expf(-1.f / (0.010f * sr));
        att_coef = expf(-1.f / (0.005f * sr));
        rel_coef = expf(-1.f / (0.200f * sr));
    }

    void SetThreshold(float t) { threshold = t; }

    inline float Process(float x)
    {
        float ax = fabsf(x);
        if (ax > env)
            env = ax;
        else
            env = env * env_coef + ax * (1.f - env_coef);

        float target = (env > threshold) ? 1.f : 0.f;
        if (target > gain)
            gain = gain * att_coef + target * (1.f - att_coef);
        else
            gain = gain * rel_coef + target * (1.f - rel_coef);

        return x * gain;
    }
};

NoiseGate noiseGate;

Overdrive distortion;
Overdrive ampLead;
DSY_SDRAM_BSS DelayLine<float, 48000> leadDelay;

// Ambient chain (preset 2) DSP objects
DSY_SDRAM_BSS DelayLine<float, 48000> ambientDelay;

// HiGain 808 chain (preset 4) DSP objects
Biquad hg808HP;       // tighten lows before clipper
Biquad hg808Peak;     // TS-808 mid hump @ 700 Hz
Overdrive hg808Drive; // saturation stage

// Lightweight white-noise generator (retired: didn't sound good in HiGain chain).
// inline float WhiteNoiseGenerator()
// {
//     static uint32_t lcg = 0x12345678u;
//     lcg = lcg * 1103515245u + 12345u;
//     return (float)((int32_t)(lcg & 0x00FFFFFFu) - 0x00800000) * (1.f / 0x00800000);
// }

RTNeural::ModelT<float, 1, 1,
                 RTNeural::GRULayerT<float, 1, 10>,
                 RTNeural::DenseT<float, 10, 1>>
    neuralModel;

// ──────────────────────────────────────────────
// Runtime state
// ──────────────────────────────────────────────

uint8_t currentEffect = EFFECT_EQ;
bool passthrough = false;
bool editingEnabled = false;
bool prevEditing = false;

float lastPotValue[NUM_POTS] = {-1.f, -1.f, -1.f, -1.f};

// ──────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────

inline float clamp01(float v)
{
    return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}

inline float ReadPot(int ch)
{
    return clamp01(hw.adc.GetFloat(ch));
}

// Snapshot current pot positions — pick-up / catch-up deadband baseline.
void SyncPotBaseline()
{
    for (int i = 0; i < NUM_POTS; i++)
        lastPotValue[i] = ReadPot(i);
}

void InitNeuralSeedModel()
{
    NeuralSeedModelData neuralData = CreateSelectedNeuralSeedModelData();

    auto &gru = neuralModel.get<0>();
    auto &dense = neuralModel.get<1>();

    gru.setWVals(neuralData.rec_weight_ih_l0);
    gru.setUVals(neuralData.rec_weight_hh_l0);
    gru.setBVals(neuralData.rec_bias);
    dense.setWeights(neuralData.lin_weight);
    dense.setBias(neuralData.lin_bias.data());

    neuralModel.reset();
}

// ──────────────────────────────────────────────
// Apply saved state → DSP objects
// ──────────────────────────────────────────────

void ApplyEffectState()
{
    const EffectState &s = effectStates[currentEffect];

    switch (static_cast<Effect>(currentEffect))
    {
    case EFFECT_EQ:
    {
        // Map pots [0..1] -> dB [-EQ_GAIN_DB .. +EQ_GAIN_DB]; centered = 0 dB.
        const float bass_db = (s.params[1] - 0.5f) * 2.f * EQ_GAIN_DB;
        const float mid_db = (s.params[2] - 0.5f) * 2.f * EQ_GAIN_DB;
        const float treble_db = (s.params[3] - 0.5f) * 2.f * EQ_GAIN_DB;
        eqBass.SetLowShelf(SAMPLE_RATE, EQ_BASS_FC, bass_db);
        eqMid.SetPeak(SAMPLE_RATE, EQ_MID_FC, EQ_MID_Q, mid_db);
        eqTreble.SetHighShelf(SAMPLE_RATE, EQ_TREBLE_FC, treble_db);
        break;
    }

        // case EFFECT_OVERDRIVE:
        //     overdrive.SetDrive(s.params[0]);
        //     break;

    case EFFECT_FUNK:
    {
        // pot0 -> autowah swing depth (envelope-driven cutoff motion).
        autowah.SetWah(s.params[1]);
        // Reassert the reverb voicing for funk, since preset 2 may have
        // overwritten feedback/LpFreq with its own knob values.
        reverb.SetFeedback(0.45f); // medium-short tail
        reverb.SetLpFreq(7000.f);  // darker, sits behind the part
        break;
    }

        // case EFFECT_CHORUS:
        //     chorus.SetLfoFreq(s.params[0] * 5.f);   // 0–5 Hz
        //     chorus.SetDelay(s.params[1]);
        //     break;

    case EFFECT_AMBIENT:
    {
        // pot0 -> delay length (100..800 ms, linear)
        const float delay_s = AMBIENT_DELAY_MIN_S + s.params[1] * (AMBIENT_DELAY_MAX_S - AMBIENT_DELAY_MIN_S);
        ambientDelay.SetDelay(delay_s * SAMPLE_RATE);

        // Re-assert ambient reverb voicing (long, bright tail).
        reverb.SetFeedback(AMBIENT_REVERB_FB);
        reverb.SetLpFreq(AMBIENT_REVERB_LP);
        break;
    }

        // case EFFECT_REVERB:   // earlier preset 2
        //     reverb.SetFeedback(s.params[0]);
        //     reverb.SetLpFreq(s.params[1] * 18000.f);
        //     break;

    case EFFECT_LEAD:
    {
        // Knob-driven parameters that need a setter call.
        distortion.SetDrive(s.params[1]);                            // pot1 = combined gain knob
        noiseGate.SetThreshold(s.params[3] * NOISE_GATE_MAX_THRESH); // pot3 = gate threshold
        // Re-assert lead-flavored reverb voicing in case preset 2 overwrote it.
        reverb.SetFeedback(LEAD_REVERB_FB);
        reverb.SetLpFreq(LEAD_REVERB_LP);
        break;
    }

    // case EFFECT_PHASER:
    //     phaser.SetFreq(s.params[0] * 10.f);     // 0–10 Hz
    //     phaser.SetFeedback(s.params[2]);
    //     break;
    case EFFECT_HIGAIN:
        hg808Drive.SetDrive(s.params[2]); // pot2 = drive
        hg808Peak.SetPeak(SAMPLE_RATE, HG808_PEAK_FC, HG808_PEAK_Q,
                          s.params[3] * HG808_PEAK_GAIN_DB); // pot3 = tone (mid hump)
        break;
    case EFFECT_NEURALSEED:
        break;
    }
}

// ──────────────────────────────────────────────
// Audio Callback
// ──────────────────────────────────────────────

void AudioCallback(AudioHandle::InputBuffer in,
                   AudioHandle::OutputBuffer out,
                   size_t size)
{
    const EffectState &s = effectStates[currentEffect];

    for (size_t i = 0; i < size; i++)
    {
        float inL = in[0][i];
        float inR = in[1][i];

        if (passthrough)
        {
            out[0][i] = inL;
            out[1][i] = inR;
            continue;
        }

        float outL = inL;
        float outR = inR;

        switch (static_cast<Effect>(currentEffect))
        {
        case EFFECT_EQ:
        {
            // 3-band EQ in series, then output-level trim.
            // Mono path (single physical input jack on the schematic).
            // pot0 = level (0..2x linear, 0.5 = unity)
            // pot1/2/3 = bass/mid/treble (-12..+12 dB, centered = flat)
            const float gain = s.params[0] * 2.f;
            float y = inL;
            y = eqBass.Process(y);
            y = eqMid.Process(y);
            y = eqTreble.Process(y);
            y *= gain;
            outL = y;
            outR = y;
            break;
        }

            // case EFFECT_OVERDRIVE:
            // {
            //     // pot0=drive (on DSP obj), pot1=tone blend, pot2=level
            //     float driven = overdrive.Process(inL);
            //     outL = driven * s.params[1] + inL * (1.f - s.params[1]);
            //     outL *= s.params[2] * 2.f;
            //     outR  = outL;
            //     break;
            // }

        case EFFECT_FUNK:
        {
            // Funk chain: Compressor -> AutoWah -> AMP -> Reverb -> output gain.
            // Mono signal path; both output channels carry the same sample.
            // pot0 = wah depth (set in ApplyEffectState)
            // pot1 = compressor parallel mix (here, per-sample)
            // pot2 = reverb wet/dry (here, per-sample)
            // pot3 = output gain (here, per-sample, 0..2x linear)
            const float out_gain = s.params[0] * 2.f; // pot0 = output volume
            const float comp_mix = s.params[2];       // pot2 = compressor parallel mix
            const float reverb_mix = s.params[3];     // pot3 = reverb wet/dry

            // 1. Parallel compression: blend dry with fully-compressed.
            float dry = inL;
            float compd = compressor.Process(dry);
            float comp_out = dry * (1.f - comp_mix) + compd * comp_mix;

            // 2. Auto-wah (envelope-driven resonant filter).
            float wah_out = autowah.Process(comp_out);

            // 3. AMP: gentle soft-clip saturation (fixed character).
            float amp_out = amp.Process(wah_out);

            // 4. Reverb: stereo out from a mono source.
            float wetL = 0.f, wetR = 0.f;
            reverb.Process(amp_out, amp_out, &wetL, &wetR);
            float mixL = amp_out * (1.f - reverb_mix) + wetL * reverb_mix;
            float mixR = amp_out * (1.f - reverb_mix) + wetR * reverb_mix;

            outL = mixL * out_gain;
            outR = mixR * out_gain;
            break;
        }

            // case EFFECT_CHORUS:
            // {
            //     // Chorus::Process is mono — process L and R separately
            //     // pot2=dry/wet mix
            //     float choL = chorus.Process(inL);
            //     float choR = chorus.Process(inR);
            //     outL = inL * (1.f - s.params[2]) + choL * s.params[2];
            //     outR = inR * (1.f - s.params[2]) + choR * s.params[2];
            //     break;
            // }

        case EFFECT_AMBIENT:
        {
            // Dreamy chain: Chorus -> Delay -> Reverb -> output gain.
            // pot0 = delay length (set in ApplyEffectState)
            // pot1 = reverb strength (additive)
            // pot2 = chorus mix (crossfade)
            // pot3 = output gain
            const float out_gain = s.params[0] * 2.f; // pot0 = output volume
            const float reverb_str = s.params[2];     // pot2 = reverb strength
            const float chorus_mix = s.params[3];     // pot3 = chorus mix

            // 1. Chorus: dry/wet crossfade.
            float chOut = chorus.Process(inL);
            float afterChorus = inL * (1.f - chorus_mix) + chOut * chorus_mix;

            // 2. Delay: feedback echo with fixed 50% wet blend; length is the variable.
            float wetDelay = ambientDelay.Read();
            ambientDelay.Write(afterChorus + wetDelay * AMBIENT_DELAY_FB);
            float afterDelay = afterChorus + wetDelay * AMBIENT_DELAY_MIX;

            // 3. Reverb: additive wet on top, scaled by strength pot.
            float rL = 0.f, rR = 0.f;
            reverb.Process(afterDelay, afterDelay, &rL, &rR);
            float wetReverb = (rL + rR) * 0.5f;
            float withReverb = afterDelay + wetReverb * reverb_str;

            // 4. Output gain.
            float final = withReverb * out_gain;
            outL = final;
            outR = final;
            break;
        }

            // case EFFECT_REVERB:   // earlier preset 2
            // {
            //     reverb.Process(inL, inR, &outL, &outR);
            //     outL = inL + outL * s.params[2];
            //     outR = inR + outR * s.params[2];
            //     break;
            // }

        case EFFECT_LEAD:
        {
            // Lead chain: Boost -> Distortion -> AMP -> CAB -> Reverb -> Delay -> NoiseGate.
            // pot0 = boost gain (1..5x linear)
            // pot1 = reverb + delay depth (shared crossfade)
            // pot2 = noise gate threshold (set in ApplyEffectState)
            // pot3 = distortion drive (set in ApplyEffectState)
            const float volume = s.params[0] * 2.f;                              // pot0 = output volume
            const float boost_gain = 1.f + s.params[1] * (LEAD_BOOST_MAX - 1.f); // pot1 = gain (boost half)
            const float wet_mix = s.params[2];                                   // pot2 = reverb/delay depth

            float x = inL * boost_gain;
            x = distortion.Process(x);
            x = ampLead.Process(x);
            x = cabLP.Process(x);

            float rL = 0.f, rR = 0.f;
            reverb.Process(x, x, &rL, &rR);
            float wetReverb = (rL + rR) * 0.5f;
            float reverbOut = x * (1.f - wet_mix) + wetReverb * wet_mix;

            float wetDelay = leadDelay.Read();
            leadDelay.Write(reverbOut + wetDelay * LEAD_DELAY_FB);
            float delayOut = reverbOut * (1.f - wet_mix) + wetDelay * wet_mix;

            float gated = noiseGate.Process(delayOut) * volume;
            outL = gated;
            outR = gated;
            break;
        }

        // case EFFECT_PHASER:
        // {
        //     float phOut = phaser.Process(inL);
        //     outL = inL * (1.f - s.params[1]) + phOut * s.params[1];
        //     outR = outL;
        //     break;
        // }
        case EFFECT_HIGAIN:
        {
            const float volume = s.params[0] * 2.f;
            const float in_gain = 1.f + s.params[1] * (HG808_GAIN_MAX - 1.f);
            // const float noise_amt = s.params[3] * HG808_NOISE_MAX;  // retired: noise removed
            float x = inL * in_gain;
            x = hg808HP.Process(x);
            x = hg808Peak.Process(x);
            // x = x + WhiteNoiseGenerator() * noise_amt;   // retired: noise removed
            x = hg808Drive.Process(x);
            x *= volume;
            outL = x;
            outR = x;
            break;
        }
        case EFFECT_NEURALSEED:
        {
            // pot0=input gain, pot1=dry/wet mix, pot2=output level
            float gainedIn = inL * (s.params[0] * 3.f);
            float neuralIn[1] = {gainedIn};
            float wet = neuralModel.forward(neuralIn) + inL;
            float mix = s.params[1];
            float dryGain = cosf(mix * HALF_PI);
            float wetGain = sinf(mix * HALF_PI);
            outL = (inL * dryGain + wet * wetGain) * s.params[2];
            outR = outL;
            break;
        }
        }
        out[0][i] = outL;
        out[1][i] = outR;
    }
}

// ──────────────────────────────────────────────
// main
// ──────────────────────────────────────────────

int main()
{
    // ── System init ────────────────────────────
    hw.Init();
    hw.SetAudioBlockSize(48);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    // ── ADC init ──────────────────────────────
    // Pass Pin objects directly — do NOT wrap in hw.GetPin()
    AdcChannelConfig adcCfg[NUM_ADC];
    adcCfg[ADC_POT0].InitSingle(POT0_PIN);
    adcCfg[ADC_POT1].InitSingle(POT1_PIN);
    adcCfg[ADC_POT2].InitSingle(POT2_PIN);
    adcCfg[ADC_POT3].InitSingle(POT3_PIN);
    hw.adc.Init(adcCfg, NUM_ADC);
    hw.adc.Start();

    // ── Switch init ───────────────────────────
    // Switch::Init(Pin, update_rate_hz, type, polarity)
    // hw.GetPin(uint8_t) returns a Pin for digital GPIO pins
    toggleEdit.Init(hw.GetPin(PIN_TOGGLE_EDIT),
                    SWITCH_UPDATE_RATE_HZ,
                    Switch::TYPE_TOGGLE,
                    Switch::POLARITY_NORMAL);

    togglePassthrough.Init(hw.GetPin(PIN_TOGGLE_PASSTHROUGH),
                           SWITCH_UPDATE_RATE_HZ,
                           Switch::TYPE_TOGGLE,
                           Switch::POLARITY_NORMAL);

    btnEffectCycle.Init(hw.GetPin(PIN_BTN_EFFECT_CYCLE),
                        SWITCH_UPDATE_RATE_HZ,
                        Switch::TYPE_MOMENTARY,
                        Switch::POLARITY_NORMAL);

    // ── DSP init ──────────────────────────────
    // overdrive.Init();
    // overdrive.SetDrive(0.5f);

    // EQ: start flat (0 dB on every band) and clear filter state.
    eqBass.SetLowShelf(SAMPLE_RATE, EQ_BASS_FC, 0.f);
    eqMid.SetPeak(SAMPLE_RATE, EQ_MID_FC, EQ_MID_Q, 0.f);
    eqTreble.SetHighShelf(SAMPLE_RATE, EQ_TREBLE_FC, 0.f);
    eqBass.Reset();
    eqMid.Reset();
    eqTreble.Reset();

    // Chorus: ambient voicing (slow, deep) — only consumer is Ambient preset.
    chorus.Init(SAMPLE_RATE);
    chorus.SetLfoFreq(AMBIENT_CHORUS_RATE);
    chorus.SetLfoDepth(AMBIENT_CHORUS_DEPTH);

    // Ambient delay: cleared buffer, default length set in ApplyEffectState.
    ambientDelay.Init();
    ambientDelay.SetDelay(AMBIENT_DELAY_MIN_S * SAMPLE_RATE);

    // Funk chain components (preset 1).
    // Compressor: fixed musical setting; only the parallel-blend knob (pot1) moves.
    compressor.Init(SAMPLE_RATE);
    compressor.SetRatio(4.f);       // 4:1 — moderate funk squash
    compressor.SetThreshold(-20.f); // dB; engages well above noise floor
    compressor.SetAttack(0.005f);   // 5 ms — preserves pick attack
    compressor.SetRelease(0.1f);    // 100 ms — smooth recovery
    compressor.AutoMakeup(true);    // automatic gain compensation

    // Autowah: depth (pot0) is set in ApplyEffectState; level/drywet fixed here.
    autowah.Init(SAMPLE_RATE);
    autowah.SetLevel(0.5f);
    autowah.SetDryWet(100.f); // internal mix fully wet; depth controls swing

    // AMP: light overdrive for harmonic warmth, no real distortion.
    amp.Init();
    amp.SetDrive(0.3f);

    reverb.Init(SAMPLE_RATE);
    reverb.SetFeedback(0.6f);
    reverb.SetLpFreq(9000.f);

    // phaser.Init(SAMPLE_RATE);

    distortion.Init();
    ampLead.Init();
    ampLead.SetDrive(LEAD_AMP_DRIVE);
    cabLP.SetLowPass(SAMPLE_RATE, LEAD_CAB_FC, LEAD_CAB_Q);
    cabLP.Reset();
    leadDelay.Init();
    leadDelay.SetDelay(LEAD_DELAY_TIME_S * SAMPLE_RATE);
    noiseGate.Init(SAMPLE_RATE);
    hg808HP.SetHighPass(SAMPLE_RATE, HG808_HP_FC, HG808_HP_Q);
    hg808HP.Reset();
    hg808Peak.SetPeak(SAMPLE_RATE, HG808_PEAK_FC, HG808_PEAK_Q, HG808_PEAK_GAIN_DB);
    hg808Peak.Reset();
    hg808Drive.Init();
    hg808Drive.SetDrive(0.5f);

    InitNeuralSeedModel();

    ApplyEffectState();

    // ── Start audio ───────────────────────────
    hw.StartAudio(AudioCallback);

    // ── Main control loop (~1 kHz) ────────────
    while (true)
    {
        toggleEdit.Debounce();
        togglePassthrough.Debounce();
        btnEffectCycle.Debounce();

        // Bottom toggle: passthrough (reads live physical state)
        passthrough = togglePassthrough.Pressed();

        // Top toggle: editing gate
        editingEnabled = true;

        // Rising edge of editing gate: snapshot pot positions (pick-up behaviour)
        if (editingEnabled && !prevEditing)
            SyncPotBaseline();

        prevEditing = editingEnabled;

        // Push button: cycle effect preset
        if (btnEffectCycle.RisingEdge())
        {
            currentEffect = (currentEffect + 1) % NUM_EFFECTS;
            ApplyEffectState();
            if (currentEffect == EFFECT_NEURALSEED)
                neuralModel.reset();
            SyncPotBaseline();
        }

        // Pots → effect parameter state (only when editing, not bypassed)
        if (editingEnabled && !passthrough)
        {
            bool changed = false;

            for (int p = 0; p < NUM_POTS; p++)
            {
                float raw = ReadPot(p);

                if (fabsf(raw - lastPotValue[p]) > ADC_DEADBAND)
                {
                    lastPotValue[p] = raw;
                    effectStates[currentEffect].params[p] = raw;
                    changed = true;
                }
            }

            if (changed)
                ApplyEffectState();
        }

        System::Delay(1);
    }
}
