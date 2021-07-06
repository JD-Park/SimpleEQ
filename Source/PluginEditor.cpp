/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
    float sliderPosProportional,
    float rotaryStartAngle,
    float rotaryEndAngle, juce::Slider& slider)
{
    using namespace juce;
    auto bounds = Rectangle<float>(x, y, width, height);

    g.setColour(Colour(60u, 86u, 125u));
    g.fillEllipse(bounds);

    //g.setColour(Colour(90u, 114u, 150u));
    //g.setColour(Colour(151u, 132u, 0u));
    //g.setColour(Colour(149u, 49u, 0u));
    g.setColour(Colours::black);
    g.drawEllipse(bounds, 2.0f);

    if (auto* rswl = dynamic_cast<RotarySliderWithLabels*>(&slider))
    {
        auto center = bounds.getCentre();
        Path p;
        Rectangle<float> r;
        r.setLeft(center.getX() - 2);
        r.setRight(center.getX() + 2);
        r.setTop(bounds.getY());
        r.setBottom(center.getY() - rswl->getTextHeight() * 1.5);

        p.addRoundedRectangle(r, 2.0f);

        jassert(rotaryStartAngle < rotaryEndAngle);

        auto sliderAngRad = jmap(sliderPosProportional, 0.0f, 1.0f, rotaryStartAngle, rotaryEndAngle);

        p.applyTransform(AffineTransform().rotated(sliderAngRad, center.getX(), center.getY()));

        g.fillPath(p);

        g.setFont(rswl->getTextHeight());
        auto text = rswl->getDisplayString();
        auto strWidth = g.getCurrentFont().getStringWidth(text);
        r.setSize(strWidth + 4, rswl->getTextHeight() + 2);
        r.setCentre(center);

        //g.setColour(Colours::black);
        //g.fillRect(r);

        g.setColour(Colour(179u, 196u, 224u));
        g.drawFittedText(text, r.toNearestInt(), juce::Justification::centred, 1);
    }
}

void RotarySliderWithLabels::paint(juce::Graphics& g)
{
    using namespace juce;
    auto startAng = degreesToRadians(180.0f + 45.0f);
    auto endAng = degreesToRadians(180.0f - 45.0f) + MathConstants<float>::twoPi;

    auto range = getRange();
    auto sliderBounds = getSliderBounds();

    //g.setColour(Colours::red);
    //g.drawRect(getLocalBounds());
    //g.setColour(Colours::yellow);
    //g.drawRect(sliderBounds);

    getLookAndFeel().drawRotarySlider(g, sliderBounds.getX(),
        sliderBounds.getY(), sliderBounds.getWidth(), sliderBounds.getHeight(), jmap(getValue(),
            range.getStart(), range.getEnd(), 0.0, 1.0), startAng, endAng, *this);

    auto center = sliderBounds.toFloat().getCentre();
    auto radius = sliderBounds.getWidth() * 0.5f;

    g.setColour(Colour(151u, 132u, 0u));
    //g.setColour(Colour(149u, 49u, 0u));
    g.setFont(getTextHeight());

    auto numChoices = labels.size();
    for (int i = 0; i < numChoices; ++i)
    {
        auto pos = labels[i].pos;
        jassert(0.0f <= pos);
        jassert(pos <= 1.0f);

        auto ang = jmap(pos, 0.0f, 1.0f, startAng, endAng);

        auto c = center.getPointOnCircumference(radius + getTextHeight() * 0.5f + 2, ang);

        Rectangle<float> r;
        auto str = labels[i].label;
        r.setSize(g.getCurrentFont().getStringWidth(str), getTextHeight());
        r.setCentre(c);
        r.setY(r.getY() + getTextHeight());

        g.drawFittedText(str, r.toNearestInt(), juce::Justification::centred, 1);
    }
}

juce::Rectangle<int> RotarySliderWithLabels::getSliderBounds() const
{
    auto bounds = getLocalBounds();

    auto size = juce::jmin(bounds.getWidth(), bounds.getHeight());

    size -= getTextHeight() * 2;
    juce::Rectangle<int> r;
    r.setSize(size, size);
    r.setCentre(bounds.getCentreX(), 0);
    r.setY(2);

    return r;
}

juce::String RotarySliderWithLabels::getDisplayString() const
{
    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(param))
    {
        return choiceParam->getCurrentChoiceName();
    }
    juce::String str;
    bool addK = false;

    if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param))
    {
        float val = getValue();
        if (val > 999.0f)
        {
            val /= 1000.0f;
            addK = true;
        }

        str = juce::String(val, (addK ? 2 : 0));
    }
    else
    {
        jassertfalse;
    }

    if (suffix.isNotEmpty())
    {
        str << " ";
        if (addK)
        {
            str << "k";
        }
        str << suffix;
    }

    return str;
}

ResponseCurveComponent::ResponseCurveComponent(SimpleEQAudioProcessor& p) : audioProcessor(p),
leftChannelFifo(&audioProcessor.leftChannelFifo)
{
    const auto& params = audioProcessor.getParameters();
    for (auto param : params)
    {
        param->addListener(this);
    }

    leftChannelFFTDataGenerator.changeOrder(FFTOrder::order2048);
    monoBuffer.setSize(1, leftChannelFFTDataGenerator.getFFTSize());

    updateChain();

    startTimerHz(60);
}
void ResponseCurveComponent::parameterValueChanged(int parameterIndex, float newValue)
{
    parametersChanged.set(true);
}

ResponseCurveComponent::~ResponseCurveComponent()
{
    const auto& params = audioProcessor.getParameters();
    for (auto param : params)
    {
        param->removeListener(this);
    }
}

void ResponseCurveComponent::timerCallback()
{
    juce::AudioBuffer<float> tempIncomingBuffer;

    while (leftChannelFifo->getNumCompleteBuffersAvailable() > 0)
    {
        if (leftChannelFifo->getAudioBuffer(tempIncomingBuffer))
        {
            auto size = tempIncomingBuffer.getNumSamples();

            juce::FloatVectorOperations::copy(monoBuffer.getWritePointer(0, 0),
                monoBuffer.getReadPointer(0, size),
                monoBuffer.getNumSamples() - size);

            juce::FloatVectorOperations::copy(monoBuffer.getWritePointer(0, monoBuffer.getNumSamples() - size),
                tempIncomingBuffer.getReadPointer(0, 0),
                size);
            
            leftChannelFFTDataGenerator.produceFFTDataForRendering(monoBuffer, -48.0f);
        }
    }

    const auto fftBounds = getAnalysisArea().toFloat();
    const auto fftSize = leftChannelFFTDataGenerator.getFFTSize();
    const auto binWidth = audioProcessor.getSampleRate() / (double)fftSize;

    while (leftChannelFFTDataGenerator.getNumAvailableFFTDataBlocks() > 0)
    {
        std::vector<float> fftData;
        if (leftChannelFFTDataGenerator.getFFTData(fftData))
        {
            pathProducer.generatePath(fftData, fftBounds, fftSize, binWidth, -48.0f);
        }
    }

    while (pathProducer.getNumPathsAvailable())
    {
        pathProducer.getPath(leftChannelFFTPath);
    }

    if (parametersChanged.compareAndSetBool(false, true));
    {
        //update the monochain
        updateChain();
        //signal a repaint
        //repaint();
    }

    repaint();
}

void ResponseCurveComponent::updateChain()
{
    auto chainSettings = getChainSettings(audioProcessor.apvts);
    auto peakCoefficients = makePeakFilter(chainSettings, audioProcessor.getSampleRate());
    updateCoefficients(monoChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);

    auto lowCutCoefficient = makeLowCutFilter(chainSettings, audioProcessor.getSampleRate());
    auto highCutCoefficient = makeHighCutFilter(chainSettings, audioProcessor.getSampleRate());

    updateCutFilter(monoChain.get<ChainPositions::LowCut>(), lowCutCoefficient, chainSettings.lowCutSlope);
    updateCutFilter(monoChain.get<ChainPositions::HighCut>(), highCutCoefficient, chainSettings.highCutSlope);
}

void ResponseCurveComponent::paint(juce::Graphics& g)
{
    using namespace juce;
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    //g.fillAll(Colours::black);

    auto imageArea = getLocalBounds();

    //imageArea.removeFromTop(12);
    //imageArea.removeFromBottom(2);
    //imageArea.removeFromLeft(20);
    //imageArea.removeFromRight(20);

    g.drawImage(background, imageArea.toFloat());

    //auto responseArea = getLocalBounds();
    //auto responseArea = getRenderArea();
    auto responseArea = getAnalysisArea();

    auto w = responseArea.getWidth();

    auto& lowcut = monoChain.get<ChainPositions::LowCut>();
    auto& peak = monoChain.get<ChainPositions::Peak>();
    auto& highcut = monoChain.get<ChainPositions::HighCut>();

    auto sampleRate = audioProcessor.getSampleRate();

    std::vector<double> mags;

    mags.resize(w);

    for (int i = 0; i < w; ++i)
    {
        double mag = 1.0f;
        auto freq = mapToLog10(double(i) / double(w), 20.0, 20000.0);

        if (!monoChain.isBypassed<ChainPositions::Peak>())
        {
            mag *= peak.coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }

        if (!lowcut.isBypassed<0>())
        {
            mag *= lowcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        if (!lowcut.isBypassed<1>())
        {
            mag *= lowcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        if (!lowcut.isBypassed<2>())
        {
            mag *= lowcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        if (!lowcut.isBypassed<3>())
        {
            mag *= lowcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }

        if (!highcut.isBypassed<0>())
        {
            mag *= highcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        if (!highcut.isBypassed<1>())
        {
            mag *= highcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        if (!highcut.isBypassed<2>())
        {
            mag *= highcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }
        if (!highcut.isBypassed<3>())
        {
            mag *= highcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        }

        mags[i] = Decibels::gainToDecibels(mag);
    }

    Path responseCurve;
    const double outputMin = responseArea.getBottom();
    const double outputMax = responseArea.getY();
    auto map = [outputMin, outputMax](double input)
    {
        return jmap(input, -24.0, 24.0, outputMin, outputMax);
    };

    responseCurve.startNewSubPath(responseArea.getX(), map(mags.front()));

    for (size_t i = 1; i < mags.size(); ++i)
    {
        responseCurve.lineTo(responseArea.getX() + i, map(mags[i]));
    }

    g.setColour(Colours::white);
    g.strokePath(leftChannelFFTPath, PathStrokeType(1.0f));

    g.setColour(Colour(90u, 114u, 150u));
    g.drawRoundedRectangle(getRenderArea().toFloat(), 4.0f, 4.0f);

    g.setColour(Colour(179u, 196u, 224u));
    g.strokePath(responseCurve, PathStrokeType(2.0f));
}

void ResponseCurveComponent::resized()
{
    using namespace juce;
    background = Image(Image::PixelFormat::RGB, getWidth(), getHeight(), true);

    Graphics g(background);

    Array<float> freqs
    {
        20, /*30, 40,*/ 50, 100,
        200, /*300, 400,*/ 500, 1000,
        2000, /*3000, 4000,*/ 5000, 10000,
        20000, 
    };

    auto renderArea = getAnalysisArea();
    auto left = renderArea.getX();
    auto right = renderArea.getRight();
    auto top = renderArea.getY();
    auto bottom = renderArea.getBottom();
    auto width = renderArea.getWidth();

    Array<float> xs;
    for (auto f : freqs)
    {
        auto normX = mapFromLog10(f, 20.0f, 20000.0f);
        xs.add(left + width * normX);
    }

    g.setColour( Colour(60u, 86u, 125u));
    for (auto x : xs)
    {
        g.drawVerticalLine(x, top, bottom);
    }

    Array<float> gain
    {
        -24, -12, 0, 12, 24
    };

    for (auto gDb : gain)
    {
        auto y = jmap(gDb, -24.0f, 24.0f, float(bottom), float(top));
        g.setColour(gDb == 0.0f ? Colour(151u, 132u, 0u) : Colour(60u, 86u, 125u));
        g.drawHorizontalLine(y, left, right);
    }

    //g.drawRect(getAnalysisArea());

    g.setColour(Colour(151u, 132u, 0));
    const int fontHeight = 10;
    g.setFont(fontHeight);

    for (int i = 0; i < freqs.size(); ++i)
    {
        auto f = freqs[i];
        auto x = xs[i];

        bool addK = false;
        String str;
        if (f > 999.f)
        {
            addK = true;
            f /= 1000.f;
        }

        str << f;
        if (addK)
            str << "k";
        str << "Hz";

        auto textWidth = g.getCurrentFont().getStringWidth(str);

        Rectangle<int> r;

        r.setSize(textWidth, fontHeight);
        r.setCentre(x, 0);
        r.setY(1);

        g.drawFittedText(str, r, juce::Justification::centred, 1);
    }

    //auto gain = getGains();

    for (auto gDb : gain)
    {
        auto y = jmap(gDb, -24.f, 24.f, float(bottom), float(top));

        String str;
        if (gDb > 0)
            str << "+";
        str << gDb;

        auto textWidth = g.getCurrentFont().getStringWidth(str);

        Rectangle<int> r;
        r.setSize(textWidth, fontHeight);
        r.setX(getWidth() - textWidth);
        r.setCentre(r.getCentreX(), y);

        g.setColour(gDb == 0.f ? Colour(151u, 132u, 0u) : Colours::white);

        g.drawFittedText(str, r, juce::Justification::centredLeft, 1);

        str.clear();
        str << (gDb - 24.f);

        r.setX(1);
        textWidth = g.getCurrentFont().getStringWidth(str);
        r.setSize(textWidth, fontHeight);
        g.setColour(Colours::lightgrey);
        g.drawFittedText(str, r, juce::Justification::centredLeft, 1);
    }
}

juce::Rectangle<int> ResponseCurveComponent::getRenderArea()
{
    auto bounds = getLocalBounds();

    //bounds.reduce(10, 8);
    bounds.removeFromTop(12);
    bounds.removeFromBottom(2);
    bounds.removeFromLeft(20);
    bounds.removeFromRight(20);

    return bounds;
}

juce::Rectangle<int> ResponseCurveComponent::getAnalysisArea()
{
    auto bounds = getRenderArea();

    bounds.removeFromTop(4);
    bounds.removeFromBottom(4);
    return bounds;
}


//==============================================================================
SimpleEQAudioProcessorEditor::SimpleEQAudioProcessorEditor(SimpleEQAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p),
peakFreqSlider(*audioProcessor.apvts.getParameter("Peak Freq"), "Hz"),
peakGainSlider(*audioProcessor.apvts.getParameter("Peak Gain"), "dB"),
peakQualitySlider(*audioProcessor.apvts.getParameter("Peak Quality"), ""),
lowCutFreqSlider(*audioProcessor.apvts.getParameter("Low-Cut Freq"), "Hz"),
highCutFreqSlider(*audioProcessor.apvts.getParameter("High-Cut Freq"), "Hz"),
lowCutSlopeSlider(*audioProcessor.apvts.getParameter("Low-Cut Slope"), "dB/Oct"),
highCutSlopeSlider(*audioProcessor.apvts.getParameter("High-Cut Slope"), "dB/Oct"),
responseCurveComponent(audioProcessor),
peakFreqSliderAttachment(audioProcessor.apvts, "Peak Freq", peakFreqSlider),
peakGainSliderAttachment(audioProcessor.apvts, "Peak Gain", peakGainSlider),
peakQualitySliderAttachment(audioProcessor.apvts, "Peak Quality", peakQualitySlider),
lowCutFreqSliderAttachment(audioProcessor.apvts, "Low-Cut Freq", lowCutFreqSlider),
highCutFreqSliderAttachment(audioProcessor.apvts, "High-Cut Freq", highCutFreqSlider),
lowCutSlopeSliderAttachment(audioProcessor.apvts, "Low-Cut Slope", lowCutSlopeSlider),
highCutSlopeSliderAttachment(audioProcessor.apvts, "High-Cut Slope", highCutSlopeSlider)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.

    peakFreqSlider.labels.add({ 0.0f, "20 Hz" });
    peakFreqSlider.labels.add({ 1.0f, "20 kHz" });

    peakGainSlider.labels.add({ 0.0f, "-24 dB" });
    peakGainSlider.labels.add({ 1.0f, "24 dB" });

    peakQualitySlider.labels.add({ 0.0f, "0.1" });
    peakQualitySlider.labels.add({ 1.0f, "10" });
    
    lowCutFreqSlider.labels.add({ 0.0f, "20 Hz" });
    lowCutFreqSlider.labels.add({ 1.0f, "20 kHz" });
    
    highCutFreqSlider.labels.add({ 0.0f, "20 Hz" });
    highCutFreqSlider.labels.add({ 1.0f, "20 kHz" });
    
    lowCutSlopeSlider.labels.add({ 0.0f, "12" });
    lowCutSlopeSlider.labels.add({ 1.0f, "48" });
    
    highCutSlopeSlider.labels.add({ 0.0f, "12" });
    highCutSlopeSlider.labels.add({ 1.0f, "48" });

    for (auto* comps : getComps())
    {
        addAndMakeVisible(comps);
    }    

    setSize (600, 480);
}

SimpleEQAudioProcessorEditor::~SimpleEQAudioProcessorEditor()
{
    
}

//==============================================================================
void SimpleEQAudioProcessorEditor::paint(juce::Graphics& g)
{
    using namespace juce;
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll(Colour(1u, 31u, 75u));
}
    
void SimpleEQAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    auto bounds = getLocalBounds();
    float hRatio = 25.0f / 100.0f; //JUCE_LIVE_CONSTANT(33) / 100.0f;
    auto responseArea = bounds.removeFromTop(bounds.getHeight() * hRatio);

    responseCurveComponent.setBounds(responseArea);

    bounds.removeFromTop(10);

    auto lowCutArea = bounds.removeFromLeft(bounds.getWidth() * 0.33);
    auto highCutArea = bounds.removeFromRight(bounds.getWidth() * 0.5);

    lowCutFreqSlider.setBounds(lowCutArea.removeFromTop(lowCutArea.getHeight() * 0.5));
    lowCutSlopeSlider.setBounds(lowCutArea);

    highCutFreqSlider.setBounds(highCutArea.removeFromTop(highCutArea.getHeight() * 0.5));
    highCutSlopeSlider.setBounds(highCutArea);

    peakFreqSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.33)); 
    peakGainSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.5));
    peakQualitySlider.setBounds(bounds);
}

std::vector<juce::Component*> SimpleEQAudioProcessorEditor::getComps()
{
    return
    {
        &peakFreqSlider,
        &peakGainSlider,
        &peakQualitySlider,
        &lowCutFreqSlider,
        &highCutFreqSlider,
        &lowCutSlopeSlider,
        &highCutSlopeSlider,
        &responseCurveComponent
    };
}
