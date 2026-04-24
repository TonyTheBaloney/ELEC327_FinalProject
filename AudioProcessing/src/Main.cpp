#include "daisy.h"
#include "daisy_seed.h"

// Use the daisy namespace to prevent having to type
// daisy:: before all libdaisy functions
using namespace daisy;

// Declare a DaisySeed object called hardware
DaisySeed hardware;

#define POTENTIOMETER_PIN_ONE 23
#define POTENTIOMETER_PIN_TWO 25
#define POTENTIOMETER_PIN_THREE 27
#define POTENTIOMETER_PIN_FOUR 29
#define BUTTON_PIN_ONE 2
#define BUTTON_PIN_TWO 3

#define MSPM0_SDA_PIN 6
#define MSPM0_SCL_PIN 7

typedef struct {
    uint8_t potentiometer_one;
    uint8_t potentiometer_two;
    uint8_t potentiometer_three;
    uint8_t potentiometer_four;
    uint8_t button_one;
    uint8_t button_two;
    // The state we're currently in for the MSP to display
}ControlState;

static ControlState control_state;

void AudioPassthroughCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    // This callback is called when the audio system needs more data to play.
    // The input and output buffers are passed as arguments, along with the
    // number of samples that need to be processed.

    // For this example, we'll just copy the input buffer to the output buffer,
    // effectively creating a "pass-through" effect.
    for(size_t i = 0; i < size; i++)
    {
        out[1][i] = in[1][i]; // Right channel
        out[0][i] = in[0][i]; // Left channel
    }
}


int main(void)
{
    // Declare a variable to store the state we want to set for the LED.
    bool led_state;
    led_state = true;

    // Configure and Initialize the Daisy Seed
    // These are separate to allow reconfiguration of any of the internal
    // components before initialization.
    hardware.Configure();
    hardware.Init();
    hardware.SetAudioBlockSize(4);

    // Initializing the ADC for the potentiometers.
    AdcChannelConfig adc_config;
    adc_config.InitSingle(hardware.GetPin(POTENTIOMETER_PIN_ONE));
    adc_config.InitSingle(hardware.GetPin(POTENTIOMETER_PIN_TWO));
    adc_config.InitSingle(hardware.GetPin(POTENTIOMETER_PIN_THREE));
    adc_config.InitSingle(hardware.GetPin(POTENTIOMETER_PIN_FOUR));
    hardware.adc.Init(&adc_config, 5);
    hardware.adc.Start();
    
    // Initialize button 
    GPIO button_gpio;
    button_gpio.Init(hardware.GetPin(BUTTON_PIN_ONE), GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    GPIO button_gpio_two;
    button_gpio_two.Init(hardware.GetPin(BUTTON_PIN_TWO), GPIO::Mode::INPUT, GPIO::Pull::PULLUP);
    

    // Setting up I2C for communication with the MSPM0
    daisy::I2CHandle::Config i2c_config;
    i2c_config.periph = I2CHandle::Config::Peripheral::I2C_1;
    i2c_config.mode = I2CHandle::Config::Mode::I2C_MASTER;
    i2c_config.speed = I2CHandle::Config::Speed::I2C_400KHZ;
    i2c_config.pin_config.scl = hardware.GetPin(MSPM0_SCL_PIN);
    i2c_config.pin_config.sda = hardware.GetPin(MSPM0_SDA_PIN);
    
    daisy::I2CHandle i2c;
    if (i2c.Init(i2c_config) != I2CHandle::Result::OK)
    {
        // Handle error
    }




    float sample_rate = hardware.AudioSampleRate();
    (void)sample_rate;
    hardware.StartAudio(AudioPassthroughCallback);

    // Loop forever
    for(;;)
    {        
        control_state.potentiometer_one = hardware.adc.Get(POTENTIOMETER_PIN_ONE);
        control_state.potentiometer_two = hardware.adc.Get(POTENTIOMETER_PIN_TWO);
        control_state.potentiometer_three = hardware.adc.Get(POTENTIOMETER_PIN_THREE);
        control_state.potentiometer_four = hardware.adc.Get(POTENTIOMETER_PIN_FOUR);
        control_state.button_one = hardware.adc.Get(BUTTON_PIN_ONE);
        control_state.button_two = hardware.adc.Get(BUTTON_PIN_TWO);
        //i2c.TransmitBlocking(0x10, (uint8_t*)data, sizeof(data) / sizeof(data[0]), 1000);

        // Wait 500ms
        System::Delay(500);
    }
}
