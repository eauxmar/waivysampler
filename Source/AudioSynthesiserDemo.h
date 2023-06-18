#pragma once
#include <cstdlib>
#include "DemoUtilities.h"
#include "AudioLiveScrollingDisplay.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include "juce_gui_basics/juce_gui_basics.h"
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>

//==============================================================================
/** Our demo synth sound is just a basic sine wave.. */
struct SineWaveSound : public SynthesiserSound
{
    SineWaveSound() {}

    bool appliesToNote (int /*midiNoteNumber*/) override    { return true; }
    bool appliesToChannel (int /*midiChannel*/) override    { return true; }
};

//==============================================================================
/** Our demo synth voice just plays a sine wave.. */
struct SineWaveVoice  : public SynthesiserVoice
{
    SineWaveVoice() : adsr(), adsrParams() {
        adsrParams.attack = 0.1f;
        adsrParams.decay = 0.1f;
        adsrParams.sustain = 1.0f;
        adsrParams.release = 0.1f;

        adsr.setParameters(adsrParams);
    }

    bool canPlaySound (SynthesiserSound* sound) override
    {
        return dynamic_cast<SineWaveSound*> (sound) != nullptr;
    }

    void startNote (int midiNoteNumber, float velocity,
                    SynthesiserSound*, int /*currentPitchWheelPosition*/) override
    {
        currentAngle = 0.0;
        level = velocity * 0.15;
        tailOff = 0.0;

        auto cyclesPerSecond = MidiMessage::getMidiNoteInHertz (midiNoteNumber);
        auto cyclesPerSample = cyclesPerSecond / getSampleRate();

        angleDelta = cyclesPerSample * MathConstants<double>::twoPi;

        // Set ADSR parameters here
        adsrParams.attack = 0.1;    // Attack time in seconds
        adsrParams.decay = 0.1;     // Decay time in seconds
        adsrParams.sustain = 1.0;   // Sustain level (0.0 - 1.0)
        adsrParams.release = 0.2;   // Release time in seconds

        adsr.setParameters(adsrParams);
        adsr.noteOn(); // Start the envelope
    }

    void stopNote (float /*velocity*/, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            // start a tail-off by setting this flag. The render callback will pick up on
            // this and do a fade out, calling clearCurrentNote() when it's finished.

            if (tailOff == 0.0) // we only need to begin a tail-off if it's not already doing so - the
                tailOff = 1.0;  // stopNote method could be called more than once.
        }
        else
        {
            // we're being told to stop playing immediately, so reset everything..
            clearCurrentNote();
            angleDelta = 0.0;
        }
        adsr.noteOff(); // Stop the envelope
    }

    void pitchWheelMoved (int /*newValue*/) override                              {}
    void controllerMoved (int /*controllerNumber*/, int /*newValue*/) override    {}

    void renderNextBlock (AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override
    {
        if (angleDelta != 0.0)
        {
            if (tailOff > 0.0)
            {
                while (--numSamples >= 0)
                {
                    auto currentSample = (float) (std::sin (currentAngle) * level * tailOff);

                    for (auto i = outputBuffer.getNumChannels(); --i >= 0;)
                        outputBuffer.addSample (i, startSample, currentSample);

                    currentAngle += angleDelta;
                    ++startSample;

                    tailOff *= 0.99;

                    if (tailOff <= 0.005)
                    {
                        clearCurrentNote();

                        angleDelta = 0.0;
                        break;
                    }
                }
            }
            else
            {
                while (--numSamples >= 0)
                {
                    auto currentSample = (float) (std::sin (currentAngle) * level);

                    for (auto i = outputBuffer.getNumChannels(); --i >= 0;)
                        outputBuffer.addSample (i, startSample, currentSample);

                    currentAngle += angleDelta;
                    ++startSample;
                }
            }
            adsr.applyEnvelopeToBuffer (outputBuffer, 0, numSamples);
        }

        while (--numSamples >= 0)
        {
            auto currentSample = (float) (std::sin (currentAngle) * level);

            // Apply the ADSR envelope to the current sample
            currentSample *= adsr.getNextSample();

            for (auto i = outputBuffer.getNumChannels(); --i >= 0;)
                outputBuffer.addSample (i, startSample, currentSample);

            currentAngle += angleDelta;
            ++startSample;
        }
    }

    using SynthesiserVoice::renderNextBlock;

private:
    double currentAngle = 0.0, angleDelta = 0.0, level = 0.0, tailOff = 0.0;
    ADSR adsr;
    ADSR::Parameters adsrParams;
};

//==============================================================================
// This is an audio source that streams the output of our demo synth.
struct SynthAudioSource  : public AudioSource
{
    SynthAudioSource (MidiKeyboardState& keyState)  : keyboardState (keyState)
    {
        // Add some voices to our synth, to play the sounds..
        for (auto i = 0; i < 4; ++i)
        {
            synth.addVoice (new SineWaveVoice());   // These voices will play our custom sine-wave sounds..
            synth.addVoice (new SamplerVoice());    // and these ones play the sampled sounds
        }

        // ..and add a sound for them to play...
        setUsingSineWaveSound();
    }

    void setUsingSineWaveSound()
    {
        synth.clearSounds();
        synth.addSound (new SineWaveSound());
    }

    void setUsingSampledSound()
    {
        WavAudioFormat wavFormat;

        std::unique_ptr<AudioFormatReader> audioReader (wavFormat.createReaderFor (createAssetInputStream ("/Users/omar/Documents/THESIS_CODE/argparse_output/closest_match.wav").release(), true));

        BigInteger allNotes;
        allNotes.setRange (0, 128, true);

        synth.clearSounds();
        synth.addSound (new SamplerSound ("demo sound",
                                          *audioReader,
                                          allNotes,
                                          74,   // root midi note
                                          attackSlider.getValue(),  // attack time
                                          0.1,  // release time
                                          10.0  // maximum sample length
                                          ));
    }

    void prepareToPlay (int /*samplesPerBlockExpected*/, double sampleRate) override
    {
        midiCollector.reset (sampleRate);

        synth.setCurrentPlaybackSampleRate (sampleRate);
    }

    void releaseResources() override
    {
    }

    void getNextAudioBlock (const AudioSourceChannelInfo& bufferToFill) override
    {
        // the synth always adds its output to the audio buffer, so we have to clear it
        // first..
        bufferToFill.clearActiveBufferRegion();

        // fill a midi buffer with incoming messages from the midi input.
        MidiBuffer incomingMidi;
        midiCollector.removeNextBlockOfMessages (incomingMidi, bufferToFill.numSamples);

        // pass these messages to the keyboard state so that it can update the component
        // to show on-screen which keys are being pressed on the physical midi keyboard.
        // This call will also add midi messages to the buffer which were generated by
        // the mouse-clicking on the on-screen keyboard.
        keyboardState.processNextMidiBuffer (incomingMidi, 0, bufferToFill.numSamples, true);

        // and now get the synth to process the midi events and generate its output.
        synth.renderNextBlock (*bufferToFill.buffer, incomingMidi, 0, bufferToFill.numSamples);
    }

    //==============================================================================
    // this collects real-time midi messages from the midi input device, and
    // turns them into blocks that we can process in our audio callback
    MidiMessageCollector midiCollector;

    // this represents the state of which keys on our on-screen keyboard are held
    // down. When the mouse is clicked on the keyboard component, this object also
    // generates midi messages for this, which we can pass on to our synth.
    MidiKeyboardState& keyboardState;

    // the synth itself!
    Synthesiser synth;
    
    juce::Slider attackSlider;
    juce::Slider releaseSlider;
};

//==============================================================================
class AudioSynthesiserDemo  : public juce::Component, public juce::Slider::Listener, public juce::TextEditor::Listener
{
public:
    AudioSynthesiserDemo()
    {
        image = juce::ImageFileFormat::loadFrom(File("/Users/omar/Documents/THESIS_CODE/argparse_output/closest_match_waveform.png"));

        addAndMakeVisible (keyboardComponent);

        addAndMakeVisible (sineButton);
        sineButton.setRadioGroupId (321);
        sineButton.setToggleState (true, dontSendNotification);
        sineButton.onClick = [this] { synthAudioSource.setUsingSineWaveSound(); };

        addAndMakeVisible (sampledButton);
        sampledButton.setRadioGroupId (321);
        sampledButton.onClick = [this] { synthAudioSource.setUsingSampledSound(); };

        addAndMakeVisible(saveButton);
        saveButton.setButtonText("Save");
        saveButton.onClick = [this] { saveText(); };

        addAndMakeVisible (liveAudioDisplayComp);
        audioDeviceManager.addAudioCallback (&liveAudioDisplayComp);
        audioSourcePlayer.setSource (&synthAudioSource);

        addAndMakeVisible (queryBox);
        queryBox.setMultiLine (false);
        queryBox.setReturnKeyStartsNewLine (false);
        queryBox.setReadOnly (false);
        queryBox.setScrollbarsShown (true);
        queryBox.setCaretVisible (true);
        queryBox.setPopupMenuEnabled (true);
        queryBox.setColour (TextEditor::textColourId, Colours::black);
        queryBox.setColour (TextEditor::backgroundColourId, Colours::white);
        queryBox.addListener(this);
        
        addAndMakeVisible(attackSlider);
    
        attackSlider.setRange(0.01, 5.0); // set the appropriate range
        attackSlider.setValue(3.14); // set initial value
        attackSlider.addListener(this);
        
        addAndMakeVisible(decaySlider);
        decaySlider.setRange(0.01, 5.0); // set the appropriate range
        decaySlider.setValue(0.1); // set initial value
        decaySlider.addListener(this);

        addAndMakeVisible(sustainSlider);
        sustainSlider.setRange(0.0, 1.0); // set the appropriate range
        sustainSlider.setValue(1.0); // set initial value
        sustainSlider.addListener(this);

        addAndMakeVisible(releaseSlider);
        releaseSlider.setRange(0.01, 5.0); // set the appropriate range
        releaseSlider.setValue(0.2); // set initial value
        releaseSlider.addListener(this);
        


       #ifndef JUCE_DEMO_RUNNER
        RuntimePermissions::request (RuntimePermissions::recordAudio,
                                     [this] (bool granted)
                                     {
                                         int numInputChannels = granted ? 2 : 0;
                                         audioDeviceManager.initialise (numInputChannels, 2, nullptr, true, {}, nullptr);
                                     });
       #endif

        audioDeviceManager.addAudioCallback (&audioSourcePlayer);
        audioDeviceManager.addMidiInputDeviceCallback ({}, &(synthAudioSource.midiCollector));

        setOpaque (true);
        setSize (640, 480);
    }

    ~AudioSynthesiserDemo() override
    {
        audioSourcePlayer.setSource (nullptr);
        audioDeviceManager.removeMidiInputDeviceCallback ({}, &(synthAudioSource.midiCollector));
        audioDeviceManager.removeAudioCallback (&audioSourcePlayer);
        audioDeviceManager.removeAudioCallback (&liveAudioDisplayComp);
        
        attackSlider.removeListener(this);
        decaySlider.removeListener(this);
        sustainSlider.removeListener(this);
        releaseSlider.removeListener(this);
    }

    void sliderValueChanged (juce::Slider* slider) override
    {
        if (slider == &attackSlider)
        {
            synthAudioSource.attackSlider.setValue(attackSlider.getValue());
            if (attackSlider.getValue() != 3.14)
                synthAudioSource.setUsingSampledSound();

            //synthAudioSource.setUsingSampledSound();
        }
        else if (slider == &decaySlider)
        {
            // Set decay parameter here
            // synthAudioSource.setDecayTime(decaySlider.getValue());
        }
        else if (slider == &sustainSlider)
        {
            // Set sustain parameter here
            // synthAudioSource.setSustainLevel(sustainSlider.getValue());
        }
        else if (slider == &releaseSlider)
        {
            //synthAudioSource.releaseSlider.setValue(releaseSlider.getValue());
            //synthAudioSource.setUsingSampledSound();
        }
    }
    
    void textEditorTextChanged (TextEditor& editor) override
    {
        if (&editor == &queryBox)
        {
            typedText = queryBox.getText();
        }
    }

    void saveText()
    {
        // Save the typed text to a file or perform any other desired operation
        // Here, we'll simply print it to the console
        std::cout << "Text saved: " << typedText << std::endl;

        // Create command to run argparse.py with the saved text as an argument
        String command = "/Users/omar/opt/anaconda3/bin/python /Users/omar/Documents/THESIS_CODE/argparsescript.py " + typedText;

        // Execute the command using system
        int result = system(command.toRawUTF8());

        // Check the result
        if(result == 0) {
            std::cout << "Command executed successfully" << std::endl;

            // Load the generated image
            image = juce::ImageFileFormat::loadFrom(File("/Users/omar/Documents/THESIS_CODE/argparse_output/closest_match_waveform.png"));

            // Repaint the component to update the image
            repaint();
        } else {
            std::cout << "Command execution failed" << std::endl;
        }

        sampledButton.setToggleState (true, dontSendNotification);
        synthAudioSource.setUsingSampledSound();
    }

    //==============================================================================
    void paint (Graphics& g) override
    {
        g.fillAll (getUIColourIfAvailable (LookAndFeel_V4::ColourScheme::UIColour::windowBackground));
        g.drawImage(image, 8, 8, getWidth() - 16, 64, 0, 0, image.getWidth(), image.getHeight());

    }

    void resized() override
    {
        int middleX = getWidth() / 2; // calculate middle x coordinate
        int sliderWidth = 250;
        int sliderGap = 30; // Space between the sliders
        int controlHeight = 24; // Common height for all control components
        int keyboardHeight = 64;
        int controlGap = 10; // Vertical gap between control components

        keyboardComponent   .setBounds (8, 96, getWidth() - 16, keyboardHeight);
        sineButton          .setBounds (middleX - 75, 176, 150, controlHeight);
        sampledButton       .setBounds (middleX - 75, 176 + controlHeight + controlGap, 150, controlHeight);
        //liveAudioDisplayComp.setBounds (8, 8, getWidth() - 16, 64);
        queryBox.setBounds(middleX - 100, 250, 200, controlHeight);
        saveButton.setBounds(middleX - 40, 280, 80, controlHeight);
            
        attackSlider.setBounds (16, 300, sliderWidth, 100);
        decaySlider.setBounds (16, 410, sliderWidth, 100);
        sustainSlider.setBounds (16 + sliderWidth + sliderGap, 300, sliderWidth, 100);
        releaseSlider.setBounds (16 + sliderWidth + sliderGap, 410, sliderWidth, 100);
        
        

    }
private:
    // if this PIP is running inside the demo runner, we'll use the shared device manager instead
   #ifndef JUCE_DEMO_RUNNER
    AudioDeviceManager audioDeviceManager;
   #else
    AudioDeviceManager& audioDeviceManager { getSharedAudioDeviceManager (0, 2) };
   #endif

    MidiKeyboardState keyboardState;
    AudioSourcePlayer audioSourcePlayer;
    SynthAudioSource synthAudioSource        { keyboardState };
    MidiKeyboardComponent keyboardComponent  { keyboardState, MidiKeyboardComponent::horizontalKeyboard };

    ToggleButton sineButton     { "Use sine wave" };
    ToggleButton sampledButton  { "Use sampled sound" };

    LiveScrollingAudioDisplay liveAudioDisplayComp;
    TextEditor queryBox;

    String typedText;
    TextButton saveButton;
    juce::Slider attackSlider;
    juce::Slider decaySlider;
    juce::Slider sustainSlider;
    juce::Slider releaseSlider;
    juce::Image image;
    


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSynthesiserDemo)
};
