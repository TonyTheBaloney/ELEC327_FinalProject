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
static constexpr uint8_t PIN_TOGGLE_EDIT        = 1;   // D1 -> top toggle
static constexpr uint8_t PIN_TOGGLE_PASSTHROUGH = 2;   // D2 -> bottom toggle
static constexpr uint8_t PIN_BTN_EFFECT_CYCLE   = 3;   // D3 -> push button

// ──────────────────────────────────────────────
// ADC channel indices (must match Init order)
// ──────────────────────────────────────────────
static constexpr int ADC_POT0 = 0;
static constexpr int ADC_POT1 = 1;
static constexpr int ADC_POT2 = 2;
static constexpr int ADC_POT3 = 3;
static constexpr int NUM_ADC  = 4;
static constexpr int NUM_POTS = 4;

// ──────────────────────────────────────────────
// Constants
// ──────────────────────────────────────────────

static constexpr int   NUM_EFFECTS  = 5;
static constexpr float SAMPLE_RATE  = 48000.f;
static constexpr float ADC_DEADBAND = 0.005f;   // ~0.5% — suppresses pot noise

// ──────────────────────────────────────────────
// Effect Parameter State
// ──────────────────────────────────────────────

enum Effect : uint8_t
{
    EFFECT_OVERDRIVE = 0,
    EFFECT_CHORUS,
    EFFECT_REVERB,
    EFFECT_PHASER,
    EFFECT_NEURALSEED
};

struct EffectState
{
    float params[NUM_POTS];   // normalised 0–1, one per pot
};

static EffectState effectStates[NUM_EFFECTS] = {
    {{ 0.5f, 0.5f, 0.5f, 0.5f }},   // Overdrive
    {{ 0.3f, 0.5f, 0.5f, 0.5f }},   // Chorus
    {{ 0.6f, 0.5f, 0.5f, 0.5f }},   // Reverb
    {{ 0.4f, 0.5f, 0.3f, 0.5f }},   // Phaser
    {{ 0.33f, 1.0f, 0.5f, 0.5f }},  // NeuralSeed
};

// ──────────────────────────────────────────────
// Global hardware & DSP objects
// ──────────────────────────────────────────────

DaisySeed hw;

Switch toggleEdit;
Switch togglePassthrough;
Switch btnEffectCycle;

Overdrive overdrive;
Chorus    chorus;
ReverbSc    reverb;   // daisysp::Reverb (not ReverbSc)
Phaser    phaser;

RTNeural::ModelT<float, 1, 1,
    RTNeural::GRULayerT<float, 1, 10>,
    RTNeural::DenseT<float, 10, 1>> neuralModel;

// ──────────────────────────────────────────────
// Runtime state
// ──────────────────────────────────────────────

uint8_t currentEffect  = EFFECT_CHORUS;
bool    passthrough    = false;
bool    editingEnabled = false;
bool    prevEditing    = false;

float lastPotValue[NUM_POTS] = { -1.f, -1.f, -1.f, -1.f };

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

    auto& gru   = neuralModel.get<0>();
    auto& dense = neuralModel.get<1>();

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
    const EffectState& s = effectStates[currentEffect];

    switch (static_cast<Effect>(currentEffect))
    {
        case EFFECT_OVERDRIVE:
            overdrive.SetDrive(s.params[0]);
            break;

        case EFFECT_CHORUS:
            chorus.SetLfoFreq(s.params[0] * 5.f);   // 0–5 Hz
            chorus.SetDelay(s.params[1]);
            break;

        case EFFECT_REVERB:
            reverb.SetFeedback(s.params[0]);
            reverb.SetLpFreq(s.params[1] * 18000.f);
            break;

        case EFFECT_PHASER:
            phaser.SetFreq(s.params[0] * 10.f);     // 0–10 Hz
            phaser.SetFeedback(s.params[2]);
            break;

        case EFFECT_NEURALSEED:
            break;
    }
}

// ──────────────────────────────────────────────
// Audio Callback
// ──────────────────────────────────────────────

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    const EffectState& s = effectStates[currentEffect];

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
            case EFFECT_OVERDRIVE:
            {
                // pot0=drive (on DSP obj), pot1=tone blend, pot2=level
                float driven = overdrive.Process(inL);
                outL = driven * s.params[1] + inL * (1.f - s.params[1]);
                outL *= s.params[2] * 2.f;
                outR  = outL;
                break;
            }

            case EFFECT_CHORUS:
            {
                // Chorus::Process is mono — process L and R separately
                // pot2=dry/wet mix
                float choL = chorus.Process(inL);
                float choR = chorus.Process(inR);
                outL = inL * (1.f - s.params[2]) + choL * s.params[2];
                outR = inR * (1.f - s.params[2]) + choR * s.params[2];
                break;
            }

            case EFFECT_REVERB:
            {
                // Reverb::Process is mono — pot2=wet level
                reverb.Process(inL, inR, &outL, &outR);
                outL = inL + outL * s.params[2];
                outR = inR + outR * s.params[2];
                break;
            }

            case EFFECT_PHASER:
            {
                // pot1=depth / wet mix
                float phOut = phaser.Process(inL);
                outL = inL * (1.f - s.params[1]) + phOut * s.params[1];
                outR = outL;
                break;
            }

            case EFFECT_NEURALSEED:
            {
                // pot0=input gain, pot1=dry/wet mix, pot2=output level
                float gainedIn = inL * (s.params[0] * 3.f);
                float neuralIn[1] = { gainedIn };
                float wet = neuralModel.forward(neuralIn) + gainedIn;
                outL = (inL * (1.f - s.params[1]) + wet * s.params[1]) * s.params[2];
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
    // Switch::Init(Pin, debounce_ms, type, polarity)
    // hw.GetPin(uint8_t) returns a Pin for digital GPIO pins
    toggleEdit.Init(hw.GetPin(PIN_TOGGLE_EDIT),
                    1000,
                    Switch::TYPE_TOGGLE,
                    Switch::POLARITY_NORMAL);

    togglePassthrough.Init(hw.GetPin(PIN_TOGGLE_PASSTHROUGH),
                           1000,
                           Switch::TYPE_TOGGLE,
                           Switch::POLARITY_NORMAL);

    btnEffectCycle.Init(hw.GetPin(PIN_BTN_EFFECT_CYCLE),
                        1000,
                        Switch::TYPE_MOMENTARY,
                        Switch::POLARITY_NORMAL);

    // ── DSP init ──────────────────────────────
    overdrive.Init();
    overdrive.SetDrive(0.5f);

    chorus.Init(SAMPLE_RATE);

    reverb.Init(SAMPLE_RATE);
    reverb.SetFeedback(0.6f);
    reverb.SetLpFreq(9000.f);

    phaser.Init(SAMPLE_RATE);

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
        editingEnabled = toggleEdit.Pressed();

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
                    lastPotValue[p]                       = raw;
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
